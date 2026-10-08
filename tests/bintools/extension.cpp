// BinTools x86_64 test suite: SourceMod glue and the natives used by
// bintools_test.smx. The direct IBinTools suite itself is in direct.cpp.
//
// The plugin side (bintools_test.smx) drives SDKCall against the s_* callees
// through the natives below, and exercises the SDKTools/SDKHooks/TF2 natives
// that call into bintools internally.
#include "smsdk_ext.h"
#include <IBinTools.h>
#include <ISDKTools.h>
#include <cstdarg>
#include <cstring>
#include "direct.h"
#include "callees.h"

using namespace SourceMod;

class BtTest : public SDKExtension
{
public:
	bool SDK_OnLoad(char *error, size_t maxlength, bool late) override;
	void SDK_OnAllLoaded() override;
	bool QueryRunning(char *error, size_t maxlength) override;
};

BtTest g_BtTest;
SMEXT_LINK(&g_BtTest);

static ISDKTools *g_pSDKTools = nullptr;

void bt_print(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	rootconsole->ConsolePrint("%s", buf);
}

/******************************************************************************
 * Natives for the plugin side
 ******************************************************************************/

static size_t g_cursor = 0;

static void WriteAddr(IPluginContext *ctx, cell_t param, void *p)
{
	cell_t *addr;
	ctx->LocalToPhysAddr(param, &addr);
	*reinterpret_cast<int64_t *>(addr) = (int64_t)(uintptr_t)p;
}

// native int BTTest_RunDirect(bool verbose = false);
static cell_t Native_RunDirect(IPluginContext *ctx, const cell_t *params)
{
	int passed;
	int failed = BtRunDirect(params[1] != 0, &passed);
	rootconsole->ConsolePrint("[bttest] direct: %d passed, %d failed", passed, failed);
	return failed;
}
// native bool BTTest_GetFunc(const char[] name, Address &addr);
static cell_t Native_GetFunc(IPluginContext *ctx, const cell_t *params)
{
	char *name;
	ctx->LocalToString(params[1], &name);
	for (const BtFunc *f = bt_funcs; f->name; f++)
	{
		if (!strcmp(f->name, name))
		{
			WriteAddr(ctx, params[2], f->fn);
			return 1;
		}
	}
	return ctx->ThrowNativeError("no test function '%s'", name);
}
// native void BTTest_GetObject(int vtblIdx, const char[] fn, Address &obj);
static cell_t Native_GetObject(IPluginContext *ctx, const cell_t *params)
{
	char *name;
	ctx->LocalToString(params[2], &name);
	unsigned int idx = params[1];
	if (idx >= 64)
		return ctx->ThrowNativeError("vtable index %u out of range", idx);
	void *fn = nullptr;
	for (const BtFunc *f = bt_funcs; f->name; f++)
		if (!strcmp(f->name, name))
			fn = f->fn;
	if (!fn)
		return ctx->ThrowNativeError("no test function '%s'", name);
	for (int i = 0; i < 64; i++)
		g_vtable[i] = (void *)c_wrong_slot;
	g_vtable[idx] = fn;
	memset(&g_obj, 0, sizeof(g_obj));
	*(void ***)g_obj.pad = g_vtable;
	WriteAddr(ctx, params[3], &g_obj);
	return 0;
}
static cell_t Native_ResetLog(IPluginContext *ctx, const cell_t *params)
{
	memset(&bt_rec, 0, sizeof(bt_rec));
	g_cursor = 0;
	return 0;
}
static bool Take(IPluginContext *ctx, void *out, size_t n)
{
	if (g_cursor + n > bt_rec.n)
	{
		ctx->ReportError("log exhausted: wanted %zu bytes at %zu, have %zu", n, g_cursor, bt_rec.n);
		return false;
	}
	memcpy(out, bt_rec.log + g_cursor, n);
	g_cursor += n;
	return true;
}
// native int BTTest_NextInt(int size);   sign-extends 1/2/4-byte values
static cell_t Native_NextInt(IPluginContext *ctx, const cell_t *params)
{
	switch (params[1])
	{
		case 1: { int8_t v; if (!Take(ctx, &v, 1)) return 0; return v; }
		case 2: { int16_t v; if (!Take(ctx, &v, 2)) return 0; return v; }
		case 4: { int32_t v; if (!Take(ctx, &v, 4)) return 0; return v; }
	}
	return ctx->ThrowNativeError("bad size %d", params[1]);
}
// native float BTTest_NextFloat();
static cell_t Native_NextFloat(IPluginContext *ctx, const cell_t *params)
{
	float v = 0;
	Take(ctx, &v, 4);
	return sp_ftoc(v);
}
// native void BTTest_NextAddr(Address &addr);
static cell_t Native_NextAddr(IPluginContext *ctx, const cell_t *params)
{
	void *p = nullptr;
	Take(ctx, &p, sizeof(p));
	WriteAddr(ctx, params[1], p);
	return 0;
}
// native void BTTest_NextString(char[] buf, int maxlen);
static cell_t Native_NextString(IPluginContext *ctx, const cell_t *params)
{
	const char *s = (const char *)bt_rec.log + g_cursor;
	size_t len = strnlen(s, bt_rec.n - g_cursor);
	ctx->StringToLocal(params[1], params[2], s);
	g_cursor += len + 1;
	return 0;
}
// native int BTTest_LogRemaining();
static cell_t Native_LogRemaining(IPluginContext *ctx, const cell_t *params) { return (cell_t)(bt_rec.n - g_cursor); }
// native int BTTest_Calls();  native int BTTest_Misaligned();  native int BTTest_WrongSlot();
static cell_t Native_Calls(IPluginContext *ctx, const cell_t *params) { return bt_rec.calls; }
static cell_t Native_Misaligned(IPluginContext *ctx, const cell_t *params) { return bt_rec.misaligned; }
static cell_t Native_WrongSlot(IPluginContext *ctx, const cell_t *params) { return bt_rec.wrongslot; }
// native void BTTest_Self(Address &addr);
static cell_t Native_Self(IPluginContext *ctx, const cell_t *params) { WriteAddr(ctx, params[1], bt_rec.self); return 0; }
// native void BTTest_EdictAddr(int index, Address &addr);
static cell_t Native_EdictAddr(IPluginContext *ctx, const cell_t *params)
{
	WriteAddr(ctx, params[2], gamehelpers->EdictOfIndex(params[1]));
	return 0;
}
// native void BTTest_GameRules(Address &addr);  native void BTTest_IServer(Address &addr);
static cell_t Native_GameRules(IPluginContext *ctx, const cell_t *params)
{
	WriteAddr(ctx, params[1], g_pSDKTools ? g_pSDKTools->GetGameRules() : nullptr);
	return 0;
}
static cell_t Native_IServer(IPluginContext *ctx, const cell_t *params)
{
	WriteAddr(ctx, params[1], g_pSDKTools ? (void *)g_pSDKTools->GetIServer() : nullptr);
	return 0;
}
// native bool BTTest_AddrEq(Address a, Address b);  (int64 compare helper)
static cell_t Native_AddrEq(IPluginContext *ctx, const cell_t *params)
{
	cell_t *a, *b;
	ctx->LocalToPhysAddr(params[1], &a);
	ctx->LocalToPhysAddr(params[2], &b);
	return *reinterpret_cast<int64_t *>(a) == *reinterpret_cast<int64_t *>(b);
}

static const sp_nativeinfo_t g_Natives[] = {
	{ "BTTest_RunDirect", Native_RunDirect },
	{ "BTTest_GetFunc", Native_GetFunc },
	{ "BTTest_GetObject", Native_GetObject },
	{ "BTTest_ResetLog", Native_ResetLog },
	{ "BTTest_NextInt", Native_NextInt },
	{ "BTTest_NextFloat", Native_NextFloat },
	{ "BTTest_NextAddr", Native_NextAddr },
	{ "BTTest_NextString", Native_NextString },
	{ "BTTest_LogRemaining", Native_LogRemaining },
	{ "BTTest_Calls", Native_Calls },
	{ "BTTest_Misaligned", Native_Misaligned },
	{ "BTTest_WrongSlot", Native_WrongSlot },
	{ "BTTest_Self", Native_Self },
	{ "BTTest_EdictAddr", Native_EdictAddr },
	{ "BTTest_GameRules", Native_GameRules },
	{ "BTTest_IServer", Native_IServer },
	{ "BTTest_AddrEq", Native_AddrEq },
	{ nullptr, nullptr },
};

bool BtTest::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	sharesys->AddDependency(myself, "bintools.ext", true, true);
	sharesys->AddNatives(myself, g_Natives);
	return true;
}

void BtTest::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(BINTOOLS, g_pBinTools);
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
}

bool BtTest::QueryRunning(char *error, size_t maxlength)
{
	SM_CHECK_IFACE(BINTOOLS, g_pBinTools);
	return true;
}
