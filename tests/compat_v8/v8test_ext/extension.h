/**
 * v8 compatibility test extension.
 *
 * This source lives in the HEAD tree but is built only against the ABI-8
 * headers in the /home/lorph/tf2dev/sdk/sm-v8 worktree, so the resulting
 * binary reports GetExtensionVersion() == 8 and exercises the old SourcePawn
 * and SourceMod interfaces through core/logic/compat_v8.
 */
#ifndef _INCLUDE_SOURCEMOD_V8TEST_EXTENSION_H_
#define _INCLUDE_SOURCEMOD_V8TEST_EXTENSION_H_

#include "smsdk_ext.h"

class V8TestExt :
	public SDKExtension,
	public IPluginsListener
{
public: // SDKExtension
	bool SDK_OnLoad(char *error, size_t maxlength, bool late);
	void SDK_OnUnload();
	void SDK_OnAllLoaded();

public: // IPluginsListener
	void OnPluginLoaded(IPlugin *plugin);
	void OnPluginUnloaded(IPlugin *plugin);
};

extern V8TestExt g_V8TestExt;
extern IChangeableForward *g_pTestForward;
extern HandleType_t g_TestHandleType;

#endif // _INCLUDE_SOURCEMOD_V8TEST_EXTENSION_H_
