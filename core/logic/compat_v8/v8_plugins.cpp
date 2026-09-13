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

#include <vector>

#include "v8_compat.h"

#include "common_logic.h"
#include "PluginSys.h"
#include <base-runtime.h>

using namespace SourceMod;
using namespace SourcePawn;

/**
 * The old IPlugin layout (which still had GetContext and GetPluginStructure
 * in the middle of the vtable).
 */
class V8PluginAdapter final : public smv8::IPlugin
{
public:
	explicit V8PluginAdapter(CPlugin *plugin)
		: pl_(plugin)
	{
	}

	CPlugin *real() const {
		return pl_;
	}

	PluginType GetType() override {
		return pl_->GetType();
	}
	spv8::IPluginContext *GetBaseContext() override {
		return V8Compat::WrapCtx(pl_->runtime());
	}
	sp_context_t *GetContext() override {
		spv8::IPluginContext *ctx = V8Compat::WrapCtx(pl_->runtime());
		if (!ctx)
			return nullptr;
		return ctx->GetContext();
	}
	void *GetPluginStructure() override {
		return nullptr;
	}
	const sm_plugininfo_t *GetPublicInfo() override {
		return pl_->GetPublicInfo();
	}
	const char *GetFilename() override {
		return pl_->GetFilename();
	}
	bool IsDebugging() override {
		return pl_->IsDebugging();
	}
	PluginStatus GetStatus() override {
		return pl_->GetStatus();
	}
	bool SetPauseState(bool paused) override {
		return pl_->SetPauseState(paused);
	}
	unsigned int GetSerial() override {
		return pl_->GetSerial();
	}
	IdentityToken_t *GetIdentity() override {
		return pl_->GetIdentity();
	}
	bool SetProperty(const char *prop, void *ptr) override {
		return pl_->SetProperty(prop, ptr);
	}
	bool GetProperty(const char *prop, void **ptr, bool remove) override {
		return pl_->GetProperty(prop, ptr, remove);
	}
	spv8::IPluginRuntime *GetRuntime() override {
		return V8Compat::WrapRuntime(pl_->runtime());
	}
	IPhraseCollection *GetPhrases() override {
		return pl_->GetPhrases();
	}
	Handle_t GetMyHandle() override {
		return pl_->GetMyHandle();
	}

private:
	CPlugin *pl_;
};

/*****************************
 * Per-plugin compat storage *
 *****************************/

V8PluginCompat::V8PluginCompat(CPlugin *pl)
	: plugin(new V8PluginAdapter(pl))
{
}

V8PluginCompat::~V8PluginCompat()
{
}

V8PluginCompat *CPlugin::v8compat()
{
	if (!m_v8)
		m_v8.reset(new V8PluginCompat(this));
	return m_v8.get();
}

V8RuntimeCompat *V8Compat::ForRuntime(IPluginRuntime *rt)
{
	if (!rt)
		return nullptr;

	IPlugin *ipl = nullptr;
	if (!rt->GetKey(2, (void **)&ipl) || !ipl)
		return nullptr;

	CPlugin *pl = static_cast<CPlugin *>(ipl);
	if (!pl->runtime())
		return nullptr;

	V8PluginCompat *compat = pl->v8compat();
	if (!compat->rt)
		compat->rt.reset(new V8RuntimeCompat(pl->runtime()));
	return compat->rt.get();
}

smv8::IPlugin *V8Compat::WrapPlugin(IPlugin *plugin)
{
	if (!plugin)
		return nullptr;
	return static_cast<CPlugin *>(plugin)->v8compat()->plugin.get();
}

CPlugin *V8Compat::UnwrapPlugin(smv8::IPlugin *plugin)
{
	if (!plugin)
		return nullptr;
	return static_cast<V8PluginAdapter *>(plugin)->real();
}

/*********************
 * Plugin iterator   *
 *********************/

class V8PluginIteratorAdapter final : public smv8::IPluginIterator
{
public:
	explicit V8PluginIteratorAdapter(SourceMod::IPluginIterator *real)
		: real_(real)
	{
	}
	~V8PluginIteratorAdapter() override {
		if (real_)
			real_->Release();
	}

	bool MorePlugins() override {
		return real_->MorePlugins();
	}
	smv8::IPlugin *GetPlugin() override {
		return V8Compat::WrapPlugin(real_->GetPlugin());
	}
	void NextPlugin() override {
		real_->NextPlugin();
	}
	void Release() override {
		delete this;
	}

private:
	SourceMod::IPluginIterator *real_;
};

/*********************
 * Listener shim     *
 *********************/

/**
 * Registered with core on an extension's behalf; translates IPlugin pointers
 * into old-layout adapters.
 */
class V8ListenerShim final : public IPluginsListener
{
public:
	V8ListenerShim(smv8::IPluginsListener_V1 *impl, bool v1)
		: impl_(impl),
		  v1_(v1)
	{
	}

	bool matches(const smv8::IPluginsListener_V1 *impl) const {
		return impl_ == impl;
	}

	unsigned int GetApiVersion() const override {
		// The v2 listener was added with API v7; pin V1 shims to v6 exactly as
		// PluginsListenerV1Wrapper does.
		return v1_ ? 6 : SMINTERFACE_PLUGINSYSTEM_VERSION;
	}

	void OnPluginLoaded(IPlugin *plugin) override {
		impl_->OnPluginLoaded(V8Compat::WrapPlugin(plugin));
	}
	void OnPluginPauseChange(IPlugin *plugin, bool paused) override {
		impl_->OnPluginPauseChange(V8Compat::WrapPlugin(plugin), paused);
	}
	void OnPluginUnloaded(IPlugin *plugin) override {
		impl_->OnPluginUnloaded(V8Compat::WrapPlugin(plugin));
	}
	void OnPluginDestroyed(IPlugin *plugin) override {
		impl_->OnPluginDestroyed(V8Compat::WrapPlugin(plugin));
	}
	void OnPluginWillUnload(IPlugin *plugin) override {
		if (v1_)
			return;
		static_cast<smv8::IPluginsListener *>(impl_)->OnPluginWillUnload(
			V8Compat::WrapPlugin(plugin));
	}

private:
	smv8::IPluginsListener_V1 *impl_;
	bool v1_;
};

/*********************
 * Plugin manager    *
 *********************/

class V8PluginManagerAdapter final : public smv8::IPluginManager
{
public:
	smv8::IPlugin *LoadPlugin(const char *path, bool debug, PluginType type, char error[],
	                          size_t maxlength, bool *wasloaded) override {
		return V8Compat::WrapPlugin(
			g_PluginSys.LoadPlugin(path, debug, type, error, maxlength, wasloaded));
	}

	bool UnloadPlugin(smv8::IPlugin *plugin) override {
		CPlugin *pl = V8Compat::UnwrapPlugin(plugin);
		if (!pl)
			return false;
		return g_PluginSys.UnloadPlugin(pl);
	}

	smv8::IPlugin *FindPluginByContext(const sp_context_t *ctx) override {
		if (!ctx)
			return nullptr;

		// The only sp_context_t values an ABI-8 extension can hold came from
		// V8ContextAdapter::GetContext(), which returns the adapter itself.
		spv8::IPluginContext *adapter = reinterpret_cast<spv8::IPluginContext *>(
			const_cast<sp_context_t *>(ctx));
		sp::BaseRuntime *rt = V8Compat::UnwrapCtx(adapter);
		if (!rt)
			return nullptr;
		return V8Compat::WrapPlugin(g_PluginSys.FindPluginByContext(rt));
	}

	unsigned int GetPluginCount() override {
		return g_PluginSys.GetPluginCount();
	}

	smv8::IPluginIterator *GetPluginIterator() override {
		SourceMod::IPluginIterator *real = g_PluginSys.GetPluginIterator();
		if (!real)
			return nullptr;
		return new V8PluginIteratorAdapter(real);
	}

	void AddPluginsListener_V1(smv8::IPluginsListener_V1 *listener) override {
		AddShim(listener, true);
	}

	void RemovePluginsListener_V1(smv8::IPluginsListener_V1 *listener) override {
		RemoveShim(listener);
	}

	smv8::IPlugin *PluginFromHandle(Handle_t handle, HandleError *err) override {
		return V8Compat::WrapPlugin(g_PluginSys.PluginFromHandle(handle, err));
	}

	void AddPluginsListener(smv8::IPluginsListener *listener) override {
		AddShim(listener, false);
	}

	void RemovePluginsListener(smv8::IPluginsListener *listener) override {
		RemoveShim(listener);
	}

private:
	void AddShim(smv8::IPluginsListener_V1 *listener, bool v1) {
		if (!listener)
			return;

		std::unique_ptr<V8ListenerShim> shim(new V8ListenerShim(listener, v1));
		g_PluginSys.AddPluginsListener(shim.get());
		shims_.push_back(std::move(shim));
	}

	void RemoveShim(const smv8::IPluginsListener_V1 *listener) {
		for (size_t i = 0; i < shims_.size(); i++) {
			if (!shims_[i]->matches(listener))
				continue;

			g_PluginSys.RemovePluginsListener(shims_[i].get());
			shims_.erase(shims_.begin() + i);
			return;
		}
	}

private:
	std::vector<std::unique_ptr<V8ListenerShim>> shims_;
};

static V8PluginManagerAdapter sV8Plugins;

SMInterface *V8Compat::PluginManager()
{
	return &sV8Plugins;
}
