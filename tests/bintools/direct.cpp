// Direct IBinTools suite, shared by the extension (bttest.ext.so) and the
// standalone runner (bttest_standalone), which links bintools from any commit.
//
// Every case calls a g++-built callee once directly from C++ and once through
// an IBinTools wrapper, then compares what the callee received (its log) and
// what came back. The direct call is the reference, so the compiler decides
// every ABI detail. Each run also checks:
//   - callee entered with a 16-byte aligned stack
//   - callee-saved registers and rsp intact after Execute
//   - no read past the parameter buffer / write past the return buffer
//     (both sit flush against a PROT_NONE page; faults are caught)
//   - by-ref arguments written back into the parameter buffer
//   - vtable dispatch reached the right slot
#include "direct.h"
#include <sys/mman.h>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include "callees.h"

using namespace SourceMod;

extern "C" uint64_t bt_call_checked(void *fn, void *a, void *b, void *c);

using OF = ObjectField;
#define FL OF::Float
#define DB OF::Double
#define I8 OF::Int8
#define I16 OF::Int16
#define I32 OF::Int32
#define I64 OF::Int64

struct Arg
{
	PassType type;
	unsigned int flags;
	std::vector<OF> fields;
	std::vector<uint8_t> val;
};

template <typename T> static std::vector<uint8_t> Bytes(const T &v)
{
	std::vector<uint8_t> b(sizeof(T));
	memcpy(b.data(), &v, sizeof(T));
	return b;
}
template <typename T> static Arg AI(T v) { return { PassType_Basic, PASSFLAG_BYVAL, {}, Bytes(v) }; }
template <typename T> static Arg AF(T v) { return { PassType_Float, PASSFLAG_BYVAL, {}, Bytes(v) }; }
template <typename T> static Arg ARefI(T v) { return { PassType_Basic, PASSFLAG_BYREF, {}, Bytes(v) }; }
template <typename T> static Arg ARefF(T v) { return { PassType_Float, PASSFLAG_BYREF, {}, Bytes(v) }; }
template <typename T> static Arg AO(const T &v, std::vector<OF> f, unsigned int extra = 0)
{ return { PassType_Object, PASSFLAG_BYVAL | extra, f, Bytes(v) }; }
template <typename T> static Arg ARefO(const T &v, std::vector<OF> f)
{ return { PassType_Object, PASSFLAG_BYREF, f, Bytes(v) }; }

struct Case
{
	std::string name;
	void *fn = nullptr;
	CallConvention cv = CallConv_Cdecl;
	unsigned int fnflags = 0;
	bool vcall = false;
	unsigned int vidx = 0, vtblOffs = 0, thisOffs = 0;
	std::vector<Arg> args;
	bool hasRet = false;
	PassType retType = PassType_Basic;
	unsigned int retFlags = PASSFLAG_BYVAL;
	std::vector<OF> retFields;
	size_t retSize = 0;
	std::function<void(void *)> direct;                       // reference call; writes the return value
	std::function<bool(const void *, const void *)> retEq;
	std::function<bool(const uint8_t *, ICallWrapper *, std::string &)> post;

	explicit Case(const char *n) : name(n) {}
};

// Comparison of return values, field-wise where a type has padding
template <typename T> static bool EqT(const T &a, const T &b) { return memcmp(&a, &b, sizeof(T)) == 0; }
static bool EqT(const FD &a, const FD &b) { return !memcmp(&a.a, &b.a, 4) && !memcmp(&a.b, &b.b, 8); }
static bool EqT(const S10 &a, const S10 &b) { return a.a == b.a && a.b == b.b; }
static bool EqT(const NTBig &a, const NTBig &b) { return !memcmp(a.v, b.v, sizeof(a.v)) && a.eh == b.eh && a.type == b.type; }

template <typename R, typename F> static void Ret(Case &c, PassType t, F f, std::vector<OF> fields = {}, unsigned int extra = 0)
{
	c.hasRet = true;
	c.retType = t;
	c.retFlags = PASSFLAG_BYVAL | extra;
	c.retFields = fields;
	c.retSize = sizeof(R);
	c.direct = [f](void *out) { R r = f(); memcpy(out, (const void *)&r, sizeof(R)); };
	c.retEq = [](const void *a, const void *b) {
		R ra, rb;
		memcpy((void *)&ra, a, sizeof(R));
		memcpy((void *)&rb, b, sizeof(R));
		return EqT(ra, rb);
	};
}
template <typename F> static void Void(Case &c, F f)
{
	c.direct = [f](void *) { f(); };
}

template <typename T> static T ArgAt(const uint8_t *buf, ICallWrapper *w, unsigned int i)
{
	T v;
	memcpy((void *)&v, buf + w->GetParamInfo(i)->offset, sizeof(T));
	return v;
}

/* Fault capture: a JIT bug that reads/writes past a buffer or passes a bad
 * pointer faults; turn that into a test failure instead of a crash. */
static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_inCall = 0;
static struct sigaction g_oldAct[4];
static const int kSigs[4] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };

static void FaultHandler(int sig, siginfo_t *, void *)
{
	if (g_inCall)
	{
		g_inCall = 0;
		siglongjmp(g_jmp, sig);
	}
	for (int i = 0; i < 4; i++)
		if (kSigs[i] == sig)
			sigaction(sig, &g_oldAct[i], nullptr);
	raise(sig);
}
static void InstallFaultHandlers()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = FaultHandler;
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	for (int i = 0; i < 4; i++)
		sigaction(kSigs[i], &sa, &g_oldAct[i]);
}
static void RemoveFaultHandlers()
{
	for (int i = 0; i < 4; i++)
		sigaction(kSigs[i], &g_oldAct[i], nullptr);
}

// Buffers that end exactly at a PROT_NONE page
struct GuardBuf
{
	uint8_t *base = nullptr;
	void Init()
	{
		if (base)
			return;
		base = (uint8_t *)mmap(nullptr, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		mprotect(base + 4096, 4096, PROT_NONE);
	}
	uint8_t *Tail(size_t size) { return base + 4096 - size; }
};
static GuardBuf g_paramBuf, g_retBuf;

// Fake objects for vtable tests
VObj g_obj;
void *g_vtable[64];

extern "C" void bt_exec_thunk(ICallWrapper *w, void *params, void *ret)
{
	w->Execute(params, ret);
}

static std::string Hex(const uint8_t *p, size_t n)
{
	std::string s;
	char b[4];
	for (size_t i = 0; i < n; i++)
	{
		snprintf(b, sizeof(b), "%02x", p[i]);
		s += b;
		if (i % 4 == 3 && i + 1 < n)
			s += ' ';
	}
	return s;
}

static int g_pass, g_fail;
static bool g_verbose;
IBinTools *g_pBinTools = nullptr;

static void Report(const Case &c, bool ok, const std::string &why)
{
	if (ok)
	{
		g_pass++;
		if (g_verbose)
			bt_print("[bttest] ok    %s", c.name.c_str());
	}
	else
	{
		g_fail++;
		bt_print("[bttest] FAIL  %s: %s", c.name.c_str(), why.c_str());
	}
}

static void Run(Case &c)
{
	std::string why;

	// PassInfo arrays (fields point into the case, which outlives the wrapper build)
	std::vector<PassInfo> pis(c.args.size());
	for (size_t i = 0; i < c.args.size(); i++)
	{
		Arg &a = c.args[i];
		pis[i].type = a.type;
		pis[i].flags = a.flags;
		pis[i].size = a.val.size();
		pis[i].fields = a.fields.empty() ? nullptr : a.fields.data();
		pis[i].numFields = (unsigned int)a.fields.size();
	}
	PassInfo ret;
	ret.type = c.retType;
	ret.flags = c.retFlags;
	ret.size = c.retSize;
	ret.fields = c.retFields.empty() ? nullptr : c.retFields.data();
	ret.numFields = (unsigned int)c.retFields.size();

	// Building the wrapper runs the JIT; a codegen bug can fault here too
	ICallWrapper *w = nullptr;
	int csig = sigsetjmp(g_jmp, 1);
	if (csig)
	{
		char b[64];
		snprintf(b, sizeof(b), "signal %d while building the wrapper", csig);
		Report(c, false, b);
		return;
	}
	g_inCall = 1;
	if (c.vcall)
	{
		// vtable at this + thisOffs + vtblOffs; every other slot is a trap
		for (int i = 0; i < 64; i++)
			g_vtable[i] = (void *)c_wrong_slot;
		g_vtable[c.vidx] = c.fn;
		memset(&g_obj, 0, sizeof(g_obj));
		void **slot = (void **)(g_obj.pad + c.thisOffs + c.vtblOffs);
		*slot = g_vtable;
		w = g_pBinTools->CreateVCall(c.vidx, c.vtblOffs, c.thisOffs, c.hasRet ? &ret : nullptr,
			pis.data(), (unsigned int)pis.size(), c.fnflags);
	}
	else
	{
		w = g_pBinTools->CreateCall(c.fn, c.cv, c.hasRet ? &ret : nullptr,
			pis.data(), (unsigned int)pis.size(), c.fnflags);
	}
	g_inCall = 0;
	if (!w)
	{
		Report(c, false, "wrapper not created");
		return;
	}
	bool thiscall = c.vcall || c.cv == CallConv_ThisCall;

	// Reference: direct call
	uint8_t expRet[64] = {};
	memset(&bt_rec, 0, sizeof(bt_rec));
	c.direct(expRet);
	BtRec expRec = bt_rec;

	size_t total = thiscall ? sizeof(void *) : 0;
	for (size_t i = 0; i < c.args.size(); i++)
	{
		size_t end = w->GetParamInfo((unsigned int)i)->offset + c.args[i].val.size();
		if (end > total)
			total = end;
	}

	bool ok = true;
	for (int pass = 0; pass < 2 && ok; pass++)        // twice: a wrapper must be re-executable
	{
		uint8_t *params = g_paramBuf.Tail(total ? total : 1);
		if (thiscall)
		{
			VObj *self = &g_obj;
			memcpy(params, &self, sizeof(self));
		}
		for (size_t i = 0; i < c.args.size(); i++)
			memcpy(params + w->GetParamInfo((unsigned int)i)->offset, c.args[i].val.data(), c.args[i].val.size());
		std::vector<uint8_t> before(params, params + total);

		uint8_t *retbuf = c.hasRet ? g_retBuf.Tail(c.retSize) : nullptr;
		if (retbuf)
			memset(retbuf, 0xCC, c.retSize);

		memset(&bt_rec, 0, sizeof(bt_rec));
		uint64_t regs = 0;
		int sig = sigsetjmp(g_jmp, 1);
		if (sig == 0)
		{
			g_inCall = 1;
			regs = bt_call_checked((void *)bt_exec_thunk, w, total ? params : nullptr, retbuf);
			g_inCall = 0;
		}
		else
		{
			char b[64];
			snprintf(b, sizeof(b), "signal %d during call", sig);
			why = b;
			ok = false;
			break;
		}

		char b[256];
		if (regs)
		{
			snprintf(b, sizeof(b), "callee-saved registers clobbered (mask 0x%llx: rbx r12 r13 r14 r15 rsp)", (unsigned long long)regs);
			why = b; ok = false;
		}
		else if (bt_rec.misaligned)
		{
			why = "callee entered with misaligned stack"; ok = false;
		}
		else if (bt_rec.wrongslot)
		{
			why = "vtable dispatch hit the wrong slot"; ok = false;
		}
		else if (bt_rec.calls != expRec.calls)
		{
			snprintf(b, sizeof(b), "callee ran %d times, expected %d", bt_rec.calls, expRec.calls);
			why = b; ok = false;
		}
		else if (thiscall && bt_rec.self != &g_obj)
		{
			snprintf(b, sizeof(b), "this = %p, expected %p", bt_rec.self, (void *)&g_obj);
			why = b; ok = false;
		}
		else if (bt_rec.n != expRec.n || memcmp(bt_rec.log, expRec.log, bt_rec.n))
		{
			size_t d = 0;
			while (d < bt_rec.n && d < expRec.n && bt_rec.log[d] == expRec.log[d])
				d++;
			size_t from = d & ~(size_t)7;
			size_t len = 16;
			snprintf(b, sizeof(b), "callee received different args (log %zu vs %zu bytes, first diff at %zu): got %s want %s",
				bt_rec.n, expRec.n, d,
				Hex(bt_rec.log + from, std::min(len, bt_rec.n > from ? bt_rec.n - from : 0)).c_str(),
				Hex(expRec.log + from, std::min(len, expRec.n > from ? expRec.n - from : 0)).c_str());
			why = b; ok = false;
		}
		else if (c.hasRet && !c.retEq(retbuf, expRet))
		{
			why = "return value: got " + Hex(retbuf, c.retSize) + " want " + Hex(expRet, c.retSize);
			ok = false;
		}
		else if (c.post)
		{
			ok = c.post(params, w, why);
		}
		else if (memcmp(before.data(), params, total))
		{
			why = "parameter buffer modified by a by-value call"; ok = false;
		}
		if (!ok && pass == 1)
			why = "(second execution) " + why;
	}
	Report(c, ok, why);
	w->Destroy();
}

#define CHECK_POST(cond, msg) do { if (!(cond)) { why = msg; return false; } } while (0)

int BtRunDirect(bool verbose, int *passed)
{
	g_pass = g_fail = 0;
	g_verbose = verbose;
	g_paramBuf.Init();
	g_retBuf.Init();
	InstallFaultHandlers();

	/* -------- integers -------- */
	{
		Case c("ints: 1/2/4/8-byte signed+unsigned, bool, 2 on stack");
		c.fn = (void *)c_ints8;
		c.args = { AI<int8_t>(-5), AI<uint8_t>(250), AI<int16_t>(-1234), AI<uint16_t>(65000), AI<int32_t>(-123456789),
			AI<uint32_t>(4000000000u), AI<int64_t>(-0x123456789ABCDEFLL), AI<bool>(true) };
		Ret<int64_t>(c, PassType_Basic, [] { return c_ints8(-5, 250, -1234, 65000, -123456789, 4000000000u, -0x123456789ABCDEFLL, true); });
		Run(c);
	}
	{
		Case c("ints: 24 x int64 (18 on stack, offsets past 127)");
		c.fn = (void *)c_ints24;
		int64_t v[24];
		for (int i = 0; i < 24; i++)
			v[i] = (i & 1 ? -1 : 1) * (0x0101010101LL * (i + 1));
		for (int i = 0; i < 24; i++)
			c.args.push_back(AI<int64_t>(v[i]));
		Ret<int64_t>(c, PassType_Basic, [v] { return c_ints24(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],
			v[12],v[13],v[14],v[15],v[16],v[17],v[18],v[19],v[20],v[21],v[22],v[23]); });
		Run(c);
	}
	{
		Case c("ints: narrow types in stack slots");
		c.fn = (void *)c_narrow_stack;
		c.args = { AI<int64_t>(1), AI<int64_t>(2), AI<int64_t>(3), AI<int64_t>(4), AI<int64_t>(5), AI<int64_t>(6),
			AI<int8_t>(-100), AI<uint8_t>(200), AI<int16_t>(-30000), AI<uint16_t>(60000), AI<int32_t>(-2000000000),
			AI<uint32_t>(3000000000u), AI<bool>(true), AI<int64_t>(-7) };
		Ret<int32_t>(c, PassType_Basic, [] { return c_narrow_stack(1, 2, 3, 4, 5, 6, -100, 200, -30000, 60000, -2000000000, 3000000000u, true, -7); });
		Run(c);
	}
	{ Case c("return int8"); c.fn = (void *)c_ret_i8; c.args = { AI<int32_t>(-3) }; Ret<int8_t>(c, PassType_Basic, [] { return c_ret_i8(-3); }); Run(c); }
	{ Case c("return uint8"); c.fn = (void *)c_ret_u8; c.args = { AI<int32_t>(0x1F3) }; Ret<uint8_t>(c, PassType_Basic, [] { return c_ret_u8(0x1F3); }); Run(c); }
	{ Case c("return int16"); c.fn = (void *)c_ret_i16; c.args = { AI<int32_t>(-300) }; Ret<int16_t>(c, PassType_Basic, [] { return c_ret_i16(-300); }); Run(c); }
	{ Case c("return uint16"); c.fn = (void *)c_ret_u16; c.args = { AI<int32_t>(0x1FFFE) }; Ret<uint16_t>(c, PassType_Basic, [] { return c_ret_u16(0x1FFFE); }); Run(c); }
	{ Case c("return int32"); c.fn = (void *)c_ret_i32; c.args = { AI<int32_t>(-12345) }; Ret<int32_t>(c, PassType_Basic, [] { return c_ret_i32(-12345); }); Run(c); }
	{ Case c("return int64"); c.fn = (void *)c_ret_i64; c.args = { AI<int64_t>(-0x7777777777LL) }; Ret<int64_t>(c, PassType_Basic, [] { return c_ret_i64(-0x7777777777LL); }); Run(c); }
	{ Case c("return bool"); c.fn = (void *)c_ret_bool; c.args = { AI<int32_t>(0x101) }; Ret<bool>(c, PassType_Basic, [] { return c_ret_bool(0x101); }); Run(c); }
	{ Case c("return pointer"); c.fn = (void *)c_ret_ptr; c.args = { AI<void *>((void *)&g_obj) }; Ret<void *>(c, PassType_Basic, [] { return c_ret_ptr((void *)&g_obj); }); Run(c); }
	{
		Case c("int128 param (2 regs) + int128 return");
		c.fn = (void *)c_i128;
		__int128 b = ((__int128)0x0123456789ABCDEFLL << 64) | 0xFEDCBA9876543210ULL;
		c.args = { AI<int64_t>(5), AI<__int128>(b), AI<int64_t>(-9) };
		Ret<__int128>(c, PassType_Basic, [b] { return c_i128(5, b, -9); });
		Run(c);
	}
	{
		Case c("int128 param that doesn't fit the last register");
		c.fn = (void *)c_i128_stack;
		__int128 b = ((__int128)0x1111222233334444LL << 64) | 0x5555666677778888ULL;
		c.args = { AI<int64_t>(1), AI<int64_t>(2), AI<int64_t>(3), AI<int64_t>(4), AI<int64_t>(5), AI<__int128>(b), AI<int64_t>(6) };
		Ret<int64_t>(c, PassType_Basic, [b] { return c_i128_stack(1, 2, 3, 4, 5, b, 6); });
		Run(c);
	}

	/* -------- floating point -------- */
	{
		Case c("floats: 10 x float (2 on stack)");
		c.fn = (void *)c_f10;
		float v[10];
		for (int i = 0; i < 10; i++)
			v[i] = (i - 4.5f) * 1.375f;
		for (int i = 0; i < 10; i++)
			c.args.push_back(AF<float>(v[i]));
		Ret<float>(c, PassType_Float, [v] { return c_f10(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]); });
		Run(c);
	}
	{
		Case c("floats: 10 x double (2 on stack)");
		c.fn = (void *)c_d10;
		double v[10];
		for (int i = 0; i < 10; i++)
			v[i] = (i - 4.5) * 1e10 + 0.1;
		for (int i = 0; i < 10; i++)
			c.args.push_back(AF<double>(v[i]));
		Ret<double>(c, PassType_Float, [v] { return c_d10(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]); });
		Run(c);
	}
	{
		Case c("floats: 30 x double (22 on stack, offsets past 127)");
		c.fn = (void *)c_d30;
		double v[30];
		for (int i = 0; i < 30; i++)
			v[i] = (i - 15) * 3.25 + i * 1e-9;
		for (int i = 0; i < 30; i++)
			c.args.push_back(AF<double>(v[i]));
		Ret<double>(c, PassType_Float, [v] { return c_d30(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],v[13],v[14],
			v[15],v[16],v[17],v[18],v[19],v[20],v[21],v[22],v[23],v[24],v[25],v[26],v[27],v[28],v[29]); });
		Run(c);
	}
	{
		Case c("floats: float/double alternating");
		c.fn = (void *)c_fd_mix;
		for (int i = 0; i < 12; i++)
			c.args.push_back(i & 1 ? AF<double>(i * 1.5e100) : AF<float>(i * -0.75f));
		Ret<double>(c, PassType_Float, [] { return c_fd_mix(0 * -0.75f, 1 * 1.5e100, 2 * -0.75f, 3 * 1.5e100, 4 * -0.75f, 5 * 1.5e100,
			6 * -0.75f, 7 * 1.5e100, 8 * -0.75f, 9 * 1.5e100, 10 * -0.75f, 11 * 1.5e100); });
		Run(c);
	}
	{
		Case c("ints+floats interleaved, both register files spill");
		c.fn = (void *)c_if_mix;
		c.args = { AI<int32_t>(-1), AF<float>(1.5f), AI<int64_t>(1LL << 40), AF<double>(-2.25), AI<int8_t>(-8), AF<float>(3.5f),
			AI<int16_t>(-1600), AF<double>(4.75), AI<int32_t>(9), AF<float>(5.5f), AI<int64_t>(-(1LL << 41)), AF<double>(6.125),
			AI<int32_t>(13), AF<float>(7.5f), AI<int64_t>(15), AF<double>(8.5), AI<uint8_t>(250), AF<float>(9.5f),
			AI<int32_t>(-19), AF<double>(10.5) };
		Ret<double>(c, PassType_Float, [] { return c_if_mix(-1, 1.5f, 1LL << 40, -2.25, -8, 3.5f, -1600, 4.75, 9, 5.5f, -(1LL << 41),
			6.125, 13, 7.5f, 15, 8.5, 250, 9.5f, -19, 10.5); });
		Run(c);
	}
	{ Case c("return float"); c.fn = (void *)c_ret_f; c.args = { AF<float>(-2.5f) }; Ret<float>(c, PassType_Float, [] { return c_ret_f(-2.5f); }); Run(c); }
	{ Case c("return double"); c.fn = (void *)c_ret_d; c.args = { AF<double>(1e300) }; Ret<double>(c, PassType_Float, [] { return c_ret_d(1e300); }); Run(c); }

	/* -------- by reference -------- */
	{
		Case c("by-ref int32/float/double/int64/int8 (written back)");
		c.fn = (void *)c_refs;
		c.args = { ARefI<int32_t>(7), ARefF<float>(1.5f), ARefF<double>(-2.5), ARefI<int64_t>(1LL << 50), ARefI<int8_t>(-3) };
		Void(c, [] { int32_t a = 7; float b = 1.5f; double d = -2.5; int64_t e = 1LL << 50; int8_t f = -3; c_refs(&a, &b, &d, &e, &f); });
		c.post = [](const uint8_t *p, ICallWrapper *w, std::string &why) {
			CHECK_POST(ArgAt<int32_t>(p, w, 0) == 22, "int32 not written back");
			CHECK_POST(ArgAt<float>(p, w, 1) == 5.5f, "float not written back");
			CHECK_POST(ArgAt<double>(p, w, 2) == -6.5, "double not written back");
			CHECK_POST(ArgAt<int64_t>(p, w, 3) == (1LL << 50) * 3 + 1, "int64 not written back");
			CHECK_POST(ArgAt<int8_t>(p, w, 4) == -8, "int8 not written back");
			return true;
		};
		Run(c);
	}
	{
		Case c("by-ref float doesn't consume an xmm register");
		c.fn = (void *)c_fref_mix;
		c.args = { AF<float>(1.25f), ARefF<float>(2.5f), AF<float>(3.75f), ARefF<double>(4.5), AF<double>(5.25) };
		Ret<float>(c, PassType_Float, [] { float b = 2.5f; double d = 4.5; return c_fref_mix(1.25f, &b, 3.75f, &d, 5.25); });
		c.post = [](const uint8_t *p, ICallWrapper *w, std::string &why) {
			CHECK_POST(ArgAt<float>(p, w, 1) == 3.5f, "float not written back");
			CHECK_POST(ArgAt<double>(p, w, 3) == 5.5, "double not written back");
			return true;
		};
		Run(c);
	}
	{
		Case c("by-ref objects (Vector*, 24-byte struct*)");
		c.fn = (void *)c_vec_ref;
		Vec v = { 1, 2, 3 };
		Big b = { { 1, 2, 3, 4, 5, 6 } };
		c.args = { ARefO(v, { FL, FL, FL }), ARefO(b, {}) };
		Void(c, [v, b] { Vec vv = v; Big bb = b; c_vec_ref(&vv, &bb); });
		c.post = [](const uint8_t *p, ICallWrapper *w, std::string &why) {
			Vec v = ArgAt<Vec>(p, w, 0);
			Big b = ArgAt<Big>(p, w, 1);
			CHECK_POST(v.x == 2 && v.y == 3 && v.z == 4, "Vector not written back");
			CHECK_POST(b.a[0] == 2 && b.a[5] == 12, "struct not written back");
			return true;
		};
		Run(c);
	}

	/* -------- objects by value: one per classification -------- */
#define OBJ_CASE(T, desc, val, extra, ...) { \
		T v = val; \
		Case c("object by value: " #T " " desc); \
		c.fn = (void *)c_obj_##T; \
		c.args = { AI<int32_t>(-7), AO(v, { __VA_ARGS__ }, extra), AI<int32_t>(0x1234567) }; \
		Ret<int32_t>(c, PassType_Basic, [v] { return c_obj_##T(-7, v, 0x1234567); }); \
		Run(c); }
	OBJ_CASE(Vec, "(SSE,SSE)", (Vec{ 1.5f, -2.25f, 3.75f }), 0, FL, FL, FL)
	OBJ_CASE(Vec, "(SDKTools flags OCTOR|OASSIGNOP)", (Vec{ -1.5f, 2.25f, -3.75f }), PASSFLAG_OCTOR | PASSFLAG_OASSIGNOP, FL, FL, FL)
	OBJ_CASE(FFI, "(SSE,INT)", (FFI{ 0.5f, -0.25f, -99 }), 0, FL, FL, I32)
	OBJ_CASE(IFs, "(INT merged)", (IFs{ -42, 6.5f }), 0, I32, FL)
	OBJ_CASE(DD, "(SSE,SSE)", (DD{ 1e200, -1e-200 }), 0, DB, DB)
	OBJ_CASE(LD, "(INT,SSE)", (LD{ -(1LL << 60), 2.5 }), 0, I64, DB)
	OBJ_CASE(DL, "(SSE,INT)", (DL{ -3.5, 1LL << 61 }), 0, DB, I64)
	OBJ_CASE(FD, "(SSE,SSE, padded)", (FD{ 7.25f, -8.5 }), 0, FL, DB)
	OBJ_CASE(II, "(INT,INT)", (II{ -1, 0x7FFFFFFFFFFFFFFFLL }), 0, I64, I64)
	OBJ_CASE(S1, "(1 byte)", (S1{ -9 }), 0, I8)
	OBJ_CASE(S2, "(2 bytes)", (S2{ -9999 }), 0, I16)
	OBJ_CASE(S3, "(3 bytes)", (S3{ 1, -2, 3 }), 0, I8, I8, I8)
	OBJ_CASE(S5, "(5 bytes)", (S5{ { 1, -2, 3, -4, 5 } }), 0, I8, I8, I8, I8, I8)
	OBJ_CASE(S6, "(6 bytes)", (S6{ 1000, -2000, 3000 }), 0, I16, I16, I16)
	OBJ_CASE(S7, "(7 bytes)", (S7{ { 1, -2, 3, -4, 5, -6, 7 } }), 0, I8, I8, I8, I8, I8, I8, I8)
	OBJ_CASE(S10, "(INT,INT 2-byte tail)", (S10{ -(1LL << 50), -12345 }), 0, I64, I16)
	OBJ_CASE(S12, "(INT,INT 4-byte tail)", (S12{ -1, 2, -3 }), 0, I32, I32, I32)
	OBJ_CASE(Big, "(24 bytes, MEMORY)", (Big{ { 1, -2, 3, -4, 5, -6 } }), 0, I32, I32, I32, I32, I32, I32)
	OBJ_CASE(Big40, "(40 bytes, MEMORY)", (Big40{ { 1.5, -2.5, 3.5, -4.5, 5.5 } }), 0, DB, DB, DB, DB, DB)
	OBJ_CASE(Pk, "(packed/unaligned, MEMORY)", (Pk{ -3, 0x12345678 }), PASSFLAG_OUNALIGN, I8, I32)
	OBJ_CASE(Pk2, "(packed/unaligned 10 bytes, MEMORY)", (Pk2{ -3, 0x1122334455667788LL, 9 }), PASSFLAG_OUNALIGN, I8, I64, I8)
	OBJ_CASE(NT, "(non-trivial dtor+copy, by invisible ref)", (NT{ 11, -22 }), PASSFLAG_ODTOR | PASSFLAG_OCOPYCTOR | PASSFLAG_OCTOR, I32, I32)
	OBJ_CASE(NT2, "(non-trivial copy ctor only)", (NT2{ -33, 44 }), PASSFLAG_OCOPYCTOR, I32, I32)
	{
		NTBig v;
		v.v[0] = 1.5f; v.v[1] = -2.5f; v.v[2] = 3.5f; v.eh = 0x0BADF00D; v.type = 6;
		Case c("object by value: NTBig variant_t lookalike (24 bytes, ODTOR -> invisible ref)");
		c.fn = (void *)c_obj_NTBig;
		c.args = { AI<int32_t>(-7), AO(v, {}, PASSFLAG_OCTOR | PASSFLAG_ODTOR | PASSFLAG_OASSIGNOP), AI<int32_t>(0x1234567) };
		Ret<int32_t>(c, PassType_Basic, [v] { return c_obj_NTBig(-7, v, 0x1234567); });
		Run(c);
	}

	/* -------- register exhaustion -------- */
	{
		Case c("5 ints + (INT,SSE) struct fits + int");
		c.fn = (void *)c_i5_ld_i;
		LD s = { -5, 6.5 };
		c.args = { AI<int64_t>(1), AI<int64_t>(2), AI<int64_t>(3), AI<int64_t>(4), AI<int64_t>(5), AO(s, { I64, DB }), AI<int64_t>(7) };
		Ret<int64_t>(c, PassType_Basic, [s] { return c_i5_ld_i(1, 2, 3, 4, 5, s, 7); });
		Run(c);
	}
	{
		Case c("6 ints + (INT,SSE) struct -> stack + int on stack");
		c.fn = (void *)c_i6_ld_i;
		LD s = { -5, 6.5 };
		c.args = { AI<int64_t>(1), AI<int64_t>(2), AI<int64_t>(3), AI<int64_t>(4), AI<int64_t>(5), AI<int64_t>(6), AO(s, { I64, DB }), AI<int64_t>(7) };
		Ret<int64_t>(c, PassType_Basic, [s] { return c_i6_ld_i(1, 2, 3, 4, 5, 6, s, 7); });
		Run(c);
	}
	{
		Case c("5 ints + (INT,INT) struct -> stack, next int still gets r9");
		c.fn = (void *)c_i5_ii_i;
		II s = { -5, 6 };
		c.args = { AI<int64_t>(1), AI<int64_t>(2), AI<int64_t>(3), AI<int64_t>(4), AI<int64_t>(5), AO(s, { I64, I64 }), AI<int64_t>(7) };
		Ret<int64_t>(c, PassType_Basic, [s] { return c_i5_ii_i(1, 2, 3, 4, 5, s, 7); });
		Run(c);
	}
	{
		Case c("7 floats + (SSE,SSE) struct -> stack, next float still gets xmm7");
		c.fn = (void *)c_f7_dd_f;
		DD s = { -5.5, 6.5 };
		c.args = { AF<float>(1), AF<float>(2), AF<float>(3), AF<float>(4), AF<float>(5), AF<float>(6), AF<float>(7), AO(s, { DB, DB }), AF<float>(8.5f) };
		Ret<double>(c, PassType_Float, [s] { return c_f7_dd_f(1, 2, 3, 4, 5, 6, 7, s, 8.5f); });
		Run(c);
	}
	{
		Case c("5 Vectors by value (5th -> stack)");
		c.fn = (void *)c_vec5;
		Vec v[5];
		for (int i = 0; i < 5; i++)
			v[i] = { i * 1.5f, -i * 2.5f, i + 0.25f };
		for (int i = 0; i < 5; i++)
			c.args.push_back(AO(v[i], { FL, FL, FL }));
		Void(c, [v] { c_vec5(v[0], v[1], v[2], v[3], v[4]); });
		Run(c);
	}
	{
		Case c("4 Vectors + 2 floats (floats go to stack)");
		c.fn = (void *)c_vec4ff;
		Vec v[4];
		for (int i = 0; i < 4; i++)
			v[i] = { i * 1.5f, -i * 2.5f, i + 0.25f };
		for (int i = 0; i < 4; i++)
			c.args.push_back(AO(v[i], { FL, FL, FL }));
		c.args.push_back(AF<float>(9.5f));
		c.args.push_back(AF<float>(-10.5f));
		Void(c, [v] { c_vec4ff(v[0], v[1], v[2], v[3], 9.5f, -10.5f); });
		Run(c);
	}
	{
		Case c("MEMORY objects interleaved with spilled scalars (stack order)");
		c.fn = (void *)c_mem_mix;
		Big b = { { 1, 2, 3, 4, 5, 6 } };
		Big40 d = { { -1.5, -2.5, -3.5, -4.5, -5.5 } };
		Pk f = { 7, -8 };
		NTBig n;
		n.v[0] = 9; n.v[1] = 10; n.v[2] = 11; n.eh = 12; n.type = 13;
		c.args = { AI<int64_t>(-1), AO(b, {}), AF<double>(2.5), AO(d, {}), AI<int32_t>(-3), AO(f, { I8, I32 }, PASSFLAG_OUNALIGN),
			AI<int64_t>(4), AI<int64_t>(5), AI<int64_t>(6), AI<int64_t>(7), AI<int64_t>(8), AI<int64_t>(9), AF<double>(10.5),
			AO(n, {}, PASSFLAG_OCTOR | PASSFLAG_ODTOR | PASSFLAG_OASSIGNOP), AF<float>(11.5f) };
		Ret<double>(c, PassType_Float, [b, d, f, n] { return c_mem_mix(-1, b, 2.5, d, -3, f, 4, 5, 6, 7, 8, 9, 10.5, n, 11.5f); });
		Run(c);
	}
	{
		Case c("objects at param offsets past 127");
		c.fn = (void *)c_late_objs;
		Big40 a = { { 1, 2, 3, 4, 5 } }, d = { { -1, -2, -3, -4, -5 } };
		Vec v = { 6.5f, 7.5f, 8.5f };
		DD w = { 9.5, -10.5 };
		S6 x = { 11, -12, 13 };
		c.args = { AO(a, {}), AO(a, {}), AO(a, {}), AO(d, {}), AO(v, { FL, FL, FL }), AO(w, { DB, DB }), AO(x, { I16, I16, I16 }), AF<float>(14.5f) };
		Void(c, [a, d, v, w, x] { c_late_objs(a, a, a, d, v, w, x, 14.5f); });
		Run(c);
	}

	/* -------- object returns -------- */
#define RET_CASE(T, desc, extra, ...) { \
		Case c("object return: " #T " " desc); \
		c.fn = (void *)c_ret_##T; \
		c.args = { AI<int32_t>(13) }; \
		Ret<T>(c, PassType_Object, [] { return c_ret_##T(13); }, { __VA_ARGS__ }, extra); \
		Run(c); }
	RET_CASE(Vec, "(xmm0,xmm1)", 0, FL, FL, FL)
	RET_CASE(Vec, "(SDKTools flags)", PASSFLAG_OCTOR | PASSFLAG_OASSIGNOP, FL, FL, FL)
	RET_CASE(FFI, "(xmm0,rax)", 0, FL, FL, I32)
	RET_CASE(IFs, "(rax)", 0, I32, FL)
	RET_CASE(DD, "(xmm0,xmm1)", 0, DB, DB)
	RET_CASE(LD, "(rax,xmm0)", 0, I64, DB)
	RET_CASE(DL, "(xmm0,rax)", 0, DB, I64)
	RET_CASE(FD, "(xmm0,xmm1)", 0, FL, DB)
	RET_CASE(II, "(rax,rdx)", 0, I64, I64)
	RET_CASE(S1, "(1 byte)", 0, I8)
	RET_CASE(S2, "(2 bytes)", 0, I16)
	RET_CASE(S3, "(3 bytes)", 0, I8, I8, I8)
	RET_CASE(S5, "(5 bytes)", 0, I8, I8, I8, I8, I8)
	RET_CASE(S6, "(6 bytes)", 0, I16, I16, I16)
	RET_CASE(S7, "(7 bytes)", 0, I8, I8, I8, I8, I8, I8, I8)
	RET_CASE(S10, "(rax,rdx 2-byte tail)", 0, I64, I16)
	RET_CASE(S12, "(rax,rdx 4-byte tail)", 0, I32, I32, I32)
	RET_CASE(Big, "(hidden pointer)", 0, I32, I32, I32, I32, I32, I32)
	RET_CASE(Big40, "(hidden pointer)", 0, DB, DB, DB, DB, DB)
	RET_CASE(Pk, "(packed, hidden pointer)", PASSFLAG_OUNALIGN, I8, I32)
	RET_CASE(Pk2, "(packed, hidden pointer)", PASSFLAG_OUNALIGN, I8, I64, I8)
	RET_CASE(NT, "(non-trivial, hidden pointer)", PASSFLAG_ODTOR | PASSFLAG_OCOPYCTOR | PASSFLAG_OCTOR, I32, I32)
	RET_CASE(NT2, "(copy ctor only, hidden pointer)", PASSFLAG_OCOPYCTOR, I32, I32)
	RET_CASE(NTBig, "(variant_t lookalike, hidden pointer)", PASSFLAG_OCTOR | PASSFLAG_ODTOR | PASSFLAG_OASSIGNOP)
	{
		Case c("hidden return pointer shifts 6th int arg to the stack");
		c.fn = (void *)c_ret_big_i6;
		c.args = { AI<int64_t>(1), AI<int64_t>(-2), AI<int64_t>(3), AI<int64_t>(-4), AI<int64_t>(5), AI<int64_t>(-6) };
		Ret<Big>(c, PassType_Object, [] { return c_ret_big_i6(1, -2, 3, -4, 5, -6); });
		Run(c);
	}
	{
		Case c("Vector return with int/double/Vector/float args");
		c.fn = (void *)c_ret_vec_args;
		Vec v = { 1, 2, 3 };
		c.args = { AI<int32_t>(4), AF<double>(5.5), AO(v, { FL, FL, FL }), AF<float>(6.5f) };
		Ret<Vec>(c, PassType_Object, [v] { return c_ret_vec_args(4, 5.5, v, 6.5f); }, { FL, FL, FL });
		Run(c);
	}

	/* -------- thiscall (non-virtual) -------- */
	{
		Case c("thiscall: this + int/float/int64");
		c.fn = (void *)c_this; c.cv = CallConv_ThisCall;
		c.args = { AI<int32_t>(-5), AF<float>(2.5f), AI<int64_t>(1LL << 45) };
		Ret<int32_t>(c, PassType_Basic, [] { return c_this(&g_obj, -5, 2.5f, 1LL << 45); });
		Run(c);
	}
	{
		Case c("thiscall: MEMORY return (retbuf in rdi, this in rsi)");
		c.fn = (void *)c_this_retbig; c.cv = CallConv_ThisCall;
		c.args = { AI<int32_t>(77) };
		Ret<Big>(c, PassType_Object, [] { return c_this_retbig(&g_obj, 77); });
		Run(c);
	}
	{
		Case c("thiscall: Vector return");
		c.fn = (void *)c_this_retvec; c.cv = CallConv_ThisCall;
		c.args = { AF<float>(-1.25f) };
		Ret<Vec>(c, PassType_Object, [] { return c_this_retvec(&g_obj, -1.25f); }, { FL, FL, FL });
		Run(c);
	}
	{
		Case c("thiscall: non-trivial return");
		c.fn = (void *)c_this_retnt; c.cv = CallConv_ThisCall;
		c.args = { AI<int32_t>(31) };
		Ret<NT>(c, PassType_Object, [] { return c_this_retnt(&g_obj, 31); }, { I32, I32 }, PASSFLAG_ODTOR | PASSFLAG_OCOPYCTOR);
		Run(c);
	}
	{
		Case c("thiscall: no params, void");
		c.fn = (void *)c_this_void; c.cv = CallConv_ThisCall;
		Void(c, [] { c_this_void(&g_obj); });
		Run(c);
	}

	/* -------- virtual calls -------- */
	static const struct { unsigned int idx, vtblOffs, thisOffs; } vt[] = {
		{ 0, 0, 0 }, { 1, 0, 0 }, { 15, 0, 0 }, { 16, 0, 0 }, { 63, 0, 0 },    // slot disp8/disp32 boundary
		{ 2, 8, 0 }, { 2, 0, 16 }, { 2, 0, 136 }, { 2, 64, 100 },              // offsets, incl. past 127
	};
	for (auto &t : vt)
	{
		char n[128];
		snprintf(n, sizeof(n), "vcall: slot %u, vtblOffs %u, thisOffs %u", t.idx, t.vtblOffs, t.thisOffs);
		Case c(n);
		c.fn = (void *)c_this; c.vcall = true; c.vidx = t.idx; c.vtblOffs = t.vtblOffs; c.thisOffs = t.thisOffs;
		c.args = { AI<int32_t>(t.idx), AF<float>(0.5f), AI<int64_t>(-1) };
		Ret<int32_t>(c, PassType_Basic, [t] { return c_this(&g_obj, t.idx, 0.5f, -1); });
		Run(c);
	}
	{
		Case c("vcall: AcceptInput shape (const char*, 2 ptrs, variant_t by value, int) -> bool");
		c.fn = (void *)c_accept_input; c.vcall = true; c.vidx = 40;
		static const char *kInput = "SetHealth";
		NTBig v;
		v.i = 0; v.v[0] = 0; v.v[1] = 0; v.v[2] = 0;
		v.i = 175; v.eh = -1; v.type = 5;
		void *act = (void *)0x1000, *call = (void *)0x2000;
		c.args = { AI<const char *>(kInput), AI<void *>(act), AI<void *>(call),
			AO(v, {}, PASSFLAG_OCTOR | PASSFLAG_ODTOR | PASSFLAG_OASSIGNOP), AI<int32_t>(7) };
		Ret<bool>(c, PassType_Basic, [v, act, call] { return c_accept_input(&g_obj, kInput, act, call, v, 7); });
		Run(c);
	}
	{
		Case c("vcall: MEMORY return");
		c.fn = (void *)c_this_retbig; c.vcall = true; c.vidx = 20;
		c.args = { AI<int32_t>(-77) };
		Ret<Big>(c, PassType_Object, [] { return c_this_retbig(&g_obj, -77); });
		Run(c);
	}
	{
		Case c("vcall: non-trivial return");
		c.fn = (void *)c_this_retnt; c.vcall = true; c.vidx = 3;
		c.args = { AI<int32_t>(-31) };
		Ret<NT>(c, PassType_Object, [] { return c_this_retnt(&g_obj, -31); }, { I32, I32 }, PASSFLAG_ODTOR | PASSFLAG_OCOPYCTOR);
		Run(c);
	}
	{
		Case c("vcall: Vector return");
		c.fn = (void *)c_this_retvec; c.vcall = true; c.vidx = 5;
		c.args = { AF<float>(3.25f) };
		Ret<Vec>(c, PassType_Object, [] { return c_this_retvec(&g_obj, 3.25f); }, { FL, FL, FL });
		Run(c);
	}

	/* -------- varargs -------- */
	{
		Case c("varargs: 10 doubles (al, 2 on stack)");
		c.fn = (void *)c_va_d; c.fnflags = FNFLAG_VARARGS;
		c.args = { AI<int32_t>(10) };
		for (int i = 0; i < 10; i++)
			c.args.push_back(AF<double>(i * 1.25 - 3));
		Ret<int64_t>(c, PassType_Basic, [] { return c_va_d(10, -3.0, -1.75, -0.5, 0.75, 2.0, 3.25, 4.5, 5.75, 7.0, 8.25); });
		Run(c);
	}
	{
		Case c("varargs: ints and doubles mixed");
		c.fn = (void *)c_va_mix; c.fnflags = FNFLAG_VARARGS;
		static const char *fmt = "idlidldidl";
		c.args = { AI<const char *>(fmt), AI<int32_t>(-1), AF<double>(2.5), AI<int64_t>(1LL << 40), AI<int32_t>(4), AF<double>(-5.5),
			AI<int64_t>(-6), AF<double>(7.5), AI<int32_t>(8), AF<double>(9.5), AI<int64_t>(10) };
		Ret<double>(c, PassType_Float, [] { return c_va_mix(fmt, -1, 2.5, 1LL << 40, 4, -5.5, (int64_t)-6, 7.5, 8, 9.5, (int64_t)10); });
		Run(c);
	}

	/* -------- void / no params -------- */
	{ Case c("void, no params"); c.fn = (void *)c_void0; Void(c, [] { c_void0(); }); Run(c); }
	{
		Case c("void with params");
		c.fn = (void *)c_void_args;
		Vec v = { 1, 2, 3 };
		c.args = { AI<int32_t>(5), AF<double>(6.5), AO(v, { FL, FL, FL }) };
		Void(c, [v] { c_void_args(5, 6.5, v); });
		Run(c);
	}
	{ Case c("no params, int return"); c.fn = (void *)c_noargs; Ret<int32_t>(c, PassType_Basic, [] { return c_noargs(); }); Run(c); }

	RemoveFaultHandlers();
	if (passed)
		*passed = g_pass;
	return g_fail;
}
