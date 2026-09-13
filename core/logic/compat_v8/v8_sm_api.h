// vim: set ts=4 sw=4 tw=99 noet :
//
// Verbatim copies of the SourceMod forward and plugin interfaces as they
// existed at extension ABI version 8 (commit 22f1504ac). These declarations
// exist only so the compiler reproduces the exact vtable layout that an
// extension binary built against the old headers expects.
//
// Declaration order, the presence (or absence) of virtual destructors, and the
// set of virtual functions must never be changed.
//
#ifndef _INCLUDE_SOURCEMOD_COMPAT_V8_SM_API_H_
#define _INCLUDE_SOURCEMOD_COMPAT_V8_SM_API_H_

#include <IForwardSys.h>
#include <IHandleSys.h>
#include <IPluginSys.h>
#include <IShareSys.h>

#include "v8_sp_api.h"

namespace SourceMod {
namespace v8 {

class IPlugin;

/**
 * @brief Describes the various ways to pass parameters to plugins.
 *
 * Identical to the current enum except that Param_VarArgs still exists.
 */
enum ParamType
{
	Param_Any = SP_PARAMTYPE_ANY,
	Param_Cell = SP_PARAMTYPE_CELL,
	Param_Float = SP_PARAMTYPE_FLOAT,
	Param_String = SP_PARAMTYPE_STRING,
	Param_Array = SP_PARAMTYPE_ARRAY,
	Param_VarArgs = (5<<1),
	Param_CellByRef = SP_PARAMTYPE_CELL|SP_PARAMFLAG_BYREF,
	Param_FloatByRef = SP_PARAMTYPE_FLOAT|SP_PARAMFLAG_BYREF,
};

struct ByrefInfo
{
	unsigned int cells;
	cell_t *orig_addr;
	int flags;
	int sz_flags;
};

struct FwdParamInfo
{
	cell_t val;
	ByrefInfo byref;
	ParamType pushedas;
	bool isnull;
};

class IForwardFilter
{
public:
	virtual void Preprocess(SourcePawn::v8::IPluginFunction *fun, FwdParamInfo *params)
	{
	}
};

/**
 * @brief Unmanaged Forward.
 */
class IForward : public SourcePawn::v8::ICallable
{
public:
	/** Virtual Destructor */
	virtual ~IForward()
	{
	}
public:
	virtual const char *GetForwardName() =0;
	virtual unsigned int GetFunctionCount() =0;
	virtual ExecType GetExecType() =0;
	virtual int Execute(cell_t *result=NULL, IForwardFilter *filter=NULL) =0;
	virtual int PushArray(cell_t *inarray, unsigned int cells, int flags=0) =0;
	virtual int PushString(const char *string) = 0;
};

/**
 * @brief Managed Forward, same as IForward, except the collection can be modified.
 */
class IChangeableForward : public IForward
{
public:
	virtual bool RemoveFunction(SourcePawn::v8::IPluginFunction *func) =0;
	virtual unsigned int RemoveFunctionsOfPlugin(IPlugin *plugin) =0;
	virtual bool AddFunction(SourcePawn::v8::IPluginFunction *func) =0;
	virtual bool AddFunction(SourcePawn::v8::IPluginContext *ctx, funcid_t index) =0;
	virtual bool RemoveFunction(SourcePawn::v8::IPluginContext *ctx, funcid_t index) =0;
};

/**
 * @brief Provides functions for creating/destroying managed and unmanaged forwards.
 */
class IForwardManager : public SMInterface
{
public:
	virtual const char *GetInterfaceName()
	{
		return SMINTERFACE_FORWARDMANAGER_NAME;
	}
	virtual unsigned int GetInterfaceVersion()
	{
		return SMINTERFACE_FORWARDMANAGER_VERSION;
	}
	virtual bool IsVersionCompatible(unsigned int version)
	{
		if (version < 2 || version > GetInterfaceVersion())
		{
			return false;
		}
		return true;
	}
public:
	virtual IForward *CreateForward(const char *name,
									ExecType et,
									unsigned int num_params,
									const ParamType *types,
									...) =0;

	virtual IChangeableForward *CreateForwardEx(const char *name,
												ExecType et,
												int num_params,
												const ParamType *types,
												...) =0;

	virtual IForward *FindForward(const char *name, IChangeableForward **ifchng) =0;

	virtual void ReleaseForward(IForward *forward) =0;
};

/**
 * @brief Encapsulates a run-time plugin as maintained by SourceMod.
 */
class IPlugin
{
public:
	/** Virtual destructor */
	virtual ~IPlugin()
	{
	}

	virtual PluginType GetType() =0;
	virtual SourcePawn::v8::IPluginContext *GetBaseContext() =0;
	virtual sp_context_t *GetContext() =0;
	virtual void *GetPluginStructure() =0;
	virtual const sm_plugininfo_t *GetPublicInfo() =0;
	virtual const char *GetFilename() =0;
	virtual bool IsDebugging() =0;
	virtual PluginStatus GetStatus() =0;
	virtual bool SetPauseState(bool paused) =0;
	virtual unsigned int GetSerial() =0;
	virtual IdentityToken_t *GetIdentity() =0;
	virtual bool SetProperty(const char *prop, void *ptr) =0;
	virtual bool GetProperty(const char *prop, void **ptr, bool remove=false) =0;
	virtual SourcePawn::v8::IPluginRuntime *GetRuntime() =0;
	virtual IPhraseCollection *GetPhrases() =0;
	virtual Handle_t GetMyHandle() =0;
};

/**
 * @brief Iterates over a list of plugins.
 */
class IPluginIterator
{
public:
	/** Virtual destructor */
	virtual ~IPluginIterator()
	{
	};
public:
	virtual bool MorePlugins() =0;
	virtual IPlugin *GetPlugin() =0;
	virtual void NextPlugin() =0;
	virtual void Release() =0;
};

/**
 * @brief Listens for plugin-oriented events.
 */
class IPluginsListener_V1
{
public:
	virtual void OnPluginCreated(IPlugin *plugin) final
	{
	}

	virtual void OnPluginLoaded(IPlugin *plugin)
	{
	}

	virtual void OnPluginPauseChange(IPlugin *plugin, bool paused)
	{
	}

	virtual void OnPluginUnloaded(IPlugin *plugin)
	{
	}

	virtual void OnPluginDestroyed(IPlugin *plugin)
	{
	}
};

// @brief Listens for plugin-oriented events. Extends the V1 listener class.
class IPluginsListener : public IPluginsListener_V1
{
public:
	virtual unsigned int GetApiVersion() const {
		return SMINTERFACE_PLUGINSYSTEM_VERSION;
	}

	virtual void OnPluginWillUnload(IPlugin *plugin)
	{
	}
};

/**
 * @brief Manages the runtime loading and unloading of plugins.
 */
class IPluginManager : public SMInterface
{
public:
	virtual const char *GetInterfaceName()
	{
		return SMINTERFACE_PLUGINSYSTEM_NAME;
	}

	virtual unsigned int GetInterfaceVersion()
	{
		return SMINTERFACE_PLUGINSYSTEM_VERSION;
	}
public:
	virtual IPlugin *LoadPlugin(const char *path,
								bool debug,
								PluginType type,
								char error[],
								size_t maxlength,
								bool *wasloaded) =0;

	virtual bool UnloadPlugin(IPlugin *plugin) =0;

	virtual IPlugin *FindPluginByContext(const sp_context_t *ctx) =0;

	virtual unsigned int GetPluginCount() =0;

	virtual IPluginIterator *GetPluginIterator() =0;

	virtual void AddPluginsListener_V1(IPluginsListener_V1 *listener) =0;

	virtual void RemovePluginsListener_V1(IPluginsListener_V1 *listener) =0;

	virtual IPlugin *PluginFromHandle(Handle_t handle, HandleError *err) =0;

	virtual void AddPluginsListener(IPluginsListener *listener) =0;

	virtual void RemovePluginsListener(IPluginsListener *listener) =0;
};

} // namespace v8
} // namespace SourceMod

#endif // _INCLUDE_SOURCEMOD_COMPAT_V8_SM_API_H_
