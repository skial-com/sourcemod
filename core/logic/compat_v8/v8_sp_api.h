// vim: set ts=8 sts=4 sw=4 tw=99 et:
//
// Verbatim copies of the SourcePawn interfaces as they existed at SourceMod
// extension ABI version 8 (SourcePawn commit 3793034233). These declarations
// exist only so the compiler reproduces the exact vtable layout that an
// extension binary built against the old headers expects. Nothing in core
// should use these types except the adapters in this directory.
//
// Declaration order, the presence (or absence) of virtual destructors, and the
// set of virtual functions must never be changed. Non-virtual helpers and
// friend declarations are safe to add.
//
#ifndef _INCLUDE_SOURCEMOD_COMPAT_V8_SP_API_H_
#define _INCLUDE_SOURCEMOD_COMPAT_V8_SP_API_H_

#include <assert.h>
#include <stdio.h>

#include <sp_vm_types.h>

struct sp_context_s;
typedef struct sp_context_s sp_context_t;

namespace sp {
class Environment;
} // namespace sp

namespace SourcePawn {
namespace v8 {

/** The values the old headers used for SOURCEPAWN_API_VERSION and
 * SOURCEPAWN_ENGINE2_API_VERSION. */
static const unsigned int kV8ApiVersion = 0x0214;
static const unsigned int kV8Engine2ApiVersion = 0x10;

/** Old string parameter flags (shifted right by one bit versus HEAD). */
static const int kV8StringUtf8 = (1 << 0);
static const int kV8StringCopy = (1 << 1);
static const int kV8StringBinary = (1 << 2);

class ICompilation;
class IContextTrace;
class IPluginContext;
class IPluginRuntime;
class IProfiler;
class IProfilingTool;
class ISourcePawnEngine2;
class ISourcePawnEnvironment;
class IVirtualMachine;

/**
   * @brief Pseudo-NULL reference types.
   */
enum SP_NULL_TYPE {
    SP_NULL_VECTOR = 0, /**< Float[3] reference */
    SP_NULL_STRING = 1, /**< const String[1] reference */
};

// @brief Interface for a refcounted objects.
class IRefcountedObject
{
  public:
    // Virtual destructor.
    virtual ~IRefcountedObject() {}

    // @brief Increase the object's reference count.
    virtual void AddRef() = 0;

    // @brief Decrease the object's reference count, freeing any resources once
    // the reference count reaches zero.
    virtual void Release() = 0;
};

// @brief Interface for a native callback.
class INativeCallback : public IRefcountedObject
{
  public:
    // @brief Return the SOURCEPAWN_API_VERSION this was compiled against.
    virtual int GetApiVersion() const { return (int)kV8ApiVersion; }

    // @brief Called when a plugin invokes this native. The signature is the same
    // as a normal native callback.
    virtual cell_t Invoke(IPluginContext* ctx, const cell_t* params) = 0;
};

/**
 * @brief Represents what a function needs to implement in order to be callable.
 */
class ICallable
{
  public:
    virtual int PushCell(cell_t cell) = 0;
    virtual int PushCellByRef(cell_t* cell, int flags = SM_PARAM_COPYBACK) = 0;
    virtual int PushFloat(float number) = 0;
    virtual int PushFloatByRef(float* number, int flags = SM_PARAM_COPYBACK) = 0;
    virtual int PushArray(cell_t* inarray, unsigned int cells, int flags = 0) = 0;
    virtual int PushString(const char* string) = 0;
    virtual int PushStringEx(char* buffer, size_t length, int sz_flags, int cp_flags) = 0;
    virtual void Cancel() = 0;
};

/**
   * @brief Encapsulates a function call in a plugin.
   */
class IPluginFunction : public ICallable
{
  public:
    virtual int Execute(cell_t* result) = 0;
    virtual int CallFunction(const cell_t* params, unsigned int num_params, cell_t* result) = 0;
    virtual IPluginContext* GetParentContext() = 0;
    virtual bool IsRunnable() = 0;
    virtual funcid_t GetFunctionID() = 0;
    virtual int Execute2(IPluginContext* ctx, cell_t* result) = 0;
    virtual int CallFunction2(IPluginContext* ctx, const cell_t* params, unsigned int num_params,
                              cell_t* result) = 0;
    virtual IPluginRuntime* GetParentRuntime() = 0;
    virtual bool Invoke(cell_t* rval = nullptr) = 0;
    virtual const char* DebugName() = 0;
};

/**
   * @brief Interface to managing a debug context at runtime.
   */
class IPluginDebugInfo
{
  public:
    virtual int LookupFile(ucell_t addr, const char** filename) = 0;
    virtual int LookupFunction(ucell_t addr, const char** name) = 0;
    virtual int LookupLine(ucell_t addr, uint32_t* line) = 0;
    virtual int LookupFunctionAddress(const char* function, const char* file, ucell_t* addr) = 0;
    virtual int LookupLineAddress(const uint32_t line, const char* file, ucell_t* addr) = 0;
    virtual size_t NumFiles() = 0;
    virtual const char* GetFileName(size_t index) = 0;
    virtual size_t NumFunctions() = 0;
    virtual const char* GetFunctionName(size_t index, const char** file) = 0;
};

/**
   * @brief Interface to managing a runtime plugin.
   */
class IPluginRuntime
{
  public:
    virtual ~IPluginRuntime() {}
    virtual IPluginDebugInfo* GetDebugInfo() = 0;
    virtual int FindNativeByName(const char* name, uint32_t* index) = 0;
    virtual int GetNativeByIndex(uint32_t index, sp_native_t** native) = 0;
    virtual uint32_t GetNativesNum() = 0;
    virtual int FindPublicByName(const char* name, uint32_t* index) = 0;
    virtual int GetPublicByIndex(uint32_t index, sp_public_t** publicptr) = 0;
    virtual uint32_t GetPublicsNum() = 0;
    virtual int GetPubvarByIndex(uint32_t index, sp_pubvar_t** pubvar) = 0;
    virtual int FindPubvarByName(const char* name, uint32_t* index) = 0;
    virtual int GetPubvarAddrs(uint32_t index, cell_t* local_addr, cell_t** phys_addr) = 0;
    virtual uint32_t GetPubVarsNum() = 0;
    virtual IPluginFunction* GetFunctionByName(const char* public_name) = 0;
    virtual IPluginFunction* GetFunctionById(funcid_t func_id) = 0;
    virtual IPluginContext* GetDefaultContext() = 0;
    virtual bool IsDebugging() = 0;
    virtual int ApplyCompilationOptions(ICompilation* co) = 0;
    virtual void SetPauseState(bool paused) = 0;
    virtual bool IsPaused() = 0;
    virtual size_t GetMemUsage() = 0;
    virtual unsigned char* GetCodeHash() = 0;
    virtual unsigned char* GetDataHash() = 0;
    virtual int UpdateNativeBinding(uint32_t index, SPVM_NATIVE_FUNC pfn, uint32_t flags,
                                    void* data) = 0;
    virtual const sp_native_t* GetNative(uint32_t index) = 0;
    virtual const char* GetFilename() = 0;
    virtual int UpdateNativeBindingObject(uint32_t index, INativeCallback* native, uint32_t flags,
                                          void* data) = 0;
    virtual bool PerformFullValidation() = 0;
    virtual bool UsesDirectArrays() = 0;
};

/**
   * @brief Allows inspecting the stack frames of the SourcePawn environment.
   */
class IFrameIterator
{
  public:
    virtual bool Done() const = 0;
    virtual void Next() = 0;
    virtual void Reset() = 0;
    virtual IPluginContext* Context() const = 0;
    virtual bool IsNativeFrame() const = 0;
    virtual bool IsScriptedFrame() const = 0;
    virtual unsigned LineNumber() const = 0;
    virtual const char* FunctionName() const = 0;
    virtual const char* FilePath() const = 0;
    virtual bool IsInternalFrame() const = 0;
};

/**
   * @brief Interface to managing a context at runtime.
   */
class IPluginContext
{
  public:
    /** Virtual destructor */
    virtual ~IPluginContext(){};

    virtual IVirtualMachine* GetVirtualMachine() = 0;
    virtual sp_context_t* GetContext() = 0;
    virtual bool IsDebugging() = 0;
    virtual int SetDebugBreak(void* newpfn, void* oldpfn) = 0;
    virtual IPluginDebugInfo* GetDebugInfo() = 0;
    virtual int HeapAlloc(unsigned int cells, cell_t* local_addr, cell_t** phys_addr) = 0;
    virtual int HeapPop(cell_t local_addr) = 0;
    virtual int HeapRelease(cell_t local_addr) = 0;
    virtual int FindNativeByName(const char* name, uint32_t* index) = 0;
    virtual int GetNativeByIndex(uint32_t index, sp_native_t** native) = 0;
    virtual uint32_t GetNativesNum() = 0;
    virtual int FindPublicByName(const char* name, uint32_t* index) = 0;
    virtual int GetPublicByIndex(uint32_t index, sp_public_t** publicptr) = 0;
    virtual uint32_t GetPublicsNum() = 0;
    virtual int GetPubvarByIndex(uint32_t index, sp_pubvar_t** pubvar) = 0;
    virtual int FindPubvarByName(const char* name, uint32_t* index) = 0;
    virtual int GetPubvarAddrs(uint32_t index, cell_t* local_addr, cell_t** phys_addr) = 0;
    virtual uint32_t GetPubVarsNum() = 0;
    virtual int LocalToPhysAddr(cell_t local_addr, cell_t** phys_addr) = 0;
    virtual int LocalToString(cell_t local_addr, char** addr) = 0;
    virtual int StringToLocal(cell_t local_addr, size_t bytes, const char* source) = 0;
    virtual int StringToLocalUTF8(cell_t local_addr, size_t maxbytes, const char* source,
                                  size_t* wrtnbytes) = 0;
    virtual int PushCell(cell_t value) = 0;
    virtual int PushCellArray(cell_t* local_addr, cell_t** phys_addr, cell_t array[],
                              unsigned int numcells) = 0;
    virtual int PushString(cell_t* local_addr, char** phys_addr, const char* string) = 0;
    virtual int PushCellsFromArray(cell_t array[], unsigned int numcells) = 0;
    virtual int BindNatives(const sp_nativeinfo_t* natives, unsigned int num, int overwrite) = 0;
    virtual int BindNative(const sp_nativeinfo_t* native) = 0;
    virtual int BindNativeToAny(SPVM_NATIVE_FUNC native) = 0;
    virtual int Execute(uint32_t code_addr, cell_t* result) = 0;
    virtual cell_t ThrowNativeErrorEx(int error, const char* msg, ...) = 0;
    virtual cell_t ThrowNativeError(const char* msg, ...) = 0;
    virtual IPluginFunction* GetFunctionByName(const char* public_name) = 0;
    virtual IPluginFunction* GetFunctionById(funcid_t func_id) = 0;
    virtual SourceMod::IdentityToken_t* GetIdentity() = 0;
    virtual cell_t* GetNullRef(SP_NULL_TYPE type) = 0;
    virtual int LocalToStringNULL(cell_t local_addr, char** addr) = 0;
    virtual int BindNativeToIndex(uint32_t index, SPVM_NATIVE_FUNC native) = 0;
    virtual bool IsInExec() = 0;
    virtual IPluginRuntime* GetRuntime() = 0;
    virtual int Execute2(IPluginFunction* function, const cell_t* params, unsigned int num_params,
                         cell_t* result) = 0;
    virtual int GetLastNativeError() = 0;
    virtual cell_t* GetLocalParams() = 0;
    virtual void SetKey(int k, void* value) = 0;
    virtual bool GetKey(int k, void** value) = 0;
    virtual void ClearLastNativeError() = 0;
    virtual ISourcePawnEngine2* APIv2() = 0;
    virtual void ReportError(const char* fmt, ...) = 0;
    virtual void ReportErrorVA(const char* fmt, va_list ap) = 0;
    virtual void ReportFatalError(const char* fmt, ...) = 0;
    virtual void ReportFatalErrorVA(const char* fmt, va_list ap) = 0;
    virtual void ReportErrorNumber(int error) = 0;
    virtual cell_t BlamePluginError(IPluginFunction* pf, const char* msg, ...) = 0;
    virtual IFrameIterator* CreateFrameIterator() = 0;
    virtual void DestroyFrameIterator(IFrameIterator* it) = 0;
    virtual bool HeapAlloc2dArray(unsigned int length, unsigned int stride, cell_t* local_addr,
                                  const cell_t* init) = 0;
    virtual void EnterHeapScope() = 0;
    virtual void LeaveHeapScope() = 0;
    virtual cell_t GetNullFunctionValue() = 0;
    virtual bool IsNullFunctionId(funcid_t func) = 0;
    virtual bool GetFunctionByIdOrNull(funcid_t func, IPluginFunction** out) = 0;
    virtual IPluginFunction* GetFunctionByIdOrError(funcid_t func) = 0;
};

/**
   * @brief Information about a reported error.
   */
class IErrorReport
{
  public:
    virtual const char* Message() const = 0;
    virtual bool IsFatal() const = 0;
    virtual IPluginContext* Context() const = 0;
    virtual IPluginFunction* Blame() const = 0;
    virtual int Code() const = 0;
};

/**
   * @brief Provides callbacks for debug information.
   */
class IDebugListener
{
  public:
    virtual void OnContextExecuteError(IPluginContext* ctx, IContextTrace* error) {}
    virtual void OnDebugSpew(const char* msg, ...) = 0;
    virtual void ReportError(const IErrorReport& report, IFrameIterator& iter) = 0;
};

struct sp_plugin_s;
typedef struct sp_plugin_s sp_plugin_t;

/**
   * @brief Contains helper functions used by VMs and the host app
   */
class ISourcePawnEngine
{
  public:
    virtual sp_plugin_t* LoadFromFilePointer(FILE* fp, int* err) = 0;
    virtual sp_plugin_t* LoadFromMemory(void* base, sp_plugin_t* plugin, int* err) = 0;
    virtual int FreeFromMemory(sp_plugin_t* plugin) = 0;
    virtual void* BaseAlloc(size_t size) = 0;
    virtual void BaseFree(void* memory) = 0;
    virtual void* ExecAlloc(size_t size) = 0;
    virtual void ExecFree(void* address) = 0;
    virtual IDebugListener* SetDebugListener(IDebugListener* listener) = 0;
    virtual unsigned int GetContextCallCount() = 0;
    virtual unsigned int GetEngineAPIVersion() = 0;
    virtual void* AllocatePageMemory(size_t size) = 0;
    virtual void SetReadWrite(void* ptr) = 0;
    virtual void SetReadExecute(void* ptr) = 0;
    virtual void FreePageMemory(void* ptr) = 0;
    virtual int SetDebugBreakHandler(SPVM_DEBUGBREAK handler) = 0;
};

class ExceptionHandler;

/**
   * @brief Outlines the interface a Virtual Machine (JIT) must expose
   */
class ISourcePawnEngine2
{
  public:
    virtual unsigned int GetAPIVersion() = 0;
    virtual const char* GetEngineName() = 0;
    virtual const char* GetVersionString() = 0;
    virtual ICompilation* StartCompilation() = 0;
    virtual IPluginRuntime* LoadPlugin(ICompilation* co, const char* file, int* err) = 0;
    virtual SPVM_NATIVE_FUNC CreateFakeNative(SPVM_FAKENATIVE_FUNC, void*) = 0;
    virtual void DestroyFakeNative(SPVM_NATIVE_FUNC) = 0;
    virtual IDebugListener* SetDebugListener(IDebugListener* listener) = 0;
    virtual void SetProfiler(IProfiler* profiler) = 0;
    virtual const char* GetErrorString(int err) = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual IPluginRuntime* CreateEmptyRuntime(const char* name, uint32_t memory) = 0;
    virtual bool InstallWatchdogTimer(size_t timeout_ms) = 0;
    virtual bool SetJitEnabled(bool enabled) = 0;
    virtual bool IsJitEnabled() = 0;
    virtual void EnableProfiling() = 0;
    virtual void DisableProfiling() = 0;
    virtual void SetProfilingTool(IProfilingTool* tool) = 0;
    virtual IPluginRuntime* LoadBinaryFromFile(const char* file, char* error, size_t maxlength) = 0;
    virtual ISourcePawnEnvironment* Environment() = 0;
    virtual IPluginRuntime* LoadBinaryFromMemory(const char* file, uint8_t* addr, size_t size,
                                                 void (*dtor)(uint8_t*), char* error,
                                                 size_t maxlength) = 0;
};

// @brief This class is the v3 API for SourcePawn. It provides access to
// the original v1 and v2 APIs as well.
class ISourcePawnEnvironment
{
  public:
    virtual ~ISourcePawnEnvironment() {}
    virtual int ApiVersion() = 0;
    virtual ISourcePawnEngine* APIv1() = 0;
    virtual ISourcePawnEngine2* APIv2() = 0;
    virtual void Shutdown() = 0;
    virtual void EnterExceptionHandlingScope(ExceptionHandler* handler) = 0;
    virtual void LeaveExceptionHandlingScope(ExceptionHandler* handler) = 0;
    virtual bool HasPendingException(const ExceptionHandler* handler) = 0;
    virtual const char* GetPendingExceptionMessage(const ExceptionHandler* handler) = 0;
    virtual bool EnableDebugBreak() = 0;
    virtual void SetDebugMetadataFlags(int flags) = 0;
};

// @brief A helper class for handling exceptions.
class ExceptionHandler
{
    friend class sp::Environment;

  public:
    ExceptionHandler(ISourcePawnEngine2* api)
     : env_(api->Environment()),
       catch_(true)
    {
        env_->EnterExceptionHandlingScope(this);
    }
    ExceptionHandler(IPluginContext* ctx)
     : env_(ctx->APIv2()->Environment()),
       catch_(true)
    {
        env_->EnterExceptionHandlingScope(this);
    }
    ~ExceptionHandler() {
        env_->LeaveExceptionHandlingScope(this);
    }

    virtual uint32_t ApiVersion() const {
        return kV8ApiVersion;
    }

    void Rethrow() {
        assert(catch_ && HasException());
        catch_ = false;
    }

    bool HasException() const {
        return env_->HasPendingException(this);
    }

    const char* Message() const {
        return env_->GetPendingExceptionMessage(this);
    }

    // Non-virtual accessor added for the compatibility layer; the old class
    // kept |catch_| protected and let sp::Environment read it as a friend.
    // Adding a non-virtual member has no effect on the object layout.
    bool WillCatch() const {
        return catch_;
    }

  private:
    // Don't allow heap construction.
    ExceptionHandler(const ExceptionHandler& other);
    void operator =(const ExceptionHandler& other);
    void* operator new(size_t size);
    void operator delete(void*, size_t);

  private:
    ISourcePawnEnvironment* env_;
    ExceptionHandler* next_;

  protected:
    // If true, the exception will be swallowed.
    bool catch_;
};

// @brief An implementation of ExceptionHandler that simply collects
// whether an exception was thrown.
class DetectExceptions : public ExceptionHandler
{
  public:
    DetectExceptions(ISourcePawnEngine2* api)
     : ExceptionHandler(api)
    {
        catch_ = false;
    }
    DetectExceptions(IPluginContext* ctx)
     : ExceptionHandler(ctx)
    {
        catch_ = false;
    }
};

} // namespace v8
} // namespace SourcePawn

#endif // _INCLUDE_SOURCEMOD_COMPAT_V8_SP_API_H_
