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

#include <unordered_map>
#include <vector>

#include "v8_compat.h"

#include "common_logic.h"
#include "ForwardSys.h"
#include "PluginSys.h"
#include <base-runtime.h>

using namespace SourceMod;
using namespace SourcePawn;

namespace {

/** One argument slot that the extension pushed as NULL. */
struct NullSlot
{
	uint32_t index;
	bool vector; /**< true = NULL_VECTOR, false = NULL_STRING */
};

/** Filler for NULL array pushes; the filter replaces the slot before use. */
cell_t sNullVectorFiller[3] = {0, 0, 0};

/**
 * Reproduces what the old CForward::Execute did for NULL pushes: for each
 * callee, substitute the callee's own NULL_VECTOR / NULL_STRING pubvar address.
 */
class V8NullFilter final : public IForwardFilter
{
public:
	explicit V8NullFilter(const std::vector<NullSlot> &slots)
		: slots_(slots)
	{
	}

	bool empty() const {
		return slots_.empty();
	}

	void Preprocess(IPluginFunction *fun, sp::CallArgs &args) override {
		IPluginRuntime *rt = fun->GetParentRuntime();
		if (!rt)
			return;

		for (size_t i = 0; i < slots_.size(); i++) {
			const NullSlot &slot = slots_[i];
			if (slot.index >= args.argc)
				continue;

			uint32_t index;
			const char *name = slot.vector ? "NULL_VECTOR" : "NULL_STRING";
			if (rt->FindPubvarByName(name, &index) != SP_ERROR_NONE)
				continue;

			cell_t local = 0;
			cell_t *phys = nullptr;
			if (rt->GetPubvarAddrs(index, &local, &phys) != SP_ERROR_NONE)
				continue;

			args.argv[slot.index].type = sp::CallArgs::ARG_CELL;
			args.argv[slot.index].flags = 0;
			args.argv[slot.index].u.value = local;
		}
	}

private:
	std::vector<NullSlot> slots_;
};

} // anonymous namespace

/**
 * The old IChangeableForward layout (ICallable + IForward + IChangeableForward)
 * over a real CForward.
 */
class V8ForwardAdapter final : public smv8::IChangeableForward
{
public:
	explicit V8ForwardAdapter(CForward *real)
		: real_(real),
		  pushed_(0)
	{
	}

	CForward *real() const {
		return real_;
	}

public: // ICallable
	int PushCell(cell_t cell) override {
		pushed_++;
		return real_->PushCell(cell);
	}
	int PushCellByRef(cell_t *cell, int flags) override {
		pushed_++;
		return real_->PushCellByRef(cell, flags);
	}
	int PushFloat(float number) override {
		pushed_++;
		return real_->PushFloat(number);
	}
	int PushFloatByRef(float *number, int flags) override {
		pushed_++;
		return real_->PushFloatByRef(number, flags);
	}
	int PushStringEx(char *buffer, size_t length, int sz_flags, int cp_flags) override {
		pushed_++;
		return real_->PushStringEx(buffer, length, V8Compat::RemapStringFlags(sz_flags), cp_flags);
	}
	void Cancel() override {
		nulls_.clear();
		pushed_ = 0;
		real_->Cancel();
	}

public: // IForward
	const char *GetForwardName() override {
		return real_->GetForwardName();
	}
	unsigned int GetFunctionCount() override {
		return real_->GetFunctionCount();
	}
	ExecType GetExecType() override {
		return real_->GetExecType();
	}

	int Execute(cell_t *result, smv8::IForwardFilter *filter) override {
		// Only one filter can be handed to CForward::Execute, and ours is
		// needed for NULL emulation, so an extension-provided filter is
		// ignored (the old header documented it as "do not use").
		V8NullFilter nulls(nulls_);
		nulls_.clear();
		pushed_ = 0;

		// Nothing below may touch |this|: a callee is allowed to release this
		// forward, which drops the adapter.
		CForward *real = real_;
		if (nulls.empty())
			return real->Execute(result, nullptr);
		return real->Execute(result, &nulls);
	}

	// NULL means "push each callee's NULL_VECTOR", which HEAD's CForward
	// rejects outright, so record the slot and fix it up in the filter.
	int PushArray(cell_t *inarray, unsigned int cells, int flags) override {
		if (!inarray) {
			if (cells != 3) {
				pushed_++;
				return real_->PushArray(nullptr, cells, flags);
			}

			NullSlot slot = {pushed_, true};
			nulls_.push_back(slot);
			pushed_++;
			return real_->PushArray(sNullVectorFiller, 3, 0);
		}

		pushed_++;
		return real_->PushArray(inarray, cells, flags);
	}

	int PushString(const char *string) override {
		if (!string) {
			NullSlot slot = {pushed_, false};
			nulls_.push_back(slot);
			pushed_++;
			return real_->PushString("");
		}

		pushed_++;
		return real_->PushString(string);
	}

public: // IChangeableForward
	bool RemoveFunction(spv8::IPluginFunction *func) override {
		return real_->RemoveFunction(V8Compat::UnwrapFn(func));
	}
	unsigned int RemoveFunctionsOfPlugin(smv8::IPlugin *plugin) override {
		CPlugin *pl = V8Compat::UnwrapPlugin(plugin);
		if (!pl)
			return 0;
		return real_->RemoveFunctionsOfPlugin(pl);
	}
	bool AddFunction(spv8::IPluginFunction *func) override {
		IPluginFunction *real = V8Compat::UnwrapFn(func);
		if (!real)
			return false;
		return real_->AddFunction(real);
	}
	bool AddFunction(spv8::IPluginContext *ctx, funcid_t index) override {
		sp::BaseRuntime *rt = V8Compat::UnwrapCtx(ctx);
		if (!rt)
			return false;
		return real_->AddFunction(rt, index);
	}
	bool RemoveFunction(spv8::IPluginContext *ctx, funcid_t index) override {
		sp::BaseRuntime *rt = V8Compat::UnwrapCtx(ctx);
		if (!rt)
			return false;
		return real_->RemoveFunction(rt, index);
	}

private:
	CForward *real_;
	uint32_t pushed_;
	std::vector<NullSlot> nulls_;
};

static std::unordered_map<CForward *, std::unique_ptr<V8ForwardAdapter>> sForwards;

smv8::IForward *V8Compat::WrapForward(IForward *fwd)
{
	if (!fwd)
		return nullptr;

	CForward *real = static_cast<CForward *>(fwd);
	auto iter = sForwards.find(real);
	if (iter != sForwards.end())
		return iter->second.get();

	V8ForwardAdapter *adapter = new V8ForwardAdapter(real);
	sForwards[real] = std::unique_ptr<V8ForwardAdapter>(adapter);
	return adapter;
}

CForward *V8Compat::UnwrapForward(smv8::IForward *fwd)
{
	if (!fwd)
		return nullptr;
	return static_cast<V8ForwardAdapter *>(fwd)->real();
}

void V8Compat::OnForwardReleased(CForward *fwd)
{
	sForwards.erase(fwd);
}

/**
 * The old IForwardManager. Reports the real interface name and version, so it
 * can be substituted for g_Forwards transparently.
 */
class V8ForwardManagerAdapter final : public smv8::IForwardManager
{
public:
	smv8::IForward *CreateForward(const char *name, ExecType et, unsigned int num_params,
	                              const smv8::ParamType *types, ...) override {
		SourceMod::ParamType converted[SP_MAX_EXEC_PARAMS];
		unsigned int count = 0;

		va_list ap;
		va_start(ap, types);
		bool ok = Convert(num_params, types, ap, converted, &count);
		va_end(ap);

		if (!ok)
			return nullptr;

		return V8Compat::WrapForward(g_Forwards.CreateForward(name, et, count, converted));
	}

	smv8::IChangeableForward *CreateForwardEx(const char *name, ExecType et, int num_params,
	                                          const smv8::ParamType *types, ...) override {
		if (num_params < 0)
			return nullptr;

		SourceMod::ParamType converted[SP_MAX_EXEC_PARAMS];
		unsigned int count = 0;

		va_list ap;
		va_start(ap, types);
		bool ok = Convert((unsigned int)num_params, types, ap, converted, &count);
		va_end(ap);

		if (!ok)
			return nullptr;

		SourceMod::IChangeableForward *real = g_Forwards.CreateForwardEx(name, et, (int)count, converted);
		if (!real)
			return nullptr;

		return static_cast<smv8::IChangeableForward *>(
			V8Compat::WrapForward(static_cast<SourceMod::IForward *>(real)));
	}

	smv8::IForward *FindForward(const char *name, smv8::IChangeableForward **ifchng) override {
		SourceMod::IChangeableForward *chng = nullptr;
		SourceMod::IForward *real = g_Forwards.FindForward(name, &chng);
		if (!real) {
			if (ifchng)
				*ifchng = nullptr;
			return nullptr;
		}

		smv8::IForward *wrapped = V8Compat::WrapForward(real);
		if (ifchng)
			*ifchng = chng ? static_cast<smv8::IChangeableForward *>(wrapped) : nullptr;
		return wrapped;
	}

	void ReleaseForward(smv8::IForward *forward) override {
		CForward *real = V8Compat::UnwrapForward(forward);
		if (!real)
			return;

		// The adapter is dropped by V8Compat::OnForwardReleased, which core
		// calls just before the forward dies (possibly deferred, if the forward
		// is still executing).
		g_Forwards.ReleaseForward(real);
	}

private:
	/**
	 * Reads num_params parameter types from |types| or the vararg stream,
	 * rewriting Param_VarArgs to Param_Any. Because HEAD rejects pushes beyond
	 * the declared parameter count, a varargs slot also pads the declaration out
	 * to SP_MAX_EXEC_PARAMS Param_Any slots.
	 */
	static bool Convert(unsigned int num_params, const smv8::ParamType *types, va_list ap,
	                    SourceMod::ParamType *out, unsigned int *count)
	{
		if (num_params > SP_MAX_EXEC_PARAMS)
			return false;

		bool varargs = false;
		for (unsigned int i = 0; i < num_params; i++) {
			int value = types ? (int)types[i] : va_arg(ap, int);
			if (value == (int)smv8::Param_VarArgs) {
				varargs = true;
				value = (int)smv8::Param_Any;
			}
			out[i] = (SourceMod::ParamType)value;
		}

		unsigned int total = num_params;
		if (varargs) {
			while (total < SP_MAX_EXEC_PARAMS)
				out[total++] = SourceMod::Param_Any;
		}

		*count = total;
		return true;
	}
};

static V8ForwardManagerAdapter sV8Forwards;

SMInterface *V8Compat::ForwardManager()
{
	return &sV8Forwards;
}
