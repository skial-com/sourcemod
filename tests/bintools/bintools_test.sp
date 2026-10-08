// x86_64 bintools test suite (needs bttest.ext.so).
//
//   sm_bttest [all|direct|sdkcall|natives] [verbose]
//
// direct   IBinTools called from C++ with every parameter/return class (see extension.cpp)
// sdkcall  every SDKCall type, pass method and call type against test callees
// natives  the shipped SDKTools/SDKHooks/TF2 natives that call through bintools,
//          checked by their effect in game (spawns a fake client for player natives)
#include <sourcemod>
#include <sdktools>
#include <sdkhooks>
#include <tf2>
#include <tf2_stocks>
#include <bttest>

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo =
{
	name = "bintools test",
	description = "x86_64 bintools ABI test suite",
};

int g_pass, g_fail;
bool g_verbose;
char g_section[64];

public void OnPluginStart()
{
	RegServerCmd("sm_bttest", Cmd_Test);
}

void Check(bool ok, const char[] fmt, any ...)
{
	char buf[256];
	VFormat(buf, sizeof(buf), fmt, 3);
	if (ok)
	{
		g_pass++;
		if (g_verbose)
			PrintToServer("[bttest] ok    %s: %s", g_section, buf);
	}
	else
	{
		g_fail++;
		PrintToServer("[bttest] FAIL  %s: %s", g_section, buf);
	}
}

/* Log readers: compare what the callee received */
void ExpectInt(int size, int want, const char[] what)
{
	int got = BTTest_NextInt(size);
	Check(got == want, "%s: got %d want %d", what, got, want);
}
void ExpectFloat(float want, const char[] what)
{
	float got = BTTest_NextFloat();
	Check(got == want, "%s: got %f want %f", what, got, want);
}
void ExpectVec(const float want[3], const char[] what)
{
	float got[3];
	for (int i = 0; i < 3; i++)
		got[i] = BTTest_NextFloat();
	Check(got[0] == want[0] && got[1] == want[1] && got[2] == want[2],
		"%s: got (%.3f %.3f %.3f) want (%.3f %.3f %.3f)", what, got[0], got[1], got[2], want[0], want[1], want[2]);
}
void ExpectAddr(Address want, const char[] what)
{
	Address got;
	BTTest_NextAddr(got);
	Check(BTTest_AddrEq(got, want), "%s: pointer mismatch", what);
}
void ExpectString(const char[] want, const char[] what)
{
	char got[128];
	BTTest_NextString(got, sizeof(got));
	Check(StrEqual(got, want), "%s: got '%s' want '%s'", what, got, want);
}
void ExpectLogDone(const char[] what)
{
	int left = BTTest_LogRemaining();
	Check(left == 0 && BTTest_Misaligned() == 0 && BTTest_WrongSlot() == 0,
		"%s: %d unread log bytes, %d misaligned entries, %d wrong vtable slots", what, left, BTTest_Misaligned(), BTTest_WrongSlot());
}
void ExpectSelf(Address want, const char[] what)
{
	Address self;
	BTTest_Self(self);
	Check(BTTest_AddrEq(self, want), "%s: this pointer mismatch", what);
}

void PrepStatic(const char[] fn, SDKCallType type = SDKCall_Static)
{
	Address addr;
	BTTest_GetFunc(fn, addr);
	StartPrepSDKCall(type);
	PrepSDKCall_SetAddress(addr);
}

Handle End(const char[] what)
{
	Handle h = EndPrepSDKCall();
	Check(h != null, "%s: EndPrepSDKCall", what);
	return h;
}

public Action Cmd_Test(int args)
{
	char which[16] = "all", arg[16];
	g_verbose = false;
	for (int i = 1; i <= args; i++)
	{
		GetCmdArg(i, arg, sizeof(arg));
		if (StrEqual(arg, "verbose"))
			g_verbose = true;
		else
			strcopy(which, sizeof(which), arg);
	}
	g_pass = g_fail = 0;
	bool all = StrEqual(which, "all");

	int directFail = 0;
	if (all || StrEqual(which, "direct"))
		directFail = BTTest_RunDirect(g_verbose);
	if (all || StrEqual(which, "sdkcall"))
		TestSDKCall();
	if (all || StrEqual(which, "natives"))
		TestNatives();

	PrintToServer("[bttest] plugin: %d passed, %d failed%s", g_pass, g_fail,
		directFail ? " (plus direct failures above)" : "");
	PrintToServer("[bttest] RESULT: %s", (g_fail + directFail) == 0 ? "ALL PASSED" : "FAILURES");
	return Plugin_Handled;
}

/******************************************************************************
 * SDKCall against the s_* callees
 ******************************************************************************/
void TestSDKCall()
{
	Handle h;

	/* ---- PlainOldData plain: 8 ints, 2 on the stack ---- */
	g_section = "sdkcall POD";
	PrepStatic("s_pod");
	for (int i = 0; i < 8; i++)
		PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("8 x int")) != null)
	{
		BTTest_ResetLog();
		int r = SDKCall(h, -1, 2, -3, 4, 0x7FFFFFFF, 0x80000000, 7, -8);
		Check(r == -1 * 1000 + -8, "return %d", r);
		int want[8] = { -1, 2, -3, 4, 0x7FFFFFFF, 0x80000000, 7, -8 };
		for (int i = 0; i < 8; i++)
			ExpectInt(4, want[i], "int arg");
		ExpectLogDone("8 x int");
		delete h;
	}

	/* ---- Float plain: 10 floats, 2 on the stack ---- */
	g_section = "sdkcall Float";
	PrepStatic("s_float");
	for (int i = 0; i < 10; i++)
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Float, SDKPass_Plain);
	if ((h = End("10 x float")) != null)
	{
		BTTest_ResetLog();
		float r = SDKCall(h, 0.5, -1.5, 2.5, -3.5, 4.5, -5.5, 6.5, -7.5, 8.5, -9.5);
		Check(r == 0.5 + -9.5 * 100.0, "return %f", r);
		for (int i = 0; i < 10; i++)
			ExpectFloat((i & 1 ? -1.0 : 1.0) * (float(i) + 0.5), "float arg");
		ExpectLogDone("10 x float");
		delete h;
	}

	/* ---- Bool plain ---- */
	g_section = "sdkcall Bool";
	PrepStatic("s_bool");
	PrepSDKCall_AddParameter(SDKType_Bool, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Bool, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Bool, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Bool, SDKPass_Plain);
	if ((h = End("bools")) != null)
	{
		BTTest_ResetLog();
		bool r = SDKCall(h, true, false, 12345, true);
		Check(r == false, "return %d", r);
		ExpectInt(1, 1, "bool a"); ExpectInt(1, 0, "bool b"); ExpectInt(4, 12345, "int c"); ExpectInt(1, 1, "bool d");
		ExpectLogDone("bools");
		delete h;
	}

	/* ---- Pointers with copy-back: int*, float*, bool*, Vector*, Vector& ---- */
	g_section = "sdkcall pointers";
	PrepStatic("s_ptrs");
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Pointer, 0, VENCODE_FLAG_COPYBACK);
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Pointer, 0, VENCODE_FLAG_COPYBACK);
	PrepSDKCall_AddParameter(SDKType_Bool, SDKPass_Pointer, 0, VENCODE_FLAG_COPYBACK);
	PrepSDKCall_AddParameter(SDKType_Vector, SDKPass_Pointer, 0, VENCODE_FLAG_COPYBACK);
	PrepSDKCall_AddParameter(SDKType_Vector, SDKPass_ByRef, 0, VENCODE_FLAG_COPYBACK);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("pointers")) != null)
	{
		int a = 10;
		float b = 1.25;
		bool c = true;
		float d[3] = { 1.0, 2.0, 3.0 }, e[3] = { 4.0, 5.0, 6.0 };
		BTTest_ResetLog();
		SDKCall(h, a, b, c, d, e);
		ExpectInt(4, 10, "int*"); ExpectFloat(1.25, "float*"); ExpectInt(1, 1, "bool*");
		float wd[3] = { 1.0, 2.0, 3.0 }, we[3] = { 4.0, 5.0, 6.0 };
		ExpectVec(wd, "Vector*"); ExpectVec(we, "Vector&");
		ExpectLogDone("pointers");
		Check(a == 15, "int copy-back %d", a);
		Check(b == 2.5, "float copy-back %f", b);
		Check(c == false, "bool copy-back %d", c);
		Check(d[0] == 2.0 && d[1] == 4.0 && d[2] == 6.0, "Vector* copy-back (%.1f %.1f %.1f)", d[0], d[1], d[2]);
		Check(e[2] == -6.0, "Vector& copy-back z %.1f", e[2]);
		delete h;
	}

	/* ---- Vector/QAngle by value mixed with scalars ---- */
	g_section = "sdkcall Vector by value";
	PrepStatic("s_vecs");
	PrepSDKCall_AddParameter(SDKType_Vector, SDKPass_ByValue);
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_QAngle, SDKPass_ByValue);
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Vector, SDKPass_ByValue);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("vec, float, qangle, int, vec")) != null)
	{
		float a[3] = { 1.5, -2.5, 3.5 }, c[3] = { 10.0, 20.0, -30.0 }, e[3] = { -7.25, 8.25, 9.75 };
		BTTest_ResetLog();
		int r = SDKCall(h, a, 4.5, c, -99, e);
		Check(r == 2, "return %d", r);
		ExpectVec(a, "Vector"); ExpectFloat(4.5, "float"); ExpectVec(c, "QAngle"); ExpectInt(4, -99, "int"); ExpectVec(e, "Vector");
		ExpectLogDone("vectors");
		delete h;
	}
	PrepStatic("s_vec_spill");
	for (int i = 0; i < 7; i++)
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Vector, SDKPass_ByValue);
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("7 floats + Vector (stack) + float")) != null)
	{
		float v[3] = { 11.0, 12.0, 13.0 };
		BTTest_ResetLog();
		SDKCall(h, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, v, 8.0);
		ExpectFloat(1.0, "float 1"); ExpectFloat(7.0, "float 7"); ExpectVec(v, "Vector on stack"); ExpectFloat(8.0, "float in xmm7");
		ExpectLogDone("vector spill");
		delete h;
	}

	/* ---- CBaseEntity*, CBasePlayer*, edict_t*, char*, Address ---- */
	g_section = "sdkcall entity/string/address";
	int ent = CreateEntityByName("info_target");
	DispatchSpawn(ent);
	int client = FindTestClient();
	PrepStatic("s_ents");
	PrepSDKCall_AddParameter(SDKType_CBaseEntity, SDKPass_Pointer);
	PrepSDKCall_AddParameter(SDKType_CBasePlayer, SDKPass_Pointer, VDECODE_FLAG_ALLOWNULL);
	PrepSDKCall_AddParameter(SDKType_Edict, SDKPass_Pointer);
	PrepSDKCall_AddParameter(SDKType_String, SDKPass_Pointer);
	PrepSDKCall_AddParameter(SDKType_Address, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("entity args")) != null)
	{
		Address obj;
		BTTest_GetObject(0, "s_this", obj);
		BTTest_ResetLog();
		SDKCall(h, ent, client > 0 ? client : -1, ent, "hello bintools", obj);
		ExpectAddr(GetEntityAddress(ent), "CBaseEntity*");
		if (client > 0)
			ExpectAddr(GetEntityAddress(client), "CBasePlayer*");
		else
			ExpectAddr(Address_Null, "CBasePlayer* (NULL)");
		Address edict;
		BTTest_EdictAddr(ent, edict);
		ExpectAddr(edict, "edict_t*");
		ExpectString("hello bintools", "char*");
		ExpectAddr(obj, "Address");
		ExpectLogDone("entity args");
		delete h;
	}

	PrepStatic("s_addr_ptr");
	PrepSDKCall_AddParameter(SDKType_Address, SDKPass_Pointer, 0, VENCODE_FLAG_COPYBACK);
	PrepSDKCall_SetReturnInfo(SDKType_Address, SDKPass_Plain);
	if ((h = End("Address* + Address return")) != null)
	{
		Address obj, orig, ret;
		BTTest_GetObject(0, "s_this", obj);
		orig = obj;
		BTTest_ResetLog();
		SDKCall(h, ret, obj);
		ExpectAddr(orig, "Address*");
		Check(BTTest_AddrEq(ret, orig), "Address return");
		Check(BTTest_AddrEq(obj, orig + view_as<Address>(16)), "Address* copy-back");
		delete h;
	}

	/* ---- every return type ---- */
	g_section = "sdkcall returns";
	PrepStatic("s_ret_str");
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_String, SDKPass_Pointer);
	if ((h = End("string return")) != null)
	{
		char buf[64];
		SDKCall(h, buf, sizeof(buf), 1);
		Check(StrEqual(buf, "bintools-test-string"), "string return '%s'", buf);
		delete h;
	}
	PrepStatic("s_ret_vec");
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Vector, SDKPass_ByValue);
	if ((h = End("Vector return by value")) != null)
	{
		float v[3];
		SDKCall(h, v, 2.5);
		Check(v[0] == 2.5 && v[1] == 3.5 && v[2] == 4.5, "Vector by value (%.2f %.2f %.2f)", v[0], v[1], v[2]);
		delete h;
	}
	PrepStatic("s_ret_vec");
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_QAngle, SDKPass_ByValue);
	if ((h = End("QAngle return by value")) != null)
	{
		float v[3];
		SDKCall(h, v, -2.5);
		Check(v[0] == -2.5 && v[1] == -1.5 && v[2] == -0.5, "QAngle by value (%.2f %.2f %.2f)", v[0], v[1], v[2]);
		delete h;
	}
	PrepStatic("s_ret_vecptr");
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Vector, SDKPass_Pointer);
	if ((h = End("Vector* return")) != null)
	{
		float v[3];
		SDKCall(h, v, 1.5);
		Check(v[0] == 3.0 && v[1] == 4.5 && v[2] == 6.0, "Vector* (%.2f %.2f %.2f)", v[0], v[1], v[2]);
		delete h;
	}
	PrepStatic("s_ret_ptr");
	PrepSDKCall_AddParameter(SDKType_CBaseEntity, SDKPass_Pointer);
	PrepSDKCall_SetReturnInfo(SDKType_CBaseEntity, SDKPass_Pointer);
	if ((h = End("CBaseEntity* return")) != null)
	{
		int r = SDKCall(h, ent);
		Check(r == ent, "CBaseEntity* return %d want %d", r, ent);
		delete h;
	}
	PrepStatic("s_ret_ptr");
	PrepSDKCall_AddParameter(SDKType_Edict, SDKPass_Pointer);
	PrepSDKCall_SetReturnInfo(SDKType_Edict, SDKPass_Pointer);
	if ((h = End("edict_t* return")) != null)
	{
		int r = SDKCall(h, ent);
		Check(r == ent, "edict_t* return %d want %d", r, ent);
		delete h;
	}
	if (client > 0)
	{
		PrepStatic("s_ret_ptr");
		PrepSDKCall_AddParameter(SDKType_CBasePlayer, SDKPass_Pointer);
		PrepSDKCall_SetReturnInfo(SDKType_CBasePlayer, SDKPass_Pointer);
		if ((h = End("CBasePlayer* return")) != null)
		{
			int r = SDKCall(h, client);
			Check(r == client, "CBasePlayer* return %d want %d", r, client);
			delete h;
		}
	}
	PrepStatic("s_ret_intptr");
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Pointer);
	if ((h = End("int* return")) != null)
	{
		int r = SDKCall(h, -6);
		Check(r == -42, "int* return %d", r);
		delete h;
	}
	PrepStatic("s_ret_floatptr");
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Float, SDKPass_Pointer);
	if ((h = End("float* return")) != null)
	{
		float r = SDKCall(h, 1.5);
		Check(r == 10.5, "float* return %f", r);
		delete h;
	}
	PrepStatic("s_ret_boolptr");
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Bool, SDKPass_Pointer);
	if ((h = End("bool* return")) != null)
	{
		bool r = SDKCall(h, 9);
		Check(r == true, "bool* return %d", r);
		delete h;
	}
	PrepStatic("s_ret_float");
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Float, SDKPass_Plain);
	if ((h = End("float return")) != null)
	{
		// The original crash: a float return stored 16 bytes into a 4-byte buffer.
		// Call it many times so heap damage would show up as a crash or bad value.
		bool ok = true;
		for (int i = 0; i < 2000; i++)
		{
			float r = SDKCall(h, float(i));
			if (r != float(i) * -3.0)
				ok = false;
		}
		Check(ok, "float return x2000");
		delete h;
	}
	PrepStatic("s_ret_bool");
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Bool, SDKPass_Plain);
	if ((h = End("bool return")) != null)
	{
		Check(SDKCall(h, 42) == true && SDKCall(h, 41) == false, "bool return");
		delete h;
	}

	/* ---- this-call types ---- */
	g_section = "sdkcall this";
	{
		Address obj;
		BTTest_GetObject(0, "s_this", obj);
		PrepStatic("s_this", SDKCall_Raw);
		PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
		PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
		if ((h = End("Raw + address")) != null)
		{
			BTTest_ResetLog();
			int r = SDKCall(h, obj, 41, 2.5);
			Check(r == 42, "return %d", r);
			ExpectSelf(obj, "Raw this");
			ExpectInt(4, 41, "int"); ExpectFloat(2.5, "float");
			ExpectLogDone("Raw");
			delete h;
		}
	}
	int slots[5] = { 0, 1, 16, 40, 63 };
	for (int s = 0; s < sizeof(slots); s++)
	{
		Address obj;
		BTTest_GetObject(slots[s], "s_this", obj);
		StartPrepSDKCall(SDKCall_Raw);
		PrepSDKCall_SetVirtual(slots[s]);
		PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
		PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
		if ((h = End("Raw + vtable")) != null)
		{
			BTTest_ResetLog();
			int r = SDKCall(h, obj, slots[s], -0.5);
			Check(r == slots[s] + 1, "vtable slot %d return %d", slots[s], r);
			ExpectSelf(obj, "vtable this");
			ExpectInt(4, slots[s], "int"); ExpectFloat(-0.5, "float");
			ExpectLogDone("vtable");
			delete h;
		}
	}
	{
		Address obj;
		BTTest_GetObject(7, "s_this_retvec", obj);
		StartPrepSDKCall(SDKCall_Raw);
		PrepSDKCall_SetVirtual(7);
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
		PrepSDKCall_SetReturnInfo(SDKType_Vector, SDKPass_ByValue);
		if ((h = End("Raw vtable + Vector return")) != null)
		{
			float v[3];
			BTTest_ResetLog();
			SDKCall(h, obj, v, 1.5);
			Check(v[0] == 1.5 && v[1] == -1.5 && v[2] == 15.0, "Vector return (%.2f %.2f %.2f)", v[0], v[1], v[2]);
			ExpectSelf(obj, "this");
			ExpectFloat(1.5, "float");
			ExpectLogDone("vtable vec");
			delete h;
		}
	}
	PrepStatic("s_this", SDKCall_Entity);
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("Entity")) != null)
	{
		BTTest_ResetLog();
		SDKCall(h, ent, 1, 1.0);
		ExpectSelf(GetEntityAddress(ent), "SDKCall_Entity this");
		delete h;
	}
	if (client > 0)
	{
		PrepStatic("s_this", SDKCall_Player);
		PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
		PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
		if ((h = End("Player")) != null)
		{
			BTTest_ResetLog();
			SDKCall(h, client, 1, 1.0);
			ExpectSelf(GetEntityAddress(client), "SDKCall_Player this");
			delete h;
		}
	}
	PrepStatic("s_this", SDKCall_GameRules);
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("GameRules")) != null)
	{
		Address gr;
		BTTest_GameRules(gr);
		BTTest_ResetLog();
		SDKCall(h, 1, 1.0);
		ExpectSelf(gr, "SDKCall_GameRules this");
		delete h;
	}
	PrepStatic("s_this", SDKCall_Server);
	PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
	PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
	if ((h = End("Server")) != null)
	{
		Address sv;
		BTTest_IServer(sv);
		BTTest_ResetLog();
		SDKCall(h, 1, 1.0);
		ExpectSelf(sv, "SDKCall_Server this");
		delete h;
	}
	SDKCallType others[2] = { SDKCall_EntityList, SDKCall_Engine };
	for (int i = 0; i < 2; i++)
	{
		PrepStatic("s_this", others[i]);
		PrepSDKCall_AddParameter(SDKType_PlainOldData, SDKPass_Plain);
		PrepSDKCall_AddParameter(SDKType_Float, SDKPass_Plain);
		PrepSDKCall_SetReturnInfo(SDKType_PlainOldData, SDKPass_Plain);
		if ((h = End(i ? "Engine" : "EntityList")) != null)
		{
			BTTest_ResetLog();
			int r = SDKCall(h, 5, 1.0);
			Address self;
			BTTest_Self(self);
			Check(r == 6 && !BTTest_AddrEq(self, Address_Null), "%s: this set, return %d", i ? "Engine" : "EntityList", r);
			ExpectInt(4, 5, "int"); ExpectFloat(1.0, "float");
			delete h;
		}
	}

	/* ---- 20 mixed params through the SDKTools encoder ---- */
	g_section = "sdkcall many";
	PrepStatic("s_many");
	SDKType types[20] = { SDKType_PlainOldData, SDKType_Float, SDKType_Vector, SDKType_PlainOldData, SDKType_Float, SDKType_Bool,
		SDKType_PlainOldData, SDKType_Float, SDKType_QAngle, SDKType_PlainOldData, SDKType_Float, SDKType_PlainOldData, SDKType_Float,
		SDKType_Vector, SDKType_PlainOldData, SDKType_Float, SDKType_PlainOldData, SDKType_Float, SDKType_PlainOldData, SDKType_Float };
	for (int i = 0; i < 20; i++)
		PrepSDKCall_AddParameter(types[i], (types[i] == SDKType_Vector || types[i] == SDKType_QAngle) ? SDKPass_ByValue : SDKPass_Plain);
	PrepSDKCall_SetReturnInfo(SDKType_Float, SDKPass_Plain);
	if ((h = End("20 mixed")) != null)
	{
		float c[3] = { 1.0, 2.0, 3.0 }, iv[3] = { 4.0, 5.0, 6.0 }, n[3] = { 7.0, 8.0, 9.0 };
		BTTest_ResetLog();
		float r = SDKCall(h, 1, 1.5, c, 2, 2.5, true, 3, 3.5, iv, 4, 4.5, 5, 5.5, n, 6, 6.5, 7, 7.5, 8, 8.5);
		Check(r == 1.5 + 8.5, "return %f", r);
		ExpectInt(4, 1, "a"); ExpectFloat(1.5, "b"); ExpectVec(c, "c"); ExpectInt(4, 2, "d"); ExpectFloat(2.5, "e"); ExpectInt(1, 1, "f");
		ExpectInt(4, 3, "g"); ExpectFloat(3.5, "h"); ExpectVec(iv, "i"); ExpectInt(4, 4, "j"); ExpectFloat(4.5, "k"); ExpectInt(4, 5, "l");
		ExpectFloat(5.5, "m"); ExpectVec(n, "n"); ExpectInt(4, 6, "o"); ExpectFloat(6.5, "p"); ExpectInt(4, 7, "q"); ExpectFloat(7.5, "r");
		ExpectInt(4, 8, "s"); ExpectFloat(8.5, "t");
		ExpectLogDone("20 mixed");
		delete h;
	}

	RemoveEntity(ent);
}

/******************************************************************************
 * Shipped natives that call through bintools, checked in game
 ******************************************************************************/
int g_outputFired;
int g_outputActivator;

public void OnUser1(const char[] output, int caller, int activator, float delay)
{
	g_outputFired++;
	g_outputActivator = activator;
}

int g_fakeClient;

float AngleDiff(float a, float b)
{
	float d = a - b;
	while (d > 180.0) d -= 360.0;
	while (d < -180.0) d += 360.0;
	return d;
}

int FindTestClient()
{
	for (int i = 1; i <= MaxClients; i++)
		if (IsClientInGame(i) && IsPlayerAlive(i) && GetClientTeam(i) > 1)
			return i;
	return 0;
}

int SpawnTestClient()
{
	int client = FindTestClient();
	if (client)
		return client;
	if (!g_fakeClient || !IsClientInGame(g_fakeClient))
		g_fakeClient = CreateFakeClient("bttest");
	if (!g_fakeClient)
		return 0;
	ChangeClientTeam(g_fakeClient, 2);
	TF2_SetPlayerClass(g_fakeClient, TFClass_Soldier);
	TF2_RespawnPlayer(g_fakeClient);
	return IsPlayerAlive(g_fakeClient) ? g_fakeClient : 0;
}

void TestNatives()
{
	/* ---- AcceptEntityInput: variant_t by value, every field type ---- */
	g_section = "AcceptEntityInput";
	int prop = CreateEntityByName("prop_dynamic_override");
	DispatchKeyValue(prop, "model", "models/player/soldier.mdl");
	DispatchKeyValue(prop, "solid", "0");
	bool spawned = DispatchSpawn(prop);
	Check(spawned, "DispatchSpawn prop_dynamic");
	ActivateEntity(prop);

	for (int i = 0; i < 100; i++)
	{
		char name[32], kv[48], got[32];
		Format(name, sizeof(name), "bttest_%d", i);
		Format(kv, sizeof(kv), "targetname %s", name);
		SetVariantString(kv);
		bool r = AcceptEntityInput(prop, "AddOutput");
		GetEntPropString(prop, Prop_Data, "m_iName", got, sizeof(got));
		if (!r || !StrEqual(got, name))
		{
			Check(false, "string variant #%d: ret %d name '%s'", i, r, got);
			break;
		}
		if (i == 99)
			Check(true, "string variant x100");
	}
	SetVariantInt(3);
	Check(AcceptEntityInput(prop, "SetTeam") && GetEntProp(prop, Prop_Send, "m_iTeamNum") == 3, "int variant (SetTeam)");
	SetVariantInt(77);
	AcceptEntityInput(prop, "Alpha");
	int rr, gg, bb, aa;
	GetEntityRenderColor(prop, rr, gg, bb, aa);
	Check(aa == 77, "int variant (Alpha) %d", aa);
	SetVariantFloat(1.75);
	AcceptEntityInput(prop, "SetPlaybackRate");
	float rate = GetEntPropFloat(prop, Prop_Send, "m_flPlaybackRate");
	Check(rate == 1.75, "float variant (SetPlaybackRate) %f", rate);
	SetVariantBool(true);
	AcceptEntityInput(prop, "AlternativeSorting");
	Check(GetEntProp(prop, Prop_Send, "m_bAlternateSorting") == 1, "bool variant (AlternativeSorting)");
	int color[4] = { 10, 20, 30, 40 };
	SetVariantColor(color);
	AcceptEntityInput(prop, "Color");
	GetEntityRenderColor(prop, rr, gg, bb, aa);
	Check(rr == 10 && gg == 20 && bb == 30, "color variant (Color) %d %d %d", rr, gg, bb);
	float scale[3] = { 1.5, 0.0, 0.0 };
	SetVariantVector3D(scale);
	AcceptEntityInput(prop, "SetModelScale");
	float ms = GetEntPropFloat(prop, Prop_Send, "m_flModelScale");
	Check(ms == 1.5, "vector variant (SetModelScale) %f", ms);

	/* ---- FireEntityOutput: variant_t by value on a this-call ---- */
	g_section = "FireEntityOutput";
	g_outputFired = 0;
	g_outputActivator = -2;
	HookSingleEntityOutput(prop, "OnUser1", OnUser1);
	FireEntityOutput(prop, "OnUser1", prop, 0.0);
	Check(g_outputFired == 1 && g_outputActivator == prop, "OnUser1 fired %d activator %d", g_outputFired, g_outputActivator);
	UnhookSingleEntityOutput(prop, "OnUser1", OnUser1);

	/* ---- DispatchKeyValue* (Vector by value), TeleportEntity, collision/owner ---- */
	g_section = "entity natives";
	float origin[3] = { 123.0, -456.0, 78.0 }, got[3];
	DispatchKeyValueVector(prop, "origin", origin);
	GetEntPropVector(prop, Prop_Data, "m_vecAbsOrigin", got);
	Check(got[0] == origin[0] && got[1] == origin[1] && got[2] == origin[2], "DispatchKeyValueVector (%.1f %.1f %.1f)", got[0], got[1], got[2]);
	DispatchKeyValueFloat(prop, "modelscale", 0.5);
	Check(GetEntPropFloat(prop, Prop_Send, "m_flModelScale") == 0.5, "DispatchKeyValueFloat");
	float pos[3] = { -10.0, 20.0, -30.0 }, ang[3] = { 0.0, 90.0, 0.0 };
	TeleportEntity(prop, pos, ang, NULL_VECTOR);
	GetEntPropVector(prop, Prop_Data, "m_vecAbsOrigin", got);
	Check(got[0] == pos[0] && got[1] == pos[1] && got[2] == pos[2], "TeleportEntity origin (%.1f %.1f %.1f)", got[0], got[1], got[2]);
	GetEntPropVector(prop, Prop_Data, "m_angAbsRotation", got);
	Check(got[1] == 90.0, "TeleportEntity angles yaw %.1f", got[1]);
	SetEntityCollisionGroup(prop, 2);
	Check(GetEntProp(prop, Prop_Send, "m_CollisionGroup") == 2, "SetEntityCollisionGroup");
	EntityCollisionRulesChanged(prop);
	Check(true, "EntityCollisionRulesChanged");
	int other = CreateEntityByName("info_target");
	DispatchSpawn(other);
	SetEntityOwner(prop, other);
	Check(GetEntPropEnt(prop, Prop_Send, "m_hOwnerEntity") == other, "SetEntityOwner");
	SetEntityModel(prop, "models/player/heavy.mdl");
	char model[PLATFORM_MAX_PATH];
	GetEntPropString(prop, Prop_Data, "m_ModelName", model, sizeof(model));
	Check(StrEqual(model, "models/player/heavy.mdl"), "SetEntityModel '%s'", model);
	int att = LookupEntityAttachment(prop, "head");
	Check(att > 0, "LookupEntityAttachment head = %d", att);
	if (att > 0)
	{
		float aorg[3], aang[3];
		bool ok = GetEntityAttachment(prop, att, aorg, aang);
		float d = GetVectorDistance(aorg, pos);
		Check(ok && d > 10.0 && d < 200.0, "GetEntityAttachment head %.1f units from origin", d);
	}
	int found = -1, seen = 0;
	while ((found = FindEntityByClassname(found, "prop_dynamic")) != -1)
		if (found == prop)
			seen++;
	Check(seen == 1, "FindEntityByClassname");
	SetLightStyle(63, "m");
	Check(true, "SetLightStyle");
	RemoveEntity(other);
	RemoveEntity(prop);

	/* ---- player natives (fake client if nobody is playing) ---- */
	g_section = "player natives";
	int client = SpawnTestClient();
	if (!client)
	{
		Check(false, "no live client and could not spawn a fake client");
		return;
	}
	float eye[3], eyeAng[3], org[3], view[3];
	GetClientEyePosition(client, eye);
	GetClientAbsOrigin(client, org);
	GetEntPropVector(client, Prop_Data, "m_vecViewOffset", view);
	Check(FloatAbs(eye[0] - org[0] - view[0]) < 0.1 && FloatAbs(eye[2] - org[2] - view[2]) < 0.1,
		"GetClientEyePosition (%.1f %.1f %.1f)", eye[0], eye[1], eye[2]);
	GetClientEyeAngles(client, eyeAng);
	float pitch = GetEntPropFloat(client, Prop_Send, "m_angEyeAngles[0]"), yaw = GetEntPropFloat(client, Prop_Send, "m_angEyeAngles[1]");
	Check(FloatAbs(eyeAng[0] - pitch) < 0.1 && FloatAbs(AngleDiff(eyeAng[1], yaw)) < 0.1,
		"GetClientEyeAngles (%.2f %.2f) vs netprop (%.2f %.2f)", eyeAng[0], eyeAng[1], pitch, yaw);
	float vel[3] = { 100.0, -200.0, 300.0 };
	TeleportEntity(client, NULL_VECTOR, NULL_VECTOR, vel);
	GetEntPropVector(client, Prop_Data, "m_vecAbsVelocity", got);
	Check(got[0] == 100.0 && got[1] == -200.0 && got[2] == 300.0, "TeleportEntity velocity (%.1f %.1f %.1f)", got[0], got[1], got[2]);
	int weapon = GetPlayerWeaponSlot(client, 0);
	Check(weapon > MaxClients && IsValidEntity(weapon), "GetPlayerWeaponSlot = %d", weapon);
	int headAtt = LookupEntityAttachment(client, "head");
	Check(headAtt > 0, "LookupEntityAttachment(player) = %d", headAtt);

	// Players use CBaseAnimating::Ignite (props override it and ignore non-flammable
	// models). A garbled bNPCOnly bool returns early; the flame's lifetime checks the float.
	IgniteEntity(client, 3.5);
	float life = -1.0;
	int flame = -1;
	while ((flame = FindEntityByClassname(flame, "entityflame")) != -1)
		if (GetEntPropEnt(flame, Prop_Send, "m_hEntAttached") == client)
			life = GetEntPropFloat(flame, Prop_Data, "m_flLifetime") - GetGameTime();
	Check((GetEntityFlags(client) & FL_ONFIRE) != 0 && FloatAbs(life - 3.5) < 0.01, "IgniteEntity: flag %d, flame lifetime %.3f",
		(GetEntityFlags(client) & FL_ONFIRE) != 0, life);
	ExtinguishEntity(client);
	Check((GetEntityFlags(client) & FL_ONFIRE) == 0, "ExtinguishEntity");

	TF2_RegeneratePlayer(client);
	int maxhp = GetClientHealth(client);
	SetEntityHealth(client, maxhp);
	SDKHooks_TakeDamage(client, 0, 0, 10.0, DMG_GENERIC);
	Check(GetClientHealth(client) == maxhp - 10, "SDKHooks_TakeDamage %d -> %d", maxhp, GetClientHealth(client));
	SlapPlayer(client, 5, false);
	Check(GetClientHealth(client) == maxhp - 15, "SlapPlayer health %d", GetClientHealth(client));
	TF2_RegeneratePlayer(client);
	Check(GetClientHealth(client) == maxhp, "TF2_RegeneratePlayer health %d", GetClientHealth(client));

	TF2_AddCondition(client, TFCond_Kritzkrieged, 5.0);
	Check(TF2_IsPlayerInCondition(client, TFCond_Kritzkrieged), "TF2_AddCondition");
	TF2_RemoveCondition(client, TFCond_Kritzkrieged);
	Check(!TF2_IsPlayerInCondition(client, TFCond_Kritzkrieged), "TF2_RemoveCondition");
	TF2_StunPlayer(client, 2.0, 0.5, TF_STUNFLAG_SLOWDOWN);
	Check(TF2_IsPlayerInCondition(client, TFCond_Dazed), "TF2_StunPlayer");
	TF2_RemoveCondition(client, TFCond_Dazed);
	TF2_MakeBleed(client, client, 2.0);
	Check(TF2_IsPlayerInCondition(client, TFCond_Bleeding), "TF2_MakeBleed");
	TF2_RemoveCondition(client, TFCond_Bleeding);
	TF2_IgnitePlayer(client, client);
	Check(TF2_IsPlayerInCondition(client, TFCond_OnFire), "TF2_IgnitePlayer");
	TF2_RemoveCondition(client, TFCond_OnFire);
	bool duel = TF2_IsPlayerInDuel(client);
	Check(!duel, "TF2_IsPlayerInDuel = %d", duel);
	bool bday = TF2_IsHolidayActive(TFHoliday_Birthday);
	bool forced = FindConVar("tf_birthday").BoolValue || FindConVar("tf_forced_holiday").IntValue == view_as<int>(TFHoliday_Birthday);
	TF2_IsHolidayActive(TFHoliday_Halloween);
	Check(bday == forced, "TF2_IsHolidayActive(Birthday) = %d, forced %d", bday, forced);
	TF2_SetPlayerPowerPlay(client, true);
	TF2_SetPlayerPowerPlay(client, false);
	Check(true, "TF2_SetPlayerPowerPlay");

	TF2_SetPlayerClass(client, TFClass_Spy);
	TF2_RespawnPlayer(client);
	Check(IsPlayerAlive(client) && TF2_GetPlayerClass(client) == TFClass_Spy, "TF2_RespawnPlayer as spy");
	TF2_DisguisePlayer(client, TFTeam_Blue, TFClass_Pyro);
	bool disguising = TF2_IsPlayerInCondition(client, TFCond_Disguising) || TF2_IsPlayerInCondition(client, TFCond_Disguised);
	Check(disguising, "TF2_DisguisePlayer");
	TF2_RemovePlayerDisguise(client);
	Check(!TF2_IsPlayerInCondition(client, TFCond_Disguised), "TF2_RemovePlayerDisguise");

	ForcePlayerSuicide(client);
	Check(!IsPlayerAlive(client), "ForcePlayerSuicide");
	TF2_SetPlayerClass(client, TFClass_Soldier);
	TF2_RespawnPlayer(client);
	Check(IsPlayerAlive(client), "TF2_RespawnPlayer");

	if (client == g_fakeClient)
	{
		KickClient(g_fakeClient, "bttest done");
		g_fakeClient = 0;
	}
}
