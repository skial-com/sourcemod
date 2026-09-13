// vim: set ts=4 sw=4 tw=99 noet :
// =============================================================================
// SourceMod
// Copyright (C) 2004-2026 AlliedModders LLC.  All rights reserved.
// =============================================================================
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 3.0, as published by the
// Free Software Foundation.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program.  If not, see <http://www.gnu.org/licenses/>.
//
// As a special exception, AlliedModders LLC gives you permission to link the
// code of this program (as well as its derivative works) to "Half-Life 2," the
// "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
// by the Valve Corporation.  You must obey the GNU General Public License in
// all respects for all other code used.  Additionally, AlliedModders LLC grants
// this exception to all derivative works.  AlliedModders LLC defines further
// exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
// or <http://www.sourcemod.net/license.php>.

#include <stdarg.h>
#include <string.h>

#include "v8_compat.h"

#include "common_logic.h"
#include "ShareSys.h"
#include <IDBDriver.h>
#include <ISourceMod.h>
#include <base-runtime.h>

using namespace SourceMod;
using namespace SourcePawn;

/**
 * IShareSys's vtable is unchanged, so this derives from the real interface and
 * only translates pointers at the boundary.
 */
class V8ShareSysAdapter final : public IShareSys
{
public:
	bool AddInterface(IExtension *myself, SMInterface *iface) override {
		return g_ShareSys.AddInterface(myself, iface);
	}
	bool RequestInterface(const char *iface_name, unsigned int iface_vers, IExtension *myself,
	                      SMInterface **pIface) override {
		return g_ShareSys.RequestInterface(iface_name, iface_vers, myself, pIface);
	}
	void AddNatives(IExtension *myself, const sp_nativeinfo_t *natives) override {
		g_ShareSys.AddNatives(myself, natives);
	}
	IdentityType_t CreateIdentType(const char *name) override {
		return g_ShareSys.CreateIdentType(name);
	}
	IdentityType_t FindIdentType(const char *name) override {
		return g_ShareSys.FindIdentType(name);
	}
	IdentityToken_t *CreateIdentity(IdentityType_t type, void *ptr) override {
		return g_ShareSys.CreateIdentity(type, ptr);
	}
	void DestroyIdentType(IdentityType_t type) override {
		g_ShareSys.DestroyIdentType(type);
	}
	void DestroyIdentity(IdentityToken_t *identity) override {
		g_ShareSys.DestroyIdentity(identity);
	}
	void AddDependency(IExtension *myself, const char *filename, bool require,
	                   bool autoload) override {
		g_ShareSys.AddDependency(myself, filename, require, autoload);
	}
	void RegisterLibrary(IExtension *myself, const char *name) override {
		g_ShareSys.RegisterLibrary(myself, name);
	}
	void OverrideNatives(IExtension *myself, const sp_nativeinfo_t *natives) override {
		g_ShareSys.OverrideNatives(myself, natives);
	}
	void AddCapabilityProvider(IExtension *myself, IFeatureProvider *provider,
	                           const char *name) override {
		g_ShareSys.AddCapabilityProvider(myself, provider, name);
	}
	void DropCapabilityProvider(IExtension *myself, IFeatureProvider *provider,
	                            const char *name) override {
		g_ShareSys.DropCapabilityProvider(myself, provider, name);
	}
	FeatureStatus TestFeature(IPluginRuntime *rt, FeatureType type, const char *name) override {
		// An ABI-8 extension only ever holds V8RuntimeAdapter pointers here.
		sp::BaseRuntime *real =
			V8Compat::UnwrapRuntime(reinterpret_cast<spv8::IPluginRuntime *>(rt));
		return g_ShareSys.TestFeature(real, type, name);
	}
};

/**
 * ISourceMod's vtable is unchanged as well.
 */
class V8SourceModAdapter final : public ISourceMod
{
public:
	const char *GetGamePath() const override {
		return g_pSM->GetGamePath();
	}
	const char *GetSourceModPath() const override {
		return g_pSM->GetSourceModPath();
	}
	size_t BuildPath(PathType type, char *buffer, size_t maxlength, const char *format,
	                 ...) override {
		char fmtbuf[2048];
		va_list ap;
		va_start(ap, format);
		g_pSM->FormatArgs(fmtbuf, sizeof(fmtbuf), format, ap);
		va_end(ap);

		return g_pSM->BuildPath(type, buffer, maxlength, "%s", fmtbuf);
	}
	void LogMessage(IExtension *pExt, const char *format, ...) override {
		char buffer[2048];
		va_list ap;
		va_start(ap, format);
		g_pSM->FormatArgs(buffer, sizeof(buffer), format, ap);
		va_end(ap);

		g_pSM->LogMessage(pExt, "%s", buffer);
	}
	void LogError(IExtension *pExt, const char *format, ...) override {
		char buffer[2048];
		va_list ap;
		va_start(ap, format);
		g_pSM->FormatArgs(buffer, sizeof(buffer), format, ap);
		va_end(ap);

		g_pSM->LogError(pExt, "%s", buffer);
	}
	size_t FormatString(char *buffer, size_t maxlength, IPluginContext *pContext,
	                    const cell_t *params, unsigned int param) override {
		sp::BaseRuntime *rt =
			V8Compat::UnwrapCtx(reinterpret_cast<spv8::IPluginContext *>(pContext));
		if (!rt)
			return 0;
		return g_pSM->FormatString(buffer, maxlength, rt, params, param);
	}
	void *CreateDataPack() override {
		return g_pSM->CreateDataPack();
	}
	void FreeDataPack(void *pack) override {
		g_pSM->FreeDataPack(pack);
	}
	HandleType_t GetDataPackHandleType(bool readonly) override {
		return g_pSM->GetDataPackHandleType(readonly);
	}
	KeyValues *ReadKeyValuesHandle(Handle_t hndl, HandleError *err, bool root) override {
		return g_pSM->ReadKeyValuesHandle(hndl, err, root);
	}
	const char *GetGameFolderName() const override {
		return g_pSM->GetGameFolderName();
	}
	ISourcePawnEngine *GetScriptingEngine() override {
		return reinterpret_cast<ISourcePawnEngine *>(V8Compat::EngineV1());
	}
	IVirtualMachine *GetScriptingVM() override {
		return nullptr;
	}
	time_t GetAdjustedTime() override {
		return g_pSM->GetAdjustedTime();
	}
	unsigned int SetGlobalTarget(unsigned int index) override {
		return g_pSM->SetGlobalTarget(index);
	}
	unsigned int GetGlobalTarget() const override {
		return g_pSM->GetGlobalTarget();
	}
	void AddGameFrameHook(GAME_FRAME_HOOK hook) override {
		g_pSM->AddGameFrameHook(hook);
	}
	void RemoveGameFrameHook(GAME_FRAME_HOOK hook) override {
		g_pSM->RemoveGameFrameHook(hook);
	}
	size_t Format(char *buffer, size_t maxlength, const char *fmt, ...) override {
		va_list ap;
		va_start(ap, fmt);
		size_t len = g_pSM->FormatArgs(buffer, maxlength, fmt, ap);
		va_end(ap);
		return len;
	}
	size_t FormatArgs(char *buffer, size_t maxlength, const char *fmt, va_list ap) override {
		return g_pSM->FormatArgs(buffer, maxlength, fmt, ap);
	}
	void AddFrameAction(FRAMEACTION fn, void *data) override {
		g_pSM->AddFrameAction(fn, data);
	}
	const char *GetCoreConfigValue(const char *key) override {
		return g_pSM->GetCoreConfigValue(key);
	}
	int GetPluginId() override {
		return g_pSM->GetPluginId();
	}
	int GetShApiVersion() override {
		return g_pSM->GetShApiVersion();
	}
	bool IsMapRunning() override {
		return g_pSM->IsMapRunning();
	}
	void *FromPseudoAddress(uint32_t pseudoAddr) override {
		return g_pSM->FromPseudoAddress(pseudoAddr);
	}
	uint32_t ToPseudoAddress(void *addr) override {
		return g_pSM->ToPseudoAddress(addr);
	}
};

static V8ShareSysAdapter sV8ShareSys;
static V8SourceModAdapter sV8SourceMod;

IShareSys *V8Compat::ShareSys()
{
	return &sV8ShareSys;
}

SMInterface *V8Compat::SourceModIface()
{
	return &sV8SourceMod;
}

SMInterface *V8Compat::SubstituteInterface(const char *name, SMInterface *real)
{
	if (strcmp(name, SMINTERFACE_SOURCEMOD_NAME) == 0)
		return SourceModIface();
	if (strcmp(name, SMINTERFACE_PLUGINSYSTEM_NAME) == 0)
		return PluginManager();
	if (strcmp(name, SMINTERFACE_FORWARDMANAGER_NAME) == 0)
		return ForwardManager();

	/* IDBI needs no translation. Between ABI 8 and ABI 11 the only change to
	 * IDBDriver.h was the version bump from 9 to 10 and a "schemaName" field
	 * appended to DatabaseInfo; every vtable and every pre-existing field
	 * offset is unchanged. A legacy extension reading a modern DatabaseInfo
	 * simply stops at maxTimeout, and modern readers of a legacy-built
	 * DatabaseInfo are already guarded by "dbiVersion >= 10" checks. Refusing
	 * the interface instead is actively worse: the failed load dlclose()s the
	 * driver, and running a database driver's global destructors on a
	 * half-initialised extension crashes the server. */
	return real;
}
