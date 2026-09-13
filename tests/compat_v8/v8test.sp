/**
 * Companion plugin for tests/compat_v8/v8test_ext.
 *
 * Compile with HEAD's spcomp and run with the ABI-8 v8test extension loaded:
 *     sm_v8test in the server console runs every check.
 */
#include <sourcemod>

#pragma semicolon 1
#pragma newdecls required

public Plugin myinfo =
{
	name = "v8 compat test",
	author = "AlliedModders",
	description = "Exercises the ABI-8 extension compatibility layer",
	version = "1.0.0.0",
	url = "http://www.sourcemod.net/"
};

native int V8Test_Strings(const char[] input, char[] out, int maxlen);
native int V8Test_SumArray(const int[] values, int count);
native bool V8Test_IsNullString(const char[] text);
native bool V8Test_IsNullVector(const float vec[3]);
native int V8Test_Throw(const char[] message);
native int V8Test_Heap(int cells);
native int V8Test_HeapInner(int cells);
native int V8Test_HeapReentrant(Function callback, int cells);
native Handle V8Test_CreateHandle();
native int V8Test_ReadHandle(Handle hndl);
native int V8Test_CallFunction(Function callback, int value, const char[] text);
native bool V8Test_WhoAmI(char[] out, int maxlen);
native int V8Test_FireForward(int value);
native int V8Test_Format(char[] out, int maxlen, const char[] fmt, any ...);
native bool V8Test_PageMemory();
native int V8Test_TestFeature(const char[] name);

int g_iFailures;
int g_iChecks;

public void OnPluginStart()
{
	RegServerCmd("sm_v8test", Cmd_RunTests);
}

void Check(bool ok, const char[] what)
{
	g_iChecks++;
	if (!ok)
	{
		g_iFailures++;
		PrintToServer("[v8test] FAIL: %s", what);
	}
	else
	{
		PrintToServer("[v8test] ok:   %s", what);
	}
}

public Action Cmd_RunTests(int args)
{
	g_iFailures = 0;
	g_iChecks = 0;

	char buffer[128];
	int written = V8Test_Strings("hello", buffer, sizeof(buffer));
	Check(StrEqual(buffer, "[hello]") && written == 7, "LocalToString/StringToLocalUTF8");

	int values[] = {1, 2, 3, 4, 5};
	Check(V8Test_SumArray(values, sizeof(values)) == 15, "LocalToPhysAddr over an array");

	Check(V8Test_IsNullString(NULL_STRING), "NULL_STRING reaches LocalToStringNULL");
	Check(!V8Test_IsNullString("not null"), "a real string is not NULL_STRING");
	Check(V8Test_IsNullVector(NULL_VECTOR), "NULL_VECTOR reaches GetNullRef");

	Check(V8Test_Heap(4) == 6, "HeapAlloc/HeapPop emulation");

	// The extension holds a heap allocation across a call into this plugin,
	// which calls back into the extension and allocates again. The outer
	// allocation must survive the inner native's return.
	g_iInnerHeapResult = 0;
	Check(V8Test_HeapReentrant(HeapReentryCallback, 8) == 8,
	      "HeapAlloc held across a reentrant native call");
	Check(g_iInnerHeapResult == 8, "nested native ran its own HeapAlloc/HeapPop");

	Handle hndl = V8Test_CreateHandle();
	Check(hndl != null, "handle creation from a legacy extension");
	if (hndl != null)
	{
		Check(V8Test_ReadHandle(hndl) == 0x5A5A, "handle read-back");
		delete hndl;
	}

	Check(V8Test_CallFunction(Callback, 21, "ping") == 42, "GetFunctionById + Push* + Execute");

	Check(V8Test_WhoAmI(buffer, sizeof(buffer)), "FindPluginByContext(GetContext())");
	Check(StrContains(buffer, "v8test") != -1, "resolved plugin filename");

	Check(V8Test_Format(buffer, sizeof(buffer), "%d-%s", 7, "seven") == 7, "ISourceMod::FormatString");
	Check(StrEqual(buffer, "7-seven"), "formatted output");

	Check(V8Test_PageMemory(), "page memory allocation through GetScriptingEngine()");

	Check(V8Test_TestFeature("V8Test_Heap") == 0 /* FeatureStatus_Available */,
	      "TestFeature on an extension native");

	g_bForwardFired = false;
	g_bNullStringSeen = false;
	g_bNullVectorSeen = false;
	int result = V8Test_FireForward(99);
	Check(g_bForwardFired, "forward fired");
	Check(g_bNullStringSeen, "forward NULL_STRING push");
	Check(g_bNullVectorSeen, "forward NULL_VECTOR push");
	Check(result == view_as<int>(Plugin_Handled), "forward result propagated");

	PrintToServer("[v8test] %d/%d checks passed", g_iChecks - g_iFailures, g_iChecks);
	return Plugin_Handled;
}

int g_iInnerHeapResult;

int HeapReentryCallback(int cells)
{
	// Re-enter the extension while it still holds a heap allocation.
	g_iInnerHeapResult = V8Test_HeapInner(cells);
	return cells;
}

int Callback(int value, char[] text, int maxlen)
{
	Format(text, maxlen, "%s-pong", text);
	return value * 2;
}

bool g_bForwardFired;
bool g_bNullStringSeen;
bool g_bNullVectorSeen;

public Action V8Test_OnEvent(int value, const char[] text, const float vec[3], any extra)
{
	g_bForwardFired = true;
	g_bNullStringSeen = IsNullString(text);
	g_bNullVectorSeen = IsNullVector(vec);

	PrintToServer("[v8test] forward: value=%d nullstr=%d nullvec=%d extra=0x%X", value,
	              g_bNullStringSeen, g_bNullVectorSeen, extra);

	return Plugin_Handled;
}
