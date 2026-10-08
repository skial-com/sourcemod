#if defined _bttest_included
 #endinput
#endif
#define _bttest_included

// Runs the direct IBinTools suite; prints results to the server console.
// Returns the number of failures.
native int BTTest_RunDirect(bool verbose = false);

// Address of a test callee (s_* functions in callees.cpp).
native bool BTTest_GetFunc(const char[] name, Address &addr);

// Fake object whose vtable slot <vtblIdx> is <fn>; every other slot traps.
native void BTTest_GetObject(int vtblIdx, const char[] fn, Address &obj);

// Callee log: reset before a call, then read back in order.
native void BTTest_ResetLog();
native int BTTest_NextInt(int size);
native float BTTest_NextFloat();
native void BTTest_NextAddr(Address &addr);
native void BTTest_NextString(char[] buf, int maxlen);
native int BTTest_LogRemaining();
native int BTTest_Calls();
native int BTTest_Misaligned();
native int BTTest_WrongSlot();
native void BTTest_Self(Address &addr);

native void BTTest_EdictAddr(int index, Address &addr);
native void BTTest_GameRules(Address &addr);
native void BTTest_IServer(Address &addr);
native bool BTTest_AddrEq(Address a, Address b);

public Extension __ext_bttest =
{
	name = "bttest",
	file = "bttest.ext",
	autoload = 1,
	required = 1,
};
