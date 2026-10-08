// Shared between the callees (built with g++, like TF2's own binaries, so the
// compiler decides the ABI) and the test driver. x86_64 SysV only.
#pragma once
#include <cstdint>
#include <cstddef>

struct Vec { float x, y, z; };                   // SSE, SSE
struct FFI { float x, y; int32_t z; };           // SSE, INTEGER
struct IFs { int32_t a; float b; };              // INTEGER (merged)
struct DD { double a, b; };                      // SSE, SSE
struct LD { int64_t a; double b; };              // INTEGER, SSE
struct DL { double a; int64_t b; };              // SSE, INTEGER
struct FD { float a; double b; };                // SSE, SSE (padding in word 0)
struct II { int64_t a, b; };                     // INTEGER, INTEGER
struct S1 { int8_t a; };
struct S2 { int16_t a; };
struct S3 { int8_t a, b, c; };
struct S5 { int8_t a[5]; };
struct S6 { int16_t a, b, c; };
struct S7 { int8_t a[7]; };
struct S10 { int64_t a; int16_t b; };            // INTEGER, INTEGER (2-byte tail)
struct S12 { int32_t a, b, c; };                 // INTEGER, INTEGER (4-byte tail)
struct Big { int32_t a[6]; };                    // 24: MEMORY
struct Big40 { double d[5]; };                   // 40: MEMORY
struct __attribute__((packed)) Pk { int8_t a; int32_t b; };          // unaligned: MEMORY
struct __attribute__((packed)) Pk2 { int8_t a; int64_t b; int8_t c; };

// Non-trivially copyable: passed and returned by invisible reference
struct NT
{
	int32_t a, b;
	NT() = default;
	NT(int32_t a, int32_t b) : a(a), b(b) {}
	NT(const NT &o);
	~NT();
};
// Only a non-trivial copy constructor
struct NT2
{
	int32_t a, b;
	NT2() = default;
	NT2(int32_t a, int32_t b) : a(a), b(b) {}
	NT2(const NT2 &o);
};
// variant_t lookalike: 24 bytes, non-trivial destructor
struct NTBig
{
	union { int32_t i; float f; void *p; float v[3]; };
	int32_t eh;
	int32_t type;
	NTBig() = default;
	~NTBig();
};

// Log of what each callee received, compared between a direct C++ call and
// the bintools call.
struct BtRec
{
	uint8_t log[2048];
	size_t n;
	int misaligned;     // callee entered with rsp not 16-byte aligned
	int calls;
	int wrongslot;      // vtable dispatch hit the wrong index
	void *self;         // this pointer last seen
};
extern "C" BtRec bt_rec;

// Hand-built "object" for vtable tests: vtable pointer sits at
// this + thisOffs + vtblOffs, exactly where bintools reads it.
struct VObj
{
	uint8_t pad[512];
};

extern "C" {
// table of callees by name, for the plugin side
struct BtFunc { const char *name; void *fn; };
extern const BtFunc bt_funcs[];
}
