/**
 * v8 compatibility test extension. Built only against the ABI-8 headers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extension.h"

V8TestExt g_V8TestExt;
SMEXT_LINK(&g_V8TestExt);

IChangeableForward *g_pTestForward = NULL;
HandleType_t g_TestHandleType = 0;

class TestHandleDispatch : public IHandleTypeDispatch
{
public:
	void OnHandleDestroy(HandleType_t type, void *object)
	{
		free(object);
	}
};

static TestHandleDispatch s_HandleDispatch;

/* ------------------------------------------------------------------------- */

// native int V8Test_Strings(const char[] input, char[] out, int maxlen);
static cell_t V8Test_Strings(IPluginContext *pContext, const cell_t *params)
{
	char *input;
	int err = pContext->LocalToString(params[1], &input);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("LocalToString failed (%d)", err);

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "[%s]", input);

	size_t written = 0;
	err = pContext->StringToLocalUTF8(params[2], params[3], buffer, &written);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("StringToLocalUTF8 failed (%d)", err);

	return (cell_t)written;
}

// native int V8Test_SumArray(const int[] values, int count);
static cell_t V8Test_SumArray(IPluginContext *pContext, const cell_t *params)
{
	cell_t *addr;
	int err = pContext->LocalToPhysAddr(params[1], &addr);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("LocalToPhysAddr failed (%d)", err);

	cell_t sum = 0;
	for (cell_t i = 0; i < params[2]; i++)
		sum += addr[i];
	return sum;
}

// native bool V8Test_IsNullString(const char[] text);
static cell_t V8Test_IsNullString(IPluginContext *pContext, const cell_t *params)
{
	char *text = NULL;
	int err = pContext->LocalToStringNULL(params[1], &text);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("LocalToStringNULL failed (%d)", err);

	/* GetNullRef only ever answered SP_NULL_VECTOR; SP_NULL_STRING returned
	 * NULL in the ABI-8 VM as well, so that is what the adapter must report. */
	if (pContext->GetNullRef(SP_NULL_STRING) != NULL)
		return pContext->ThrowNativeError("GetNullRef(SP_NULL_STRING) should be NULL");

	return text == NULL ? 1 : 0;
}

// native bool V8Test_IsNullVector(const float[3] vec);
static cell_t V8Test_IsNullVector(IPluginContext *pContext, const cell_t *params)
{
	cell_t *nullref = pContext->GetNullRef(SP_NULL_VECTOR);
	if (nullref == NULL)
		return pContext->ThrowNativeError("GetNullRef(SP_NULL_VECTOR) returned NULL");

	cell_t *addr;
	if (pContext->LocalToPhysAddr(params[1], &addr) != SP_ERROR_NONE)
		return 0;

	return addr == nullref ? 1 : 0;
}

// native void V8Test_Throw(const char[] message);
static cell_t V8Test_Throw(IPluginContext *pContext, const cell_t *params)
{
	char *message;
	pContext->LocalToString(params[1], &message);
	return pContext->ThrowNativeError("%s", message);
}

// native int V8Test_Heap(int cells);
static cell_t V8Test_Heap(IPluginContext *pContext, const cell_t *params)
{
	cell_t local = 0;
	cell_t *phys = NULL;

	int err = pContext->HeapAlloc(params[1], &local, &phys);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("HeapAlloc failed (%d)", err);
	if (phys == NULL)
		return pContext->ThrowNativeError("HeapAlloc gave no physical address");

	for (cell_t i = 0; i < params[1]; i++)
		phys[i] = i;

	cell_t sum = 0;
	for (cell_t i = 0; i < params[1]; i++)
		sum += phys[i];

	err = pContext->HeapPop(local);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("HeapPop failed (%d)", err);

	return sum;
}

// native int V8Test_HeapInner(int cells);
//
// Called back into from the plugin callback that V8Test_HeapReentrant invokes,
// so its allocation nests inside the outer native's still-open heap scope.
static cell_t V8Test_HeapInner(IPluginContext *pContext, const cell_t *params)
{
	cell_t local = 0;
	cell_t *phys = NULL;

	int err = pContext->HeapAlloc(params[1], &local, &phys);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("inner HeapAlloc failed (%d)", err);

	for (cell_t i = 0; i < params[1]; i++)
		phys[i] = 0x7F7F7F7F;

	err = pContext->HeapPop(local);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("inner HeapPop failed (%d)", err);

	return params[1];
}

// native int V8Test_HeapReentrant(Function callback, int cells);
//
// Holds an outstanding HeapAlloc across a call into plugin code that re-enters
// the extension (and allocates again) on the same runtime, then checks that its
// own allocation is still intact and still pops cleanly.
static cell_t V8Test_HeapReentrant(IPluginContext *pContext, const cell_t *params)
{
	cell_t local = 0;
	cell_t *phys = NULL;

	int err = pContext->HeapAlloc(params[2], &local, &phys);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("outer HeapAlloc failed (%d)", err);
	if (phys == NULL)
		return pContext->ThrowNativeError("outer HeapAlloc gave no physical address");

	for (cell_t i = 0; i < params[2]; i++)
		phys[i] = 0xABC0 + i;

	IPluginFunction *fun = pContext->GetFunctionById((funcid_t)params[1]);
	if (fun == NULL) {
		pContext->HeapPop(local);
		return pContext->ThrowNativeError("Invalid function id");
	}

	cell_t callback_result = 0;
	{
		DetectExceptions eh(pContext);

		fun->PushCell(params[2]);
		int callerr = fun->Execute(&callback_result);

		if (eh.HasException()) {
			pContext->HeapPop(local);
			return pContext->ThrowNativeError("reentrant callback threw: %s", eh.Message());
		}
		if (callerr != SP_ERROR_NONE) {
			pContext->HeapPop(local);
			return pContext->ThrowNativeError("reentrant callback failed (%d)", callerr);
		}
	}

	/* The nested native's HeapAlloc/HeapPop must not have invalidated ours. */
	for (cell_t i = 0; i < params[2]; i++) {
		if (phys[i] != 0xABC0 + i) {
			pContext->HeapPop(local);
			return pContext->ThrowNativeError("outer heap buffer clobbered at %d (0x%X)", i,
			                                  phys[i]);
		}
	}

	err = pContext->HeapPop(local);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("outer HeapPop failed after reentry (%d)", err);

	return callback_result;
}

// native Handle V8Test_CreateHandle();
static cell_t V8Test_CreateHandle(IPluginContext *pContext, const cell_t *params)
{
	int *payload = (int *)malloc(sizeof(int));
	*payload = 0x5A5A;

	Handle_t handle = handlesys->CreateHandle(g_TestHandleType, payload, pContext->GetIdentity(),
	                                          myself->GetIdentity(), NULL);
	if (handle == BAD_HANDLE) {
		free(payload);
		return pContext->ThrowNativeError("Could not create handle");
	}
	return handle;
}

// native int V8Test_ReadHandle(Handle handle);
static cell_t V8Test_ReadHandle(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity sec(pContext->GetIdentity(), myself->GetIdentity());
	int *payload = NULL;

	HandleError err = handlesys->ReadHandle((Handle_t)params[1], g_TestHandleType, &sec,
	                                        (void **)&payload);
	if (err != HandleError_None)
		return pContext->ThrowNativeError("Invalid handle (error %d)", err);

	return *payload;
}

// native int V8Test_CallFunction(Function callback, int value, const char[] text);
static cell_t V8Test_CallFunction(IPluginContext *pContext, const cell_t *params)
{
	IPluginFunction *fun = pContext->GetFunctionById((funcid_t)params[1]);
	if (fun == NULL)
		return pContext->ThrowNativeError("Invalid function id");

	if (!fun->IsRunnable())
		return pContext->ThrowNativeError("Function is not runnable");

	char *text;
	pContext->LocalToString(params[3], &text);

	// The old exception-handling API must keep working.
	DetectExceptions eh(pContext);

	char buffer[64];
	strncpy(buffer, text, sizeof(buffer) - 1);
	buffer[sizeof(buffer) - 1] = 0;

	cell_t result = 0;
	fun->PushCell(params[2]);
	fun->PushStringEx(buffer, sizeof(buffer), SM_PARAM_STRING_COPY, SM_PARAM_COPYBACK);
	int err = fun->Execute(&result);

	if (eh.HasException())
		return pContext->ThrowNativeError("Callback threw: %s", eh.Message());
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("Callback failed (%d)", err);

	smutils->LogMessage(myself, "callback copied back \"%s\"", buffer);
	return result;
}

// native int V8Test_WhoAmI(char[] out, int maxlen);
static cell_t V8Test_WhoAmI(IPluginContext *pContext, const cell_t *params)
{
	// The deprecated sp_context_t round trip must still resolve the plugin.
	IPlugin *plugin = plsys->FindPluginByContext(pContext->GetContext());
	if (plugin == NULL)
		return pContext->ThrowNativeError("FindPluginByContext failed");

	if (plugin->GetIdentity() != pContext->GetIdentity())
		return pContext->ThrowNativeError("Identity mismatch");

	if (plugin->GetBaseContext() != pContext)
		return pContext->ThrowNativeError("GetBaseContext mismatch");

	if (plugin->GetRuntime() != pContext->GetRuntime())
		return pContext->ThrowNativeError("GetRuntime mismatch");

	pContext->StringToLocalUTF8(params[1], params[2], plugin->GetFilename(), NULL);
	return 1;
}

// native int V8Test_FireForward(int value);
static cell_t V8Test_FireForward(IPluginContext *pContext, const cell_t *params)
{
	if (g_pTestForward == NULL)
		return pContext->ThrowNativeError("No forward");

	cell_t result = 0;
	cell_t extra = 0x1234;

	g_pTestForward->PushCell(params[1]);
	g_pTestForward->PushString(NULL);     /* NULL_STRING */
	g_pTestForward->PushArray(NULL, 3);   /* NULL_VECTOR */
	g_pTestForward->PushCell(extra);      /* varargs slot */

	int err = g_pTestForward->Execute(&result);
	if (err != SP_ERROR_NONE)
		return pContext->ThrowNativeError("Forward failed (%d)", err);

	return result;
}

// native int V8Test_Format(char[] out, int maxlen, const char[] fmt, any ...);
static cell_t V8Test_Format(IPluginContext *pContext, const cell_t *params)
{
	char buffer[512];
	size_t written = smutils->FormatString(buffer, sizeof(buffer), pContext, params, 3);

	pContext->StringToLocalUTF8(params[1], params[2], buffer, NULL);
	return (cell_t)written;
}

// native bool V8Test_PageMemory();
static cell_t V8Test_PageMemory(IPluginContext *pContext, const cell_t *params)
{
	ISourcePawnEngine *engine = smutils->GetScriptingEngine();
	if (engine == NULL)
		return pContext->ThrowNativeError("No scripting engine");

	void *page = engine->AllocatePageMemory(256);
	if (page == NULL)
		return pContext->ThrowNativeError("AllocatePageMemory failed");

	engine->SetReadWrite(page);
	memset(page, 0x90, 256);
	engine->SetReadExecute(page);
	engine->FreePageMemory(page);

	if (engine->GetEngineAPIVersion() == 0)
		return pContext->ThrowNativeError("Bad engine API version");

	return 1;
}

// native int V8Test_TestFeature(const char[] name);
static cell_t V8Test_TestFeature(IPluginContext *pContext, const cell_t *params)
{
	char *name;
	pContext->LocalToString(params[1], &name);

	return (cell_t)g_pShareSys->TestFeature(pContext->GetRuntime(), FeatureType_Native, name);
}

/* ------------------------------------------------------------------------- */

static const sp_nativeinfo_t s_Natives[] = {
	{"V8Test_Strings",        V8Test_Strings},
	{"V8Test_SumArray",       V8Test_SumArray},
	{"V8Test_IsNullString",   V8Test_IsNullString},
	{"V8Test_IsNullVector",   V8Test_IsNullVector},
	{"V8Test_Throw",          V8Test_Throw},
	{"V8Test_Heap",           V8Test_Heap},
	{"V8Test_HeapInner",      V8Test_HeapInner},
	{"V8Test_HeapReentrant",  V8Test_HeapReentrant},
	{"V8Test_CreateHandle",   V8Test_CreateHandle},
	{"V8Test_ReadHandle",     V8Test_ReadHandle},
	{"V8Test_CallFunction",   V8Test_CallFunction},
	{"V8Test_WhoAmI",         V8Test_WhoAmI},
	{"V8Test_FireForward",    V8Test_FireForward},
	{"V8Test_Format",         V8Test_Format},
	{"V8Test_PageMemory",     V8Test_PageMemory},
	{"V8Test_TestFeature",    V8Test_TestFeature},
	{NULL,                    NULL},
};

bool V8TestExt::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	g_TestHandleType = handlesys->CreateType("V8TestHandle", &s_HandleDispatch, 0, NULL, NULL,
	                                         myself->GetIdentity(), NULL);
	if (g_TestHandleType == 0) {
		snprintf(error, maxlength, "Could not create handle type");
		return false;
	}

	/* Param_VarArgs must survive the translation to Param_Any. */
	g_pTestForward = forwards->CreateForwardEx("V8Test_OnEvent", ET_Event, 4, NULL,
	                                           Param_Cell, Param_String, Param_Array,
	                                           Param_VarArgs);
	if (g_pTestForward == NULL) {
		snprintf(error, maxlength, "Could not create forward");
		return false;
	}

	plsys->AddPluginsListener(this);
	sharesys->AddNatives(myself, s_Natives);
	sharesys->RegisterLibrary(myself, "v8test");
	return true;
}

void V8TestExt::SDK_OnAllLoaded()
{
	smutils->LogMessage(myself, "v8 compat test extension loaded (engine \"%s\")",
	                    smutils->GetScriptingEngine() ? "present" : "missing");
}

void V8TestExt::SDK_OnUnload()
{
	if (g_pTestForward != NULL) {
		forwards->ReleaseForward(g_pTestForward);
		g_pTestForward = NULL;
	}

	plsys->RemovePluginsListener(this);

	if (g_TestHandleType != 0) {
		handlesys->RemoveType(g_TestHandleType, myself->GetIdentity());
		g_TestHandleType = 0;
	}
}

void V8TestExt::OnPluginLoaded(IPlugin *plugin)
{
	uint32_t index = 0;
	const char *has_info = "no";

	IPluginRuntime *runtime = plugin->GetRuntime();
	if (runtime != NULL && runtime->FindPubvarByName("myinfo", &index) == SP_ERROR_NONE)
		has_info = "yes";

	/* A forward from CreateForwardEx is private, so its callees are attached by
	 * hand. That is what exercises IChangeableForward::AddFunction and the
	 * old GetFunctionByName on the plugin's context. */
	IPluginContext *pContext = plugin->GetBaseContext();
	if (g_pTestForward != NULL && pContext != NULL) {
		IPluginFunction *fun = pContext->GetFunctionByName("V8Test_OnEvent");
		if (fun != NULL) {
			g_pTestForward->AddFunction(fun);
			smutils->LogMessage(myself, "hooked V8Test_OnEvent in %s",
			                    plugin->GetFilename());
		}
	}

	smutils->LogMessage(myself, "plugin loaded: %s (myinfo: %s)", plugin->GetFilename(), has_info);
}

void V8TestExt::OnPluginUnloaded(IPlugin *plugin)
{
	if (g_pTestForward != NULL)
		g_pTestForward->RemoveFunctionsOfPlugin(plugin);

	smutils->LogMessage(myself, "plugin unloaded: %s", plugin->GetFilename());
}
