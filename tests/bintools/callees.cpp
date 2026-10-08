// Test callees. Built with g++ -fno-omit-frame-pointer so ENTER() can check
// the stack alignment at entry. Every callee logs the values it received.
#include <cstring>
#include <cstdarg>
#include "bttest.h"

BtRec bt_rec = {};

#define NI extern "C" __attribute__((noinline, used))
// With a frame pointer, rbp == entry rsp - 8, which is 16-byte aligned iff the
// caller had rsp 16-byte aligned at the call instruction.
#define ENTER() do { \
	if ((uintptr_t)__builtin_frame_address(0) & 15) bt_rec.misaligned++; \
	bt_rec.calls++; } while (0)

template <typename T> static inline void L(const T &v)
{
	if (bt_rec.n + sizeof(T) <= sizeof(bt_rec.log))
	{
		memcpy(bt_rec.log + bt_rec.n, &v, sizeof(T));
		bt_rec.n += sizeof(T);
	}
}
static inline void LB(const void *p, size_t n)
{
	if (bt_rec.n + n <= sizeof(bt_rec.log))
	{
		memcpy(bt_rec.log + bt_rec.n, p, n);
		bt_rec.n += n;
	}
}

// Field-wise loggers (padding bytes are unspecified, so never log whole structs)
static void LV(const Vec &v) { L(v.x); L(v.y); L(v.z); }
static void LV(const FFI &v) { L(v.x); L(v.y); L(v.z); }
static void LV(const IFs &v) { L(v.a); L(v.b); }
static void LV(const DD &v) { L(v.a); L(v.b); }
static void LV(const LD &v) { L(v.a); L(v.b); }
static void LV(const DL &v) { L(v.a); L(v.b); }
static void LV(const FD &v) { L(v.a); L(v.b); }
static void LV(const II &v) { L(v.a); L(v.b); }
static void LV(const S1 &v) { L(v.a); }
static void LV(const S2 &v) { L(v.a); }
static void LV(const S3 &v) { L(v.a); L(v.b); L(v.c); }
static void LV(const S5 &v) { LB(v.a, 5); }
static void LV(const S6 &v) { L(v.a); L(v.b); L(v.c); }
static void LV(const S7 &v) { LB(v.a, 7); }
static void LV(const S10 &v) { L(v.a); L(v.b); }
static void LV(const S12 &v) { L(v.a); L(v.b); L(v.c); }
static void LV(const Big &v) { LB(v.a, sizeof(v.a)); }
static void LV(const Big40 &v) { LB(v.d, sizeof(v.d)); }
static void LV(const Pk &v) { int8_t a = v.a; int32_t b = v.b; L(a); L(b); }
static void LV(const Pk2 &v) { int8_t a = v.a, c = v.c; int64_t b = v.b; L(a); L(b); L(c); }
static void LV(const NT &v) { L(v.a); L(v.b); }
static void LV(const NT2 &v) { L(v.a); L(v.b); }
static void LV(const NTBig &v) { LB(v.v, sizeof(v.v)); L(v.eh); L(v.type); }

NT::NT(const NT &o) : a(o.a), b(o.b) { asm volatile("" ::: "memory"); }
NT::~NT() { asm volatile("" ::: "memory"); }
NT2::NT2(const NT2 &o) : a(o.a), b(o.b) { asm volatile("" ::: "memory"); }
NTBig::~NTBig() { asm volatile("" ::: "memory"); }

/* ---------------- integers ---------------- */
NI int64_t c_ints8(int8_t a, uint8_t b, int16_t c, uint16_t d, int32_t e, uint32_t f, int64_t g, bool h)
{ ENTER(); L(a); L(b); L(c); L(d); L(e); L(f); L(g); L(h); return a + b + c + d + e + (int64_t)f + g + h; }

NI int64_t c_ints24(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, int64_t a7,
	int64_t a8, int64_t a9, int64_t a10, int64_t a11, int64_t a12, int64_t a13, int64_t a14, int64_t a15,
	int64_t a16, int64_t a17, int64_t a18, int64_t a19, int64_t a20, int64_t a21, int64_t a22, int64_t a23)
{
	ENTER();
	int64_t a[24] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20,a21,a22,a23};
	int64_t s = 0;
	for (int i = 0; i < 24; i++) { L(a[i]); s += a[i] * (i + 1); }
	return s;
}

// narrow integers that land on the stack
NI int32_t c_narrow_stack(int64_t r0, int64_t r1, int64_t r2, int64_t r3, int64_t r4, int64_t r5,
	int8_t s0, uint8_t s1, int16_t s2, uint16_t s3, int32_t s4, uint32_t s5, bool s6, int64_t s7)
{ ENTER(); L(r0); L(r5); L(s0); L(s1); L(s2); L(s3); L(s4); L(s5); L(s6); L(s7); return s0 + s2 + s4; }

NI int8_t c_ret_i8(int32_t x) { ENTER(); L(x); return (int8_t)x; }
NI uint8_t c_ret_u8(int32_t x) { ENTER(); L(x); return (uint8_t)x; }
NI int16_t c_ret_i16(int32_t x) { ENTER(); L(x); return (int16_t)x; }
NI uint16_t c_ret_u16(int32_t x) { ENTER(); L(x); return (uint16_t)x; }
NI int32_t c_ret_i32(int32_t x) { ENTER(); L(x); return x * 3; }
NI int64_t c_ret_i64(int64_t x) { ENTER(); L(x); return x * 3 + 0x100000000LL; }
NI bool c_ret_bool(int32_t x) { ENTER(); L(x); return x & 1; }
NI void *c_ret_ptr(void *x) { ENTER(); L(x); return (char *)x + 1; }
NI __int128 c_i128(int64_t a, __int128 b, int64_t c)
{ ENTER(); L(a); L(b); L(c); return b * 3 + a; }
NI int64_t c_i128_stack(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, __int128 b, int64_t c)
{ ENTER(); L(a0); L(a4); L(b); L(c); return (int64_t)(b >> 64) + c; }

/* ---------------- floating point ---------------- */
NI float c_f10(float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8, float a9)
{ ENTER(); float a[10] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9}; float s = 0; for (int i = 0; i < 10; i++) { L(a[i]); s += a[i] * (i + 1); } return s; }

NI double c_d10(double a0, double a1, double a2, double a3, double a4, double a5, double a6, double a7, double a8, double a9)
{ ENTER(); double a[10] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9}; double s = 0; for (int i = 0; i < 10; i++) { L(a[i]); s += a[i] * (i + 1); } return s; }

NI double c_d30(double a0, double a1, double a2, double a3, double a4, double a5, double a6, double a7, double a8, double a9,
	double a10, double a11, double a12, double a13, double a14, double a15, double a16, double a17, double a18, double a19,
	double a20, double a21, double a22, double a23, double a24, double a25, double a26, double a27, double a28, double a29)
{
	ENTER();
	double a[30] = {a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,a20,a21,a22,a23,a24,a25,a26,a27,a28,a29};
	double s = 0;
	for (int i = 0; i < 30; i++) { L(a[i]); s += a[i] * (i + 1); }
	return s;
}

NI double c_fd_mix(float a, double b, float c, double d, float e, double f, float g, double h, float i, double j, float k, double l)
{ ENTER(); L(a); L(b); L(c); L(d); L(e); L(f); L(g); L(h); L(i); L(j); L(k); L(l); return a + b + c + d + e + f + g + h + i + j + k + l; }

// ints and floats interleaved; both register files overflow onto the stack
NI double c_if_mix(int32_t a, float b, int64_t c, double d, int8_t e, float f, int16_t g, double h, int32_t i, float j,
	int64_t k, double l, int32_t m, float n, int64_t o, double p, uint8_t q, float r, int32_t s, double t)
{
	ENTER();
	L(a); L(b); L(c); L(d); L(e); L(f); L(g); L(h); L(i); L(j); L(k); L(l); L(m); L(n); L(o); L(p); L(q); L(r); L(s); L(t);
	return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s + t;
}

NI float c_ret_f(float x) { ENTER(); L(x); return x * 2.0f + 1.0f; }
NI double c_ret_d(double x) { ENTER(); L(x); return x * 2.0 + 1.0; }

/* ---------------- by reference ---------------- */
NI void c_refs(int32_t *a, float *b, double *c, int64_t *d, int8_t *e)
{ ENTER(); L(*a); L(*b); L(*c); L(*d); L(*e); *a = *a * 3 + 1; *b = *b * 3 + 1; *c = *c * 3 + 1; *d = *d * 3 + 1; *e = *e * 3 + 1; }

// a by-ref float must not use up an xmm register
NI float c_fref_mix(float a, float *b, float c, double *d, double e)
{ ENTER(); L(a); L(*b); L(c); L(*d); L(e); *b += 1.0f; *d += 1.0; return a + c + (float)e; }

NI void c_vec_ref(Vec *v, Big *b)
{ ENTER(); LV(*v); LV(*b); v->x += 1; v->y += 1; v->z += 1; for (int i = 0; i < 6; i++) b->a[i] *= 2; }

/* ---------------- objects by value, register classes ---------------- */
#define OBJ1(T) NI int32_t c_obj_##T(int32_t pre, T v, int32_t post) { ENTER(); L(pre); LV(v); L(post); return pre + post; }
OBJ1(Vec) OBJ1(FFI) OBJ1(IFs) OBJ1(DD) OBJ1(LD) OBJ1(DL) OBJ1(FD) OBJ1(II)
OBJ1(S1) OBJ1(S2) OBJ1(S3) OBJ1(S5) OBJ1(S6) OBJ1(S7) OBJ1(S10) OBJ1(S12)
OBJ1(Big) OBJ1(Big40) OBJ1(Pk) OBJ1(Pk2) OBJ1(NT) OBJ1(NT2) OBJ1(NTBig)

// register exhaustion: an object that doesn't fit goes to the stack whole,
// and later scalars still get the registers that are left
NI int64_t c_i5_ld_i(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, LD s, int64_t z)
{ ENTER(); L(a); L(e); LV(s); L(z); return a + z; }
NI int64_t c_i6_ld_i(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, LD s, int64_t z)
{ ENTER(); L(a); L(f); LV(s); L(z); return a + z; }
NI int64_t c_i5_ii_i(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, II s, int64_t z)
{ ENTER(); L(a); L(e); LV(s); L(z); return a + z; }
NI double c_f7_dd_f(float a, float b, float c, float d, float e, float f, float g, DD s, float z)
{ ENTER(); L(a); L(g); LV(s); L(z); return a + z; }
NI void c_vec5(Vec a, Vec b, Vec c, Vec d, Vec e)
{ ENTER(); LV(a); LV(b); LV(c); LV(d); LV(e); }
NI void c_vec4ff(Vec a, Vec b, Vec c, Vec d, float e, float f)
{ ENTER(); LV(a); LV(b); LV(c); LV(d); L(e); L(f); }

// memory objects interleaved with spilled scalars: stack order must follow param order
NI double c_mem_mix(int64_t a, Big b, double c, Big40 d, int32_t e, Pk f, int64_t g, int64_t h, int64_t i,
	int64_t j, int64_t k, int64_t l, double m, NTBig n, float o)
{ ENTER(); L(a); LV(b); L(c); LV(d); L(e); LV(f); L(g); L(h); L(i); L(j); L(k); L(l); L(m); LV(n); L(o); return a + c + m + o; }

// objects whose param-buffer offset is past 127 (disp32 loads)
NI void c_late_objs(Big40 a, Big40 b, Big40 c, Big40 d, Vec v, DD w, S6 x, float y)
{ ENTER(); LV(a); LV(d); LV(v); LV(w); LV(x); L(y); }

/* ---------------- object returns ---------------- */
#define RET1(T, ...) NI T c_ret_##T(int32_t k) { ENTER(); L(k); T r = __VA_ARGS__; return r; }
RET1(Vec, { k + 0.5f, k + 1.5f, k + 2.5f })
RET1(FFI, { k + 0.25f, k * 2.0f, k * 3 })
RET1(IFs, { k * 5, k + 0.75f })
RET1(DD, { k + 0.125, k * 4.0 })
RET1(LD, { (int64_t)k << 33, k + 0.5 })
RET1(DL, { k - 0.5, -(int64_t)k << 31 })
RET1(FD, { k + 0.5f, k * 1.25 })
RET1(II, { (int64_t)k << 40 | 7, -(int64_t)k })
RET1(S1, { (int8_t)(k + 1) })
RET1(S2, { (int16_t)(k * 300) })
RET1(S3, { (int8_t)k, (int8_t)(k + 1), (int8_t)(k + 2) })
RET1(S5, { { (int8_t)k, 2, 3, 4, (int8_t)(k + 5) } })
RET1(S6, { (int16_t)k, (int16_t)(k * 2), (int16_t)(k * 3) })
RET1(S7, { { (int8_t)k, 2, 3, 4, 5, 6, (int8_t)(k + 7) } })
RET1(S10, { (int64_t)k << 34, (int16_t)(k * 7) })
RET1(S12, { k, k * 2, k * 3 })
RET1(Big, { { k, k + 1, k + 2, k + 3, k + 4, k + 5 } })
RET1(Big40, { { k + 0.5, k + 1.5, k + 2.5, k + 3.5, k + 4.5 } })
RET1(Pk, { (int8_t)k, k * 1000 })
RET1(Pk2, { (int8_t)k, (int64_t)k << 36, (int8_t)(k + 1) })
NI NT c_ret_NT(int32_t k) { ENTER(); L(k); return NT(k, k * 2); }
NI NT2 c_ret_NT2(int32_t k) { ENTER(); L(k); return NT2(k, k * 3); }
NI NTBig c_ret_NTBig(int32_t k) { ENTER(); L(k); NTBig r; r.v[0] = k; r.v[1] = k + 1; r.v[2] = k + 2; r.eh = k * 7; r.type = 5; return r; }

// a hidden return pointer takes rdi, so the 6th integer argument spills
NI Big c_ret_big_i6(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f)
{ ENTER(); L(a); L(b); L(c); L(d); L(e); L(f); Big r = { { (int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d, (int32_t)e, (int32_t)f } }; return r; }
NI Vec c_ret_vec_args(int32_t a, double b, Vec c, float d)
{ ENTER(); L(a); L(b); LV(c); L(d); Vec r = { c.x + a, c.y + (float)b, c.z + d }; return r; }

/* ---------------- this-call and vtables ---------------- */
// Free functions taking the object first have the same ABI as member functions.
NI int32_t c_this(VObj *self, int32_t a, float b, int64_t c)
{ ENTER(); bt_rec.self = self; L(a); L(b); L(c); return a * 2; }
NI Big c_this_retbig(VObj *self, int32_t a)
{ ENTER(); bt_rec.self = self; L(a); Big r = { { a, a, a, a, a, a + 1 } }; return r; }
NI Vec c_this_retvec(VObj *self, float a)
{ ENTER(); bt_rec.self = self; L(a); Vec r = { a, a * 2, a * 3 }; return r; }
NI NT c_this_retnt(VObj *self, int32_t a)
{ ENTER(); bt_rec.self = self; L(a); return NT(a, -a); }
NI void c_this_void(VObj *self)
{ ENTER(); bt_rec.self = self; }
// CBaseEntity::AcceptInput(const char *, CBaseEntity *, CBaseEntity *, variant_t, int)
NI bool c_accept_input(VObj *self, const char *name, void *activator, void *caller, NTBig value, int32_t outputID)
{
	ENTER(); bt_rec.self = self;
	LB(name, strlen(name) + 1); L(activator); L(caller); LV(value); L(outputID);
	return outputID == 7;
}
NI void c_wrong_slot(VObj *self) { bt_rec.wrongslot++; bt_rec.self = self; }

/* ---------------- varargs ---------------- */
NI int64_t c_va_d(int32_t n, ...)
{
	ENTER(); L(n);
	va_list ap; va_start(ap, n);
	for (int i = 0; i < n; i++) { double d = va_arg(ap, double); L(d); }
	va_end(ap);
	return 0x1122334455667788LL;
}
// format: i = int32, l = int64, d = double
NI double c_va_mix(const char *fmt, ...)
{
	ENTER(); LB(fmt, strlen(fmt) + 1);
	va_list ap; va_start(ap, fmt);
	double s = 0;
	for (const char *p = fmt; *p; p++)
	{
		if (*p == 'i') { int32_t v = va_arg(ap, int32_t); L(v); s += v; }
		else if (*p == 'l') { int64_t v = va_arg(ap, int64_t); L(v); s += v; }
		else { double v = va_arg(ap, double); L(v); s += v; }
	}
	va_end(ap);
	return s;
}

/* ---------------- void / no params ---------------- */
NI void c_void0() { ENTER(); L((int32_t)0x5A5A); }
NI void c_void_args(int32_t a, double b, Vec c) { ENTER(); L(a); L(b); LV(c); }
NI int32_t c_noargs() { ENTER(); return 0x7654321; }

/* ---------------- SDKCall targets (plugin side) ----------------
 * SDKTools types: int (POD), float, bool, Vector/QAngle, CBaseEntity*,
 * edict_t*, const char*, void* (Address). Results are read back through the
 * log via natives, so these just record. */
NI int32_t s_pod(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f, int32_t g, int32_t h)
{ ENTER(); L(a); L(b); L(c); L(d); L(e); L(f); L(g); L(h); return a * 1000 + h; }
NI float s_float(float a, float b, float c, float d, float e, float f, float g, float h, float i, float j)
{ ENTER(); L(a); L(b); L(c); L(d); L(e); L(f); L(g); L(h); L(i); L(j); return a + j * 100.0f; }
NI bool s_bool(bool a, bool b, int32_t c, bool d)
{ ENTER(); L(a); L(b); L(c); L(d); return !a; }
NI int32_t s_ptrs(int32_t *a, float *b, bool *c, Vec *d, Vec *e)
{ ENTER(); L(*a); L(*b); L(*c); LV(*d); LV(*e); *a += 5; *b *= 2; *c = !*c; d->x += 1; d->y += 2; d->z += 3; e->z = -e->z; return 1; }
NI int32_t s_vecs(Vec a, float b, Vec c, int32_t d, Vec e)
{ ENTER(); LV(a); L(b); LV(c); L(d); LV(e); return 2; }
// 7 floats use xmm0-6; the Vector needs 2 more, so it goes on the stack
NI int32_t s_vec_spill(float a, float b, float c, float d, float e, float f, float g, Vec h, float i)
{ ENTER(); L(a); L(g); LV(h); L(i); return 3; }
NI int32_t s_ents(void *ent, void *player, void *edict, const char *str, void *addr)
{ ENTER(); L(ent); L(player); L(edict); LB(str, strlen(str) + 1); L(addr); return 4; }
NI void *s_addr_ptr(void **pp) { ENTER(); L(*pp); void *r = *pp; *pp = (char *)*pp + 16; return r; }
NI const char *s_ret_str(int32_t k) { ENTER(); L(k); static char buf[64]; strcpy(buf, k ? "bintools-test-string" : ""); return buf; }
NI Vec s_ret_vec(float k) { ENTER(); L(k); Vec r = { k, k + 1, k + 2 }; return r; }
static Vec s_vec_storage;
NI Vec *s_ret_vecptr(float k) { ENTER(); L(k); s_vec_storage = { k * 2, k * 3, k * 4 }; return &s_vec_storage; }
NI void *s_ret_ptr(void *p) { ENTER(); L(p); return p; }
static int32_t s_int_storage; static float s_float_storage; static bool s_bool_storage;
NI int32_t *s_ret_intptr(int32_t k) { ENTER(); s_int_storage = k * 7; return &s_int_storage; }
NI float *s_ret_floatptr(float k) { ENTER(); s_float_storage = k * 7; return &s_float_storage; }
NI bool *s_ret_boolptr(int32_t k) { ENTER(); s_bool_storage = k != 0; return &s_bool_storage; }
NI float s_ret_float(float k) { ENTER(); L(k); return k * -3.0f; }
NI bool s_ret_bool(int32_t k) { ENTER(); return k == 42; }
// thiscall targets for SDKCall_Entity/Player/Raw/GameRules/...
NI int32_t s_this(void *self, int32_t a, float b)
{ ENTER(); bt_rec.self = self; L(a); L(b); return a + 1; }
NI Vec s_this_retvec(void *self, float a)
{ ENTER(); bt_rec.self = self; L(a); Vec r = { a, -a, a * 10 }; return r; }
// many mixed params through the SDKTools encoder
NI float s_many(int32_t a, float b, Vec c, int32_t d, float e, bool f, int32_t g, float h, Vec i, int32_t j,
	float k, int32_t l, float m, Vec n, int32_t o, float p, int32_t q, float r, int32_t s, float t)
{
	ENTER(); L(a); L(b); LV(c); L(d); L(e); L(f); L(g); L(h); LV(i); L(j); L(k); L(l); L(m); LV(n); L(o); L(p); L(q); L(r); L(s); L(t);
	return b + t;
}

extern "C" const BtFunc bt_funcs[] = {
#define F(n) { #n, (void *)n },
	F(s_pod) F(s_float) F(s_bool) F(s_ptrs) F(s_vecs) F(s_vec_spill) F(s_ents) F(s_addr_ptr) F(s_ret_str)
	F(s_ret_vec) F(s_ret_vecptr) F(s_ret_ptr) F(s_ret_intptr) F(s_ret_floatptr) F(s_ret_boolptr)
	F(s_ret_float) F(s_ret_bool) F(s_this) F(s_this_retvec) F(s_many) F(c_wrong_slot)
#undef F
	{ nullptr, nullptr }
};
