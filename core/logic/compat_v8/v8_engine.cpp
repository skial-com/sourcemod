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

#include <stdlib.h>

#include <vector>

#include <amtl/am-string.h>

#include "v8_compat.h"

#include "common_logic.h"
#include <environment.h>

using namespace SourceMod;
using namespace SourcePawn;

/**
 * The old ISourcePawnEngine (15 slots, no virtual destructor).
 */
class V8EngineV1Adapter final : public spv8::ISourcePawnEngine
{
public:
	spv8::sp_plugin_t *LoadFromFilePointer(FILE *fp, int *err) override {
		if (err)
			*err = SP_ERROR_ABORTED;
		return nullptr;
	}
	spv8::sp_plugin_t *LoadFromMemory(void *base, spv8::sp_plugin_t *plugin, int *err) override {
		if (err)
			*err = SP_ERROR_ABORTED;
		return nullptr;
	}
	int FreeFromMemory(spv8::sp_plugin_t *plugin) override {
		return SP_ERROR_ABORTED;
	}
	void *BaseAlloc(size_t size) override {
		return malloc(size);
	}
	void BaseFree(void *memory) override {
		free(memory);
	}
	void *ExecAlloc(size_t size) override {
		return g_pPawnEnv->AllocatePageMemory(size);
	}
	void ExecFree(void *address) override {
		g_pPawnEnv->FreePageMemory(address);
	}
	spv8::IDebugListener *SetDebugListener(spv8::IDebugListener *listener) override {
		return nullptr;
	}
	unsigned int GetContextCallCount() override {
		return 0;
	}
	unsigned int GetEngineAPIVersion() override {
		return spv8::kV8ApiVersion;
	}
	void *AllocatePageMemory(size_t size) override {
		return g_pPawnEnv->AllocatePageMemory(size);
	}
	void SetReadWrite(void *ptr) override {
		g_pPawnEnv->SetReadWrite(ptr);
	}
	void SetReadExecute(void *ptr) override {
		g_pPawnEnv->SetReadExecute(ptr);
	}
	void FreePageMemory(void *ptr) override {
		g_pPawnEnv->FreePageMemory(ptr);
	}
	int SetDebugBreakHandler(SPVM_DEBUGBREAK handler) override {
		return SP_ERROR_ABORTED;
	}
};

/**
 * The old ISourcePawnEngine2 (22 slots, no virtual destructor).
 */
class V8EngineV2Adapter final : public spv8::ISourcePawnEngine2
{
public:
	unsigned int GetAPIVersion() override {
		return spv8::kV8Engine2ApiVersion;
	}
	const char *GetEngineName() override {
		return "SourcePawn 2 (legacy ABI v8 compatibility)";
	}
	const char *GetVersionString() override {
		return "SourcePawn 2";
	}
	spv8::ICompilation *StartCompilation() override {
		return nullptr;
	}
	spv8::IPluginRuntime *LoadPlugin(spv8::ICompilation *co, const char *file, int *err) override {
		if (err)
			*err = SP_ERROR_ABORTED;
		return nullptr;
	}
	SPVM_NATIVE_FUNC CreateFakeNative(SPVM_FAKENATIVE_FUNC callback, void *data) override {
		return nullptr;
	}
	void DestroyFakeNative(SPVM_NATIVE_FUNC func) override {
	}
	spv8::IDebugListener *SetDebugListener(spv8::IDebugListener *listener) override {
		return nullptr;
	}
	void SetProfiler(spv8::IProfiler *profiler) override {
	}
	const char *GetErrorString(int err) override {
		return g_pPawnEnv->GetErrorString(err);
	}
	bool Initialize() override {
		return true;
	}
	void Shutdown() override {
	}
	spv8::IPluginRuntime *CreateEmptyRuntime(const char *name, uint32_t memory) override {
		return nullptr;
	}
	bool InstallWatchdogTimer(size_t timeout_ms) override {
		return false;
	}
	bool SetJitEnabled(bool enabled) override {
		return false;
	}
	bool IsJitEnabled() override {
		return true;
	}
	void EnableProfiling() override {
	}
	void DisableProfiling() override {
	}
	void SetProfilingTool(spv8::IProfilingTool *tool) override {
	}
	spv8::IPluginRuntime *LoadBinaryFromFile(const char *file, char *error,
	                                         size_t maxlength) override {
		if (error && maxlength)
			ke::SafeStrcpy(error, maxlength, "Not supported");
		return nullptr;
	}
	spv8::ISourcePawnEnvironment *Environment() override {
		return V8Compat::Env();
	}
	spv8::IPluginRuntime *LoadBinaryFromMemory(const char *file, uint8_t *addr, size_t size,
	                                           void (*dtor)(uint8_t *), char *error,
	                                           size_t maxlength) override {
		if (dtor)
			dtor(addr);
		if (error && maxlength)
			ke::SafeStrcpy(error, maxlength, "Not supported");
		return nullptr;
	}
};

/**
 * The old ISourcePawnEnvironment (virtual destructor + 10 slots).
 *
 * Old ExceptionHandler objects cannot be linked into the real environment's
 * handler chain (their layout carries a vptr and the real chain walks real
 * handlers), so each old handling scope is mirrored by a real handler held in a
 * heap-allocated Scope. Old handlers are always stack objects, so the two
 * stacks stay in lockstep.
 */
class V8EnvironmentAdapter final : public spv8::ISourcePawnEnvironment
{
	// Gives us write access to the real handler's catch_ flag.
	struct EhAccess : public ExceptionHandler
	{
		using ExceptionHandler::ExceptionHandler;

		void set_catch(bool value) {
			catch_ = value;
		}
	};

	struct Scope
	{
		explicit Scope(spv8::ExceptionHandler *handler)
			: old(handler),
			  eh(g_pPawnEnv)
		{
		}

		spv8::ExceptionHandler *old;
		EhAccess eh;
	};

public:
	int ApiVersion() override {
		return (int)spv8::kV8ApiVersion;
	}
	spv8::ISourcePawnEngine *APIv1() override {
		return V8Compat::EngineV1();
	}
	spv8::ISourcePawnEngine2 *APIv2() override {
		return V8Compat::EngineV2();
	}
	void Shutdown() override {
		// Core owns the environment's lifetime.
	}

	void EnterExceptionHandlingScope(spv8::ExceptionHandler *handler) override {
		// The real handler enters the real scope in its constructor.
		stack_.push_back(new Scope(handler));
	}

	void LeaveExceptionHandlingScope(spv8::ExceptionHandler *handler) override {
		if (stack_.empty())
			return;

		// Old handlers are stack objects, so this must be the top one.
		assert(stack_.back()->old == handler);
		if (stack_.back()->old != handler)
			return;

		Scope *scope = stack_.back();
		stack_.pop_back();

		// Rethrow()/DetectExceptions have set their final catch_ state by the
		// time the old handler is destroyed; mirror it before the real handler
		// leaves the real scope in its destructor.
		scope->eh.set_catch(handler->WillCatch());
		delete scope;
	}

	bool HasPendingException(const spv8::ExceptionHandler *handler) override {
		Scope *scope = Find(handler);
		if (!scope)
			return g_pPawnEnv->hasPendingException();
		return g_pPawnEnv->HasPendingException(&scope->eh);
	}

	const char *GetPendingExceptionMessage(const spv8::ExceptionHandler *handler) override {
		// The old Message() did not check for an exception first, but the real
		// environment asserts that one is pending.
		Scope *scope = Find(handler);
		if (!scope || !g_pPawnEnv->HasPendingException(&scope->eh))
			return nullptr;
		return g_pPawnEnv->GetPendingExceptionMessage(&scope->eh);
	}

	bool EnableDebugBreak() override {
		return false;
	}
	void SetDebugMetadataFlags(int flags) override {
	}

private:
	Scope *Find(const spv8::ExceptionHandler *handler) {
		for (size_t i = stack_.size(); i > 0; i--) {
			if (stack_[i - 1]->old == handler)
				return stack_[i - 1];
		}
		return nullptr;
	}

private:
	std::vector<Scope *> stack_;
};

static V8EngineV1Adapter sV8EngineV1;
static V8EngineV2Adapter sV8EngineV2;
static V8EnvironmentAdapter sV8Environment;

spv8::ISourcePawnEngine *V8Compat::EngineV1()
{
	return &sV8EngineV1;
}

spv8::ISourcePawnEngine2 *V8Compat::EngineV2()
{
	return &sV8EngineV2;
}

spv8::ISourcePawnEnvironment *V8Compat::Env()
{
	return &sV8Environment;
}
