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
#include <stdio.h>
#include <stdlib.h>

#include "v8_compat.h"

#include "common_logic.h"
#include <base-runtime.h>
#include <environment.h>

using namespace SourceMod;
using namespace SourcePawn;
using sp::ARRAY_PTR; // ARRAY_PTR moved into namespace sp (base-runtime.h) in sourcepawn 6797adb4

/**
 * Wraps a real IFrameIterator so that Context() hands back an old-layout
 * context adapter.
 */
class V8FrameIteratorAdapter final : public spv8::IFrameIterator
{
public:
	explicit V8FrameIteratorAdapter(SourcePawn::IFrameIterator *real)
		: real_(real)
	{
	}

	SourcePawn::IFrameIterator *real() const {
		return real_;
	}

	bool Done() const override {
		return real_->Done();
	}
	void Next() override {
		real_->Next();
	}
	void Reset() override {
		real_->Reset();
	}
	spv8::IPluginContext *Context() const override {
		return V8Compat::WrapCtx(real_->Context());
	}
	bool IsNativeFrame() const override {
		return real_->IsNativeFrame();
	}
	bool IsScriptedFrame() const override {
		return real_->IsScriptedFrame();
	}
	unsigned LineNumber() const override {
		return real_->LineNumber();
	}
	const char *FunctionName() const override {
		return real_->FunctionName();
	}
	const char *FilePath() const override {
		return real_->FilePath();
	}
	bool IsInternalFrame() const override {
		return real_->IsInternalFrame();
	}

private:
	SourcePawn::IFrameIterator *real_;
};

/**
 * The old 63-slot IPluginContext, over an sp::BaseRuntime.
 */
class V8ContextAdapter final : public spv8::IPluginContext
{
public:
	explicit V8ContextAdapter(V8RuntimeCompat *compat)
		: compat_(compat)
	{
	}

	sp::BaseRuntime *rt() const {
		return compat_->rt;
	}

public: // Deprecated/dead in the old VM too.
	spv8::IVirtualMachine *GetVirtualMachine() override {
		return nullptr;
	}
	spv8::IPluginDebugInfo *GetDebugInfo() override {
		return nullptr;
	}
	int SetDebugBreak(void *newpfn, void *oldpfn) override {
		return SP_ERROR_ABORTED;
	}
	int PushCell(cell_t value) override {
		return SP_ERROR_ABORTED;
	}
	int PushCellArray(cell_t *local_addr, cell_t **phys_addr, cell_t array[],
	                  unsigned int numcells) override {
		return SP_ERROR_ABORTED;
	}
	int PushString(cell_t *local_addr, char **phys_addr, const char *string) override {
		return SP_ERROR_ABORTED;
	}
	int PushCellsFromArray(cell_t array[], unsigned int numcells) override {
		return SP_ERROR_ABORTED;
	}
	int BindNatives(const sp_nativeinfo_t *natives, unsigned int num, int overwrite) override {
		return SP_ERROR_ABORTED;
	}
	int BindNative(const sp_nativeinfo_t *native) override {
		return SP_ERROR_ABORTED;
	}
	int BindNativeToAny(SPVM_NATIVE_FUNC native) override {
		return SP_ERROR_ABORTED;
	}
	int BindNativeToIndex(uint32_t index, SPVM_NATIVE_FUNC native) override {
		return SP_ERROR_ABORTED;
	}
	int Execute(uint32_t code_addr, cell_t *result) override {
		return SP_ERROR_ABORTED;
	}
	int Execute2(spv8::IPluginFunction *function, const cell_t *params, unsigned int num_params,
	             cell_t *result) override {
		rt()->ReportErrorNumber(SP_ERROR_ABORTED);
		return SP_ERROR_ABORTED;
	}

public:
	// Old code does plsys->FindPluginByContext(ctx->GetContext()); the old VM
	// returned itself cast to the opaque type, so we do the same.
	sp_context_t *GetContext() override {
		return reinterpret_cast<sp_context_t *>(this);
	}

	bool IsDebugging() override {
		return rt()->IsDebugging();
	}

	int HeapAlloc(unsigned int cells, cell_t *local_addr, cell_t **phys_addr) override {
		return compat_->HeapAlloc(cells, local_addr, phys_addr);
	}
	int HeapPop(cell_t local_addr) override {
		return compat_->HeapPop(local_addr);
	}
	int HeapRelease(cell_t local_addr) override {
		return compat_->HeapRelease(local_addr);
	}

	int FindNativeByName(const char *name, uint32_t *index) override {
		return rt()->FindNativeByName(name, index);
	}
	int GetNativeByIndex(uint32_t index, sp_native_t **native) override {
		const sp_native_t *ntv = rt()->GetNative(index);
		if (!ntv)
			return SP_ERROR_INDEX;
		if (native)
			*native = const_cast<sp_native_t *>(ntv);
		return SP_ERROR_NONE;
	}
	uint32_t GetNativesNum() override {
		return rt()->GetNativesNum();
	}
	int FindPublicByName(const char *name, uint32_t *index) override {
		return rt()->FindPublicByName(name, index);
	}
	int GetPublicByIndex(uint32_t index, sp_public_t **publicptr) override {
		return rt()->GetPublicByIndex(index, publicptr);
	}
	uint32_t GetPublicsNum() override {
		return rt()->GetPublicsNum();
	}
	int GetPubvarByIndex(uint32_t index, sp_pubvar_t **pubvar) override {
		return rt()->GetPubvarByIndex(index, pubvar);
	}
	int FindPubvarByName(const char *name, uint32_t *index) override {
		return rt()->FindPubvarByName(name, index);
	}
	int GetPubvarAddrs(uint32_t index, cell_t *local_addr, cell_t **phys_addr) override {
		return rt()->GetPubvarAddrs(index, local_addr, phys_addr);
	}
	uint32_t GetPubVarsNum() override {
		return rt()->GetPubVarsNum();
	}
	int LocalToPhysAddr(cell_t local_addr, cell_t **phys_addr) override {
		return rt()->LocalToPhysAddr(local_addr, phys_addr);
	}
	int LocalToString(cell_t local_addr, char **addr) override {
		return rt()->LocalToString(local_addr, addr);
	}
	int StringToLocal(cell_t local_addr, size_t bytes, const char *source) override {
		return rt()->StringToLocal(local_addr, bytes, source);
	}
	int StringToLocalUTF8(cell_t local_addr, size_t maxbytes, const char *source,
	                      size_t *wrtnbytes) override {
		return rt()->StringToLocalUTF8(local_addr, maxbytes, source, wrtnbytes);
	}

	cell_t ThrowNativeErrorEx(int error, const char *msg, ...) override {
		if (!msg) {
			rt()->ReportErrorNumber(error);
			return 0;
		}

		char buffer[1024];
		va_list ap;
		va_start(ap, msg);
		vsnprintf(buffer, sizeof(buffer), msg, ap);
		va_end(ap);

		rt()->ThrowNativeErrorEx(error, "%s", buffer);
		return 0;
	}
	cell_t ThrowNativeError(const char *msg, ...) override {
		char buffer[1024];
		va_list ap;
		va_start(ap, msg);
		vsnprintf(buffer, sizeof(buffer), msg ? msg : "", ap);
		va_end(ap);

		return rt()->ThrowNativeError("%s", buffer);
	}

	spv8::IPluginFunction *GetFunctionByName(const char *public_name) override {
		return compat_->Wrap(rt()->GetFunctionByName(public_name));
	}
	spv8::IPluginFunction *GetFunctionById(funcid_t func_id) override {
		return compat_->Wrap(rt()->GetFunctionById(func_id));
	}
	IdentityToken_t *GetIdentity() override {
		return rt()->GetIdentity();
	}
	cell_t *GetNullRef(spv8::SP_NULL_TYPE type) override {
		return rt()->GetNullRef(static_cast<SourcePawn::SP_NULL_TYPE>((int)type));
	}
	int LocalToStringNULL(cell_t local_addr, char **addr) override {
		return rt()->LocalToStringNULL(local_addr, addr);
	}
	bool IsInExec() override {
		return rt()->IsInExec();
	}
	spv8::IPluginRuntime *GetRuntime() override;
	int GetLastNativeError() override {
		return rt()->GetLastNativeError();
	}
	cell_t *GetLocalParams() override {
		return rt()->GetLocalParams();
	}
	void SetKey(int k, void *value) override {
		rt()->SetKey(k, value);
	}
	bool GetKey(int k, void **value) override {
		return rt()->GetKey(k, value);
	}
	void ClearLastNativeError() override {
		if (g_pPawnEnv->hasPendingException())
			g_pPawnEnv->clearPendingException();
	}
	spv8::ISourcePawnEngine2 *APIv2() override {
		return V8Compat::EngineV2();
	}

	void ReportError(const char *fmt, ...) override {
		va_list ap;
		va_start(ap, fmt);
		rt()->ReportErrorVA(fmt, ap);
		va_end(ap);
	}
	void ReportErrorVA(const char *fmt, va_list ap) override {
		rt()->ReportErrorVA(fmt, ap);
	}
	void ReportFatalError(const char *fmt, ...) override {
		va_list ap;
		va_start(ap, fmt);
		rt()->ReportFatalErrorVA(fmt, ap);
		va_end(ap);
	}
	void ReportFatalErrorVA(const char *fmt, va_list ap) override {
		rt()->ReportFatalErrorVA(fmt, ap);
	}
	void ReportErrorNumber(int error) override {
		rt()->ReportErrorNumber(error);
	}
	cell_t BlamePluginError(spv8::IPluginFunction *pf, const char *msg, ...) override {
		char buffer[1024];
		va_list ap;
		va_start(ap, msg);
		vsnprintf(buffer, sizeof(buffer), msg ? msg : "", ap);
		va_end(ap);

		return rt()->BlamePluginError(V8Compat::UnwrapFn(pf), "%s", buffer);
	}

	spv8::IFrameIterator *CreateFrameIterator() override {
		IFrameIterator *real = rt()->CreateFrameIterator();
		if (!real)
			return nullptr;
		return new V8FrameIteratorAdapter(real);
	}
	void DestroyFrameIterator(spv8::IFrameIterator *it) override {
		if (!it)
			return;
		V8FrameIteratorAdapter *adapter = static_cast<V8FrameIteratorAdapter *>(it);
		rt()->DestroyFrameIterator(adapter->real());
		delete adapter;
	}

	bool HeapAlloc2dArray(unsigned int length, unsigned int stride, cell_t *local_addr,
	                      const cell_t *init) override {
		return rt()->HeapAlloc2dArray(length, stride, local_addr, init);
	}
	void EnterHeapScope() override {
		rt()->EnterHeapScope();
	}
	void LeaveHeapScope() override {
		rt()->LeaveHeapScope();
	}
	cell_t GetNullFunctionValue() override {
		return rt()->GetNullFunctionValue();
	}
	bool IsNullFunctionId(funcid_t func) override {
		return rt()->IsNullFunctionId(func);
	}
	bool GetFunctionByIdOrNull(funcid_t func, spv8::IPluginFunction **out) override {
		IPluginFunction *real = nullptr;
		if (!rt()->GetFunctionByIdOrNull(func, &real)) {
			if (out)
				*out = nullptr;
			return false;
		}
		if (out)
			*out = compat_->Wrap(real);
		return true;
	}
	spv8::IPluginFunction *GetFunctionByIdOrError(funcid_t func) override {
		return compat_->Wrap(rt()->GetFunctionByIdOrError(func));
	}

private:
	V8RuntimeCompat *compat_;
};

/**
 * The old 27-slot (plus virtual destructor) IPluginRuntime.
 */
class V8RuntimeAdapter final : public spv8::IPluginRuntime
{
public:
	explicit V8RuntimeAdapter(V8RuntimeCompat *compat)
		: compat_(compat)
	{
	}

	sp::BaseRuntime *rt() const {
		return compat_->rt;
	}

	spv8::IPluginDebugInfo *GetDebugInfo() override {
		return nullptr;
	}
	int FindNativeByName(const char *name, uint32_t *index) override {
		return rt()->FindNativeByName(name, index);
	}
	int GetNativeByIndex(uint32_t index, sp_native_t **native) override {
		const sp_native_t *ntv = rt()->GetNative(index);
		if (!ntv)
			return SP_ERROR_INDEX;
		if (native)
			*native = const_cast<sp_native_t *>(ntv);
		return SP_ERROR_NONE;
	}
	uint32_t GetNativesNum() override {
		return rt()->GetNativesNum();
	}
	int FindPublicByName(const char *name, uint32_t *index) override {
		return rt()->FindPublicByName(name, index);
	}
	int GetPublicByIndex(uint32_t index, sp_public_t **publicptr) override {
		return rt()->GetPublicByIndex(index, publicptr);
	}
	uint32_t GetPublicsNum() override {
		return rt()->GetPublicsNum();
	}
	int GetPubvarByIndex(uint32_t index, sp_pubvar_t **pubvar) override {
		return rt()->GetPubvarByIndex(index, pubvar);
	}
	int FindPubvarByName(const char *name, uint32_t *index) override {
		return rt()->FindPubvarByName(name, index);
	}
	int GetPubvarAddrs(uint32_t index, cell_t *local_addr, cell_t **phys_addr) override {
		return rt()->GetPubvarAddrs(index, local_addr, phys_addr);
	}
	uint32_t GetPubVarsNum() override {
		return rt()->GetPubVarsNum();
	}
	spv8::IPluginFunction *GetFunctionByName(const char *public_name) override {
		return compat_->Wrap(rt()->GetFunctionByName(public_name));
	}
	spv8::IPluginFunction *GetFunctionById(funcid_t func_id) override {
		return compat_->Wrap(rt()->GetFunctionById(func_id));
	}
	spv8::IPluginContext *GetDefaultContext() override {
		return compat_->ctx.get();
	}
	bool IsDebugging() override {
		return rt()->IsDebugging();
	}
	int ApplyCompilationOptions(spv8::ICompilation *co) override {
		return SP_ERROR_ABORTED;
	}
	void SetPauseState(bool paused) override {
		rt()->SetPauseState(paused);
	}
	bool IsPaused() override {
		return rt()->IsPaused();
	}
	size_t GetMemUsage() override {
		return rt()->GetMemUsage();
	}
	unsigned char *GetCodeHash() override {
		return rt()->GetCodeHash();
	}
	unsigned char *GetDataHash() override {
		return rt()->GetDataHash();
	}
	int UpdateNativeBinding(uint32_t index, SPVM_NATIVE_FUNC pfn, uint32_t flags,
	                        void *data) override {
		if (!pfn)
			return rt()->UpdateNativeBindingObject(index, nullptr, flags, data);
		return rt()->UpdateNativeBindingObject(index, V8Compat::MakeNativeCallback(pfn), flags,
		                                       data);
	}
	const sp_native_t *GetNative(uint32_t index) override {
		return rt()->GetNative(index);
	}
	const char *GetFilename() override {
		return rt()->GetFilename();
	}
	int UpdateNativeBindingObject(uint32_t index, spv8::INativeCallback *native, uint32_t flags,
	                              void *data) override;
	bool PerformFullValidation() override {
		return rt()->PerformFullValidation();
	}
	bool UsesDirectArrays() override {
		return rt()->UsesDirectArrays();
	}

private:
	V8RuntimeCompat *compat_;
};

/**
 * The old ICallable (8 slots) + IPluginFunction (10 slots) layout.
 */
class V8FunctionAdapter final : public spv8::IPluginFunction
{
public:
	V8FunctionAdapter(V8RuntimeCompat *compat, SourcePawn::IPluginFunction *real)
		: compat_(compat),
		  real_(real)
	{
	}

	SourcePawn::IPluginFunction *real() const {
		return real_;
	}

public: // ICallable
	int PushCell(cell_t cell) override {
		return real_->PushCell(cell);
	}
	int PushCellByRef(cell_t *cell, int flags) override {
		return real_->PushCellByRef(cell, flags);
	}
	int PushFloat(float number) override {
		return real_->PushFloat(number);
	}
	int PushFloatByRef(float *number, int flags) override {
		return real_->PushFloatByRef(number, flags);
	}
	int PushArray(cell_t *inarray, unsigned int cells, int flags) override {
		return real_->PushArray(inarray, cells, flags);
	}
	int PushString(const char *string) override {
		return real_->PushString(string);
	}
	int PushStringEx(char *buffer, size_t length, int sz_flags, int cp_flags) override {
		return real_->PushStringEx(buffer, length, V8Compat::RemapStringFlags(sz_flags), cp_flags);
	}
	void Cancel() override {
		real_->Cancel();
	}

public: // IPluginFunction
	int Execute(cell_t *result) override {
		return real_->Execute(result);
	}
	int CallFunction(const cell_t *params, unsigned int num_params, cell_t *result) override {
		compat_->rt->ReportErrorNumber(SP_ERROR_ABORTED);
		return SP_ERROR_ABORTED;
	}
	spv8::IPluginContext *GetParentContext() override {
		return compat_->ctx.get();
	}
	bool IsRunnable() override {
		return real_->IsRunnable();
	}
	funcid_t GetFunctionID() override {
		return real_->GetFunctionID();
	}
	int Execute2(spv8::IPluginContext *ctx, cell_t *result) override {
		return real_->Execute(result);
	}
	int CallFunction2(spv8::IPluginContext *ctx, const cell_t *params, unsigned int num_params,
	                  cell_t *result) override {
		compat_->rt->ReportErrorNumber(SP_ERROR_ABORTED);
		return SP_ERROR_ABORTED;
	}
	spv8::IPluginRuntime *GetParentRuntime() override {
		return compat_->runtime.get();
	}
	bool Invoke(cell_t *rval) override {
		return real_->Invoke(rval);
	}
	const char *DebugName() override {
		return real_->DebugName();
	}

private:
	V8RuntimeCompat *compat_;
	SourcePawn::IPluginFunction *real_;
};

/**
 * Bridges an old plain native function pointer into the new callback-object
 * binding mechanism.
 */
class V8NativeCallback final : public INativeCallback
{
	typedef cell_t (*V8NativeFn)(spv8::IPluginContext *, const cell_t *);

public:
	explicit V8NativeCallback(SPVM_NATIVE_FUNC fn)
		: fn_(reinterpret_cast<V8NativeFn>(fn))
	{
	}

	void AddRef() override {
		refcount_++;
	}
	void Release() override {
		assert(refcount_ > 0);
		if (--refcount_ == 0)
			delete this;
	}

	cell_t Invoke(IPluginContext *ctx, const cell_t *params) override {
		V8RuntimeCompat *compat = V8Compat::ForRuntime(ctx);
		if (!compat) {
			ctx->ReportError("Legacy extension native called from an unknown runtime");
			return 0;
		}

		// Old natives were allowed to leak heap allocations; SP2 heap scopes
		// must be balanced, so anything this native leaves behind is released
		// on the way out. The mark makes that reentrancy-safe: if this native
		// was reached from a plugin callback invoked by an outer ABI-8 native
		// on the same runtime, only our own allocations are dropped, and the
		// heap scope is closed by whichever native opened it.
		size_t mark = compat->heap.size();
		cell_t result = fn_(compat->ctx.get(), params);
		compat->ReleaseHeapTo(mark);
		return result;
	}

private:
	V8NativeFn fn_;
	size_t refcount_ = 0;
};

/**
 * Bridges an extension-provided old INativeCallback into the new interface.
 */
class V8CallbackShim final : public INativeCallback
{
public:
	explicit V8CallbackShim(spv8::INativeCallback *inner)
		: inner_(inner)
	{
		if (inner_)
			inner_->AddRef();
	}
	~V8CallbackShim() override {
		if (inner_)
			inner_->Release();
	}

	void AddRef() override {
		refcount_++;
	}
	void Release() override {
		assert(refcount_ > 0);
		if (--refcount_ == 0)
			delete this;
	}

	cell_t Invoke(IPluginContext *ctx, const cell_t *params) override {
		V8RuntimeCompat *compat = V8Compat::ForRuntime(ctx);
		if (!compat) {
			ctx->ReportError("Legacy extension native called from an unknown runtime");
			return 0;
		}

		// See V8NativeCallback::Invoke for why this is marked rather than a
		// wholesale release.
		size_t mark = compat->heap.size();
		cell_t result = inner_->Invoke(compat->ctx.get(), params);
		compat->ReleaseHeapTo(mark);
		return result;
	}

private:
	spv8::INativeCallback *inner_;
	size_t refcount_ = 0;
};

spv8::IPluginRuntime *V8ContextAdapter::GetRuntime()
{
	return compat_->runtime.get();
}

int V8RuntimeAdapter::UpdateNativeBindingObject(uint32_t index, spv8::INativeCallback *native,
                                               uint32_t flags, void *data)
{
	if (!native)
		return rt()->UpdateNativeBindingObject(index, nullptr, flags, data);
	return rt()->UpdateNativeBindingObject(index, new V8CallbackShim(native), flags, data);
}

/*************************
 * V8RuntimeCompat       *
 *************************/

V8RuntimeCompat::V8RuntimeCompat(sp::BaseRuntime *base_runtime)
	: rt(base_runtime)
{
	ctx.reset(new V8ContextAdapter(this));
	runtime.reset(new V8RuntimeAdapter(this));
}

V8RuntimeCompat::~V8RuntimeCompat()
{
	// Do not touch the runtime here: it may already be gone.
	heap.clear();
}

spv8::IPluginFunction *V8RuntimeCompat::Wrap(IPluginFunction *fun)
{
	if (!fun)
		return nullptr;

	auto iter = funcs.find(fun);
	if (iter != funcs.end())
		return iter->second.get();

	V8FunctionAdapter *adapter = new V8FunctionAdapter(this, fun);
	funcs[fun] = std::unique_ptr<V8FunctionAdapter>(adapter);
	return adapter;
}

int V8RuntimeCompat::HeapAlloc(unsigned int cells, cell_t *local_addr, cell_t **phys_addr)
{
	if (!cells)
		return SP_ERROR_PARAM;

	bool opened_scope = heap.empty();
	if (opened_scope)
		rt->EnterHeapScope();

	/* HeapAlloc2dArray is the only heap allocator the modern runtime exposes,
	 * so ask for a single row of |cells| and hand the caller that row. Asking
	 * for |cells| rows of one instead would give back the outer array's
	 * element vector, and writing flat cells over it corrupts the nested
	 * array handles that LeaveHeapScope later finalizes. */
	cell_t outer = 0;
	if (!rt->HeapAlloc2dArray(1, cells, &outer, nullptr)) {
		if (opened_scope)
			rt->LeaveHeapScope();
		return SP_ERROR_HEAPLOW;
	}

	ARRAY_PTR handle = nullptr;
	int err = rt->LocalToArrayPtr(outer, &handle);
	if (err != SP_ERROR_NONE) {
		if (opened_scope)
			rt->LeaveHeapScope();
		return err;
	}
	cell_t *outer_data = reinterpret_cast<cell_t *>(rt->GetArrayData(handle, nullptr));
	if (!outer_data) {
		if (opened_scope)
			rt->LeaveHeapScope();
		return SP_ERROR_INVALID_ADDRESS;
	}

	cell_t row_local = 0;
	cell_t *row_phys = nullptr;
	if (rt->AsV2()) {
		/* An index cell holds the row's own local address. */
		row_local = outer_data[0];
		ARRAY_PTR row_handle = nullptr;
		err = rt->LocalToArrayPtr(row_local, &row_handle);
		if (err != SP_ERROR_NONE) {
			if (opened_scope)
				rt->LeaveHeapScope();
			return err;
		}
		row_phys = reinterpret_cast<cell_t *>(rt->GetArrayData(row_handle, nullptr));
	} else {
		/* AMX indirection vectors hold a byte offset from the index cell. */
		row_phys = reinterpret_cast<cell_t *>(
			reinterpret_cast<char *>(outer_data) + outer_data[0]);
		row_local = outer + outer_data[0];
	}

	if (!row_phys) {
		if (opened_scope)
			rt->LeaveHeapScope();
		return SP_ERROR_INVALID_ADDRESS;
	}

	if (phys_addr)
		*phys_addr = row_phys;
	if (local_addr)
		*local_addr = row_local;

	heap.push_back(row_local);
	return SP_ERROR_NONE;
}

int V8RuntimeCompat::HeapPop(cell_t local_addr)
{
	if (heap.empty() || heap.back() != local_addr)
		return SP_ERROR_INVALID_ADDRESS;

	heap.pop_back();
	if (heap.empty())
		rt->LeaveHeapScope();
	return SP_ERROR_NONE;
}

int V8RuntimeCompat::HeapRelease(cell_t local_addr)
{
	size_t index = heap.size();
	for (size_t i = 0; i < heap.size(); i++) {
		if (heap[i] == local_addr) {
			index = i;
			break;
		}
	}

	if (index == heap.size())
		return SP_ERROR_INVALID_ADDRESS;

	// SP2 heap scopes free as a unit, so everything above |local_addr| goes
	// away with it; this matches HeapRelease's documented semantics.
	ReleaseHeapTo(index);
	return SP_ERROR_NONE;
}

void V8RuntimeCompat::ReleaseHeapTo(size_t mark)
{
	if (heap.size() <= mark)
		return;

	heap.resize(mark);

	// Only the allocation that opened the scope closes it.
	if (heap.empty())
		rt->LeaveHeapScope();
}

void V8RuntimeCompat::ForceReleaseHeap()
{
	ReleaseHeapTo(0);
}

/*************************
 * V8Compat entry points *
 *************************/

int V8Compat::RemapStringFlags(int sz_flags)
{
	int out = 0;
	if (sz_flags & spv8::kV8StringUtf8)
		out |= SM_PARAM_STRING_UTF8;
	if (sz_flags & spv8::kV8StringCopy)
		out |= SM_PARAM_STRING_COPY;
	if (sz_flags & spv8::kV8StringBinary)
		out |= SM_PARAM_STRING_BINARY;
	return out;
}

spv8::IPluginContext *V8Compat::WrapCtx(IPluginRuntime *rt)
{
	V8RuntimeCompat *compat = ForRuntime(rt);
	if (!compat)
		return nullptr;
	return compat->ctx.get();
}

spv8::IPluginRuntime *V8Compat::WrapRuntime(IPluginRuntime *rt)
{
	V8RuntimeCompat *compat = ForRuntime(rt);
	if (!compat)
		return nullptr;
	return compat->runtime.get();
}

spv8::IPluginFunction *V8Compat::WrapFn(IPluginFunction *fun)
{
	if (!fun)
		return nullptr;

	V8RuntimeCompat *compat = ForRuntime(fun->GetParentRuntime());
	if (!compat)
		return nullptr;
	return compat->Wrap(fun);
}

sp::BaseRuntime *V8Compat::UnwrapCtx(spv8::IPluginContext *ctx)
{
	if (!ctx)
		return nullptr;
	return static_cast<V8ContextAdapter *>(ctx)->rt();
}

sp::BaseRuntime *V8Compat::UnwrapRuntime(spv8::IPluginRuntime *rt)
{
	if (!rt)
		return nullptr;
	return static_cast<V8RuntimeAdapter *>(rt)->rt();
}

IPluginFunction *V8Compat::UnwrapFn(spv8::IPluginFunction *fun)
{
	if (!fun)
		return nullptr;
	return static_cast<V8FunctionAdapter *>(fun)->real();
}

INativeCallback *V8Compat::MakeNativeCallback(SPVM_NATIVE_FUNC fn)
{
	return new V8NativeCallback(fn);
}
