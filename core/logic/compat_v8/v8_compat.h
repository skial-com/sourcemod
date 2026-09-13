// vim: set ts=4 sw=4 tw=99 noet :
//
// Entry points for the ABI-version-8 extension compatibility layer.
//
// Every pointer of a changed type that an ABI-8 extension binary sees is an
// adapter object created here, and adapters are the only such pointers it ever
// sees. Unwrapping is therefore a plain cast with no registry lookup.
//
#ifndef _INCLUDE_SOURCEMOD_COMPAT_V8_COMPAT_H_
#define _INCLUDE_SOURCEMOD_COMPAT_V8_COMPAT_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include <amtl/am-refcounting.h>

#include "v8_sm_api.h"

// The "v8" namespace name exists in both SourcePawn and SourceMod, and core
// has a using-directive for both, so it cannot be named unqualified. Use these
// aliases instead.
namespace spv8 = SourcePawn::v8;
namespace smv8 = SourceMod::v8;

namespace sp {
class BaseRuntime;
} // namespace sp

class CForward;
class CPlugin;

class V8ContextAdapter;
class V8FunctionAdapter;
class V8PluginAdapter;
class V8RuntimeAdapter;

/**
 * Per-runtime compatibility state. Recreated lazily after an eviction, so it
 * never outlives the sp::BaseRuntime it points at.
 */
struct V8RuntimeCompat
{
	explicit V8RuntimeCompat(sp::BaseRuntime *runtime);
	~V8RuntimeCompat();

	V8RuntimeCompat(const V8RuntimeCompat &) = delete;
	V8RuntimeCompat &operator =(const V8RuntimeCompat &) = delete;

	// Null-safe, cached.
	spv8::IPluginFunction *Wrap(SourcePawn::IPluginFunction *fun);

	// Heap emulation (old HeapAlloc/HeapPop/HeapRelease).
	int HeapAlloc(unsigned int cells, cell_t *local_addr, cell_t **phys_addr);
	int HeapPop(cell_t local_addr);
	int HeapRelease(cell_t local_addr);

	/* Drops every allocation made above |mark|, closing the heap scope only if
	 * that empties the allocation stack. Callers that invoke into an ABI-8
	 * native take a mark first, so a nested native cannot free the allocations
	 * of the native that called into the plugin. */
	void ReleaseHeapTo(size_t mark);
	void ForceReleaseHeap();

	sp::BaseRuntime *rt;
	std::unique_ptr<V8ContextAdapter> ctx;
	std::unique_ptr<V8RuntimeAdapter> runtime;
	std::unordered_map<SourcePawn::IPluginFunction *, std::unique_ptr<V8FunctionAdapter>> funcs;

	// Local addresses handed out by HeapAlloc, oldest first.
	std::vector<cell_t> heap;
};

/**
 * Per-plugin compatibility state. Lives for the CPlugin's lifetime and thus
 * survives eviction; the runtime half is reset on eviction.
 */
struct V8PluginCompat
{
	explicit V8PluginCompat(CPlugin *plugin);
	~V8PluginCompat();

	V8PluginCompat(const V8PluginCompat &) = delete;
	V8PluginCompat &operator =(const V8PluginCompat &) = delete;

	std::unique_ptr<V8PluginAdapter> plugin;
	std::unique_ptr<V8RuntimeCompat> rt;
};

namespace V8Compat {

/** The IShareSys handed to an ABI-8 extension's OnExtensionLoad. */
SourceMod::IShareSys *ShareSys();

/**
 * Returns the interface an ABI-8 extension should receive in place of |real|,
 * or nullptr if the request must be refused.
 */
SourceMod::SMInterface *SubstituteInterface(const char *name, SourceMod::SMInterface *real);

/** Per-runtime state for a runtime owned by a CPlugin, or null. */
V8RuntimeCompat *ForRuntime(SourcePawn::IPluginRuntime *rt);

/* Wrapping (core object -> old-layout adapter). All are null-safe. */
spv8::IPluginContext *WrapCtx(SourcePawn::IPluginRuntime *rt);
spv8::IPluginRuntime *WrapRuntime(SourcePawn::IPluginRuntime *rt);
spv8::IPluginFunction *WrapFn(SourcePawn::IPluginFunction *fun);
smv8::IPlugin *WrapPlugin(SourceMod::IPlugin *plugin);
smv8::IForward *WrapForward(SourceMod::IForward *fwd);

/* Unwrapping (old-layout adapter -> core object). All are null-safe. */
sp::BaseRuntime *UnwrapCtx(spv8::IPluginContext *ctx);
sp::BaseRuntime *UnwrapRuntime(spv8::IPluginRuntime *rt);
SourcePawn::IPluginFunction *UnwrapFn(spv8::IPluginFunction *fun);
CForward *UnwrapForward(smv8::IForward *fwd);
CPlugin *UnwrapPlugin(smv8::IPlugin *plugin);

/** Called from CForwardManager::ReleaseForward just before the forward dies. */
void OnForwardReleased(CForward *fwd);

/** Wraps an old-style native function pointer as an INativeCallback. */
SourcePawn::INativeCallback *MakeNativeCallback(SPVM_NATIVE_FUNC fn);

/** The substituted singleton interfaces handed to ABI-8 extensions. */
SourceMod::SMInterface *ForwardManager();
SourceMod::SMInterface *PluginManager();
SourceMod::SMInterface *SourceModIface();

/** The old ISourcePawnEngine / Engine2 / Environment singletons. */
spv8::ISourcePawnEngine *EngineV1();
spv8::ISourcePawnEngine2 *EngineV2();
spv8::ISourcePawnEnvironment *Env();

/** Remaps the old SM_PARAM_STRING_* bits onto the current ones. */
int RemapStringFlags(int sz_flags);

} // namespace V8Compat

#endif // _INCLUDE_SOURCEMOD_COMPAT_V8_COMPAT_H_
