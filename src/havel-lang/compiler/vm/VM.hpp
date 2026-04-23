#pragma once

#include "../core/BytecodeIR.hpp"
#include "../gc/GC.hpp"
#include "VMImage.hpp"
#include "../../runtime/HostContext.hpp"
#include "../../runtime/ModuleLoader.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel::compiler {

// Forward declarations
class Fiber;
using CallbackId = uint32_t;
constexpr CallbackId INVALID_CALLBACK_ID = 0;

namespace havel::errors {
  struct HavelError;
}

// Error handling with stack traces
struct ScriptError final {
  Value value;
  std::string message;
  std::string stackTrace;
  uint32_t line = 0;
  uint32_t column = 0;

  ScriptError(Value val, const std::string &msg,
              const std::string &trace, uint32_t ln = 0, uint32_t col = 0)
      : value(std::move(val)), message(msg), stackTrace(trace), line(ln),
        column(col) {}

  const char *what() const noexcept { return message.c_str(); }
  
  // Note: toHavelError() implementation moved to .cpp to avoid Qt moc issues
};

// ============================================================================
// VM EXECUTION RESULT
//
// Result of executing a single bytecode instruction in a Fiber
// Used by Phase 3 main loop (executeFrame) to determine fiber state
// ============================================================================
struct VMExecutionResult {
  enum Type : uint8_t {
    YIELD,       // Instruction completed normally, return value on stack
    SUSPENDED,   // Fiber suspended (waiting for event), state frozen
    RETURNED,    // Function returned, value in result_value
    ERROR        // Exception thrown, message in error_message
  };
  
  Type type;
  Value result_value;           // For YIELD or RETURNED
  std::string error_message;    // For ERROR
  
  // Default constructor
  VMExecutionResult();
  
  // Convenience constructors
  static VMExecutionResult Yield(const Value& value) {
    VMExecutionResult r;
    r.type = YIELD;
    r.result_value = value;
    return r;
  }
  
  static VMExecutionResult Suspended() {
    VMExecutionResult r;
    r.type = SUSPENDED;
    r.result_value = Value();
    return r;
  }
  
  static VMExecutionResult Returned(const Value& value) {
    VMExecutionResult r;
    r.type = RETURNED;
    r.result_value = value;
    return r;
  }
  
  static VMExecutionResult Error(const std::string& msg) {
    VMExecutionResult r;
    r.type = ERROR;
    r.error_message = msg;
    r.result_value = Value();
    return r;
  }
};

class VM : public BytecodeInterpreter {
public:
  // Timer check callback - called periodically during script execution
  using TimerCheckFunction = std::function<void()>;
  
  class GCRoot {
  public:
    GCRoot() = default;
    GCRoot(VM &vm, const Value &value) : vm_(&vm) {
      id_ = vm.pinExternalRoot(value);
    }
    GCRoot(const GCRoot &) = delete;
    GCRoot &operator=(const GCRoot &) = delete;
    GCRoot(GCRoot &&other) noexcept : vm_(other.vm_), id_(other.id_) {
      other.vm_ = nullptr;
      other.id_ = 0;
    }
    GCRoot &operator=(GCRoot &&other) noexcept {
      if (this == &other) {
        return *this;
      }
      reset();
      vm_ = other.vm_;
      id_ = other.id_;
      other.vm_ = nullptr;
      other.id_ = 0;
      return *this;
    }
    ~GCRoot() { reset(); }

    std::optional<Value> get() const {
      if (!vm_ || id_ == 0) {
        return std::nullopt;
      }
      return vm_->externalRootValue(id_);
    }

    void reset() {
      if (vm_ && id_ != 0) {
        vm_->unpinExternalRoot(id_);
      }
      vm_ = nullptr;
      id_ = 0;
    }

  private:
    VM *vm_ = nullptr;
    uint64_t id_ = 0;
  };

private:
  struct RuntimeClosure {
    uint32_t function_index = 0;
    uint32_t chunk_index = 0;  // NEW: track which chunk this closure belongs to
    std::vector<std::shared_ptr<GCHeap::UpvalueCell>> upvalues;
  };

  struct TryHandler {
    uint32_t catch_ip = 0;
    uint32_t finally_ip = 0; // 0 if no finally block
    uint32_t finally_return_ip =
        0; // Where to go after finally (catch_ip or re-throw)
    size_t stack_depth = 0;
  };

  struct CallFrame {
    const BytecodeFunction *function = nullptr;
    size_t ip = 0;
    size_t locals_base = 0;
    uint32_t closure_id = 0;
    std::vector<TryHandler> try_stack;
  };

  std::stack<Value> stack;
  std::vector<Value> locals;
  std::vector<CallFrame> frame_arena_;
  size_t frame_count_ = 0;
  GCHeap heap_;
  std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>
      open_upvalues;
  std::unordered_map<std::string, Value> globals;
  mutable std::shared_mutex globals_mutex_; // Thread-safe access to globals
  std::unordered_map<std::string, BytecodeHostFunction> host_functions;
  std::vector<std::string> host_function_names_; // Index -> name mapping
  std::unordered_map<std::string, Value> host_function_globals_; // Name -> HostFuncId Value
  
        // Function properties support (fn.prop = value for static state, memoization, etc.)
        std::unordered_map<uint32_t, ObjectRef> function_properties_; // function_index -> properties object
        std::unordered_map<uint32_t, ObjectRef> closure_properties_;  // closure_id -> properties object
        std::unordered_map<uint32_t, ObjectRef> hostfunc_properties_; // host_func_idx -> properties object

        bool has_current_exception_ = false;
  Value current_exception_ = nullptr;

    // Module exports for END_MODULE opcode
    Value module_exports_;

    // Module system: delegate path resolution + caching to canonical ModuleLoader
    // Circular dependency detection still in VM
    std::unordered_set<std::string> modules_loading_; // Circular dependency detection
    std::string current_script_dir_; // Directory of the currently executing script (for relative imports)
    // Keep module BytecodeChunks alive so exported closures can reference them
    std::unordered_map<std::string, std::shared_ptr<BytecodeChunk>> module_chunks_;

    // Canonical module loader for path resolution and caching
    ModuleLoader moduleLoader_;

    std::vector<std::unordered_map<std::string, Value>> globals_stack_;

  // ObjectId of the _G heap object; UINT32_MAX = unset.
  // OBJECT_GET/OBJECT_SET/ITER_NEW check this to delegate to live globals maps.
  uint32_t globals_mirror_object_id_ = UINT32_MAX;

  // Coroutine support (Lua-style coroutines)
  uint32_t current_coroutine_id_ = 0;  // Currently executing coroutine (0 = main)
  std::unordered_map<uint32_t, uint32_t> coroutine_to_frame_; // Map coroutine ID to frame index

  // Phase 3B-7: Suspension state for non-blocking fiber suspension
  // When an operation needs to wait (thread.join, channel.recv, etc)
  // it sets these flags to signal the VM to suspend the current fiber
  bool suspension_requested_ = false;          // True if suspension needed
  uint8_t suspension_reason_ = 0;              // Why is it suspending? (SuspensionReason enum value)
  void* suspension_context_ = nullptr;         // Context pointer (thread_id, channel*, etc)

  // Phase 3B-7: Thread wait tracking
  // Maps thread_id -> Fiber* for fibers suspended on THREAD_JOIN
  // Used to unpark fibers when threads complete
  std::unordered_map<uint32_t, Fiber*> thread_wait_map_;
  mutable std::shared_mutex thread_wait_mutex_;

  // Phase 2A: Event queue reference for variable change notifications
  class EventQueue* event_queue_ = nullptr;
  
  // Phase 2A: Helper to emit VAR_CHANGED events
  void emitVariableChanged(const std::string& var_name);
  
  // Prototype system - methods on types (String, Array, Object)
  // Maps type name -> method name -> host function index
  std::unordered_map<std::string,
                     std::unordered_map<std::string, uint32_t>>
      prototypes_;

  // Host context for service access (non-owning)
  const HostContext *context_ = nullptr;

  const BytecodeChunk *current_chunk = nullptr;
  bool debug_mode = false;
  size_t max_call_depth_ = 1024;
  bool profiling_enabled_ = false;
  std::array<uint64_t, 256> opcode_counts_{};
  uint64_t executed_instructions_ = 0;

  // System object initializer - called after registerDefaultHostGlobals()
  using SystemObjectInitializer = std::function<void(VM *)>;
  SystemObjectInitializer system_object_initializer_;
  
  // Timer check function - called periodically during execution
  TimerCheckFunction timer_check_func_;
  size_t instructions_since_timer_check_ = 0;
  static constexpr size_t TIMER_CHECK_INTERVAL = 1000;  // Check every 1000 instructions

  template <typename T> T getValue(const Value &value);
  std::string toStringInternal(const Value &value,
                               std::unordered_set<uint32_t> &visitedIds,
                               int depth) const;
  const CallFrame &currentFrame() const;
  CallFrame &currentFrame();
  Value getConstant(uint32_t index);

  // State snapshot for re-entrant calls (HOF callbacks)
  struct ExecutionState {
    std::stack<Value> stack;
    std::vector<Value> locals;
    std::vector<CallFrame> frames;
    size_t frame_count = 0;
  };
  ExecutionState saveState() const;
  void restoreState(const ExecutionState &state);

  // Callback queue for non-reentrant execution
  struct PendingCall {
    Value fn;
    std::vector<Value> args;
    Value *result; // Pointer to store result
    bool *completed;       // Flag to signal completion
  };
  std::vector<PendingCall> pending_calls;
  void scheduleCall(const Value &fn,
                    const std::vector<Value> &args,
                    Value &result, bool &completed);
  void processPendingCalls();
  Value callFunctionSync(const Value &fn,
                                 const std::vector<Value> &args);
  void executeInstruction(const Instruction &instruction);

  // Inline stack helper declarations - extracted from executeInstruction lambdas
  Value popStack();
  void pushStack(Value value);
  uint32_t toAbsoluteLocal(uint32_t local_index);
  void ensureLocalIndex(uint32_t absolute_index);
  void doReturn();

  // Extracted opcode handlers to reduce stack frame size
  void execBinaryOp(const Instruction &instruction);
  void execLogicalOp(OpCode opcode);
  void execNegate();
  void execJump(const Instruction &instruction);
  void execJumpIfFalse(const Instruction &instruction);
  void execJumpIfTrue(const Instruction &instruction);

  void doCall(Value callee_value, std::vector<Value> args,
              bool advance_caller_ip = true);
  void doTailCall(Value callee_value,
                  std::vector<Value> args); // TCO
  void runDispatchLoop(size_t stop_frame_depth);
  bool handleScriptThrow(const Value &value);
  std::string buildStackTrace(size_t frame_count) const;
  void closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end);

  // Source location helper for error messages
  std::string currentSourceLocation() const;

  // Rust-style error formatting with source line and arrow
  int64_t toInt(const Value &value) const;
  double toFloat(const Value &value) const;
  bool toBool(const Value &value) const;
  std::optional<std::string> valueAsString(const Value &value) const;
  bool valuesEqualDeep(const Value &left, const Value &right) const;
  bool valuesEqualDeep(const Value &left, const Value &right,
                       std::unordered_set<uint64_t> &visited_array_pairs,
                       std::unordered_set<uint64_t> &visited_object_pairs) const;
  std::string formatErrorWithContext(const std::string &message) const;

  std::vector<Value> stackValuesForRoots() const;
  std::vector<uint32_t> activeClosureIdsForRoots() const;
  void maybeCollectGarbage();
  void collectGarbage();
  void stepGarbageCollection(size_t work_budget = 128);
  void registerDefaultHostFunctions();
  void registerDefaultHostGlobals();
  Value invokeHostFunction(const std::string &name, uint32_t arg_count);

public:
  // Value utility functions (public for prototype method implementations)
  bool toBoolPublic(const Value &value) const { return toBool(value); }
  bool valuesEqualDeepPublic(const Value &left, const Value &right) const { return valuesEqualDeep(left, right); }

  // Direct invocation (bypasses stack, takes args as vector)
  Value invokeHostFunctionDirect(const std::string &name,
                                  const std::vector<Value> &args);

  VM();
  VM(const HostContext &ctx);
  ~VM() override;
  Value execute(const BytecodeChunk &chunk,
                const std::string &function_name,
                const std::vector<Value> &args = {}) override;
  // Execute persistently (preserves globals and heap - for REPL)
  Value executePersistent(const BytecodeChunk &chunk,
                          const std::string &function_name,
                          const std::vector<Value> &args = {});
  
  // ========== PHASE 2E: CONDITION EVALUATION ==========
  // Evaluate a condition bytecode (for reactive watchers)
  // Executes condition code and returns result as boolean
  // Used by WatcherRegistry to re-evaluate conditions on variable changes
  // @param func_index The function index in bytecode (condition function)
  // @param ip Optional instruction pointer within function (default: 0)
  // @return Boolean result of condition evaluation
  bool evaluateConditionBytecode(uint32_t func_index, uint32_t ip = 0);
  
  // ========== PHASE 3: SINGLE-STEP EXECUTION ==========
  // Execute exactly one bytecode instruction in the current fiber
  // Returns immediately after one instruction (never blocks)
  // Used by main event loop for cooperative fiber scheduling
  // @param current_fiber The Fiber to execute in (must be RUNNABLE state)
  // @return VMExecutionResult indicating what happened
  VMExecutionResult executeOneStep(Fiber *current_fiber);
  
  // ========== PHASE 3B-1: FIBER STATE SYNCHRONIZATION ==========
  // Load fiber's state into VM's global execution state
  // Must be called before executeOneStep() to restore suspended fiber
  // @param fiber The Fiber to load (must have SUSPENDED or pending state)
  void loadFiberState(Fiber *fiber);
  
  // Save VM's current execution state back to fiber
  // Must be called after executeOneStep() to persist execution progress
  // @param fiber The Fiber to save to (preserves IP for resumption)
  void saveFiberState(Fiber *fiber);

  // ========== PHASE 3B-7: SUSPENSION CALLBACKS ==========
  // Track which fiber is waiting on a specific thread
  // Called when a fiber is suspended waiting for THREAD_JOIN completion
  // @param thread_id The thread being waited on
  // @param fiber The fiber waiting (will be unparked when thread completes)
  void registerThreadWait(uint32_t thread_id, Fiber *fiber);
  
  // Get the fiber waiting on a specific thread (if any)
  Fiber* getThreadWaitingFiber(uint32_t thread_id) const;
  
  // Unregister a thread from tracking once the fiber is resumed
  void unregisterThreadWait(uint32_t thread_id);
  
  // Get all thread IDs that have waiting fibers (for iteration in main loop)
  // Thread-safe copy of all threads with suspended fibers
  std::vector<uint32_t> getWaitingThreadIds() const;
  
  // Check and clear all threads, calling callback for each waiting fiber
  // Callback signature: void(uint32_t thread_id, Fiber* waiting_fiber)
  // Used by ExecutionEngine to unpark fibers when threads complete
  template<typename Callback>
  void checkWaitingThreads(const Callback& on_fiber_ready) {
    std::shared_lock<std::shared_mutex> lock(thread_wait_mutex_);
    for (auto& [thread_id, fiber] : thread_wait_map_) {
      if (fiber) {
        on_fiber_ready(thread_id, fiber);
      }
    }
  }

  // ========== CALLER INFO ==========
  // Returns caller info at given depth (0 = immediate caller, 1 = caller's caller, etc.)
  // Returns {function_name, line, file} as strings, or empty strings if unavailable
  struct CallerInfo {
    std::string function;
    uint32_t line = 0;
    uint32_t column = 0;
    std::string file;
  };
  CallerInfo getCallerInfo(int depth = 0) const;
  
  // Check if there are any active frames (for detecting completion)
  bool hasActiveFrames() const { return frame_count_ > 0; }
  
  Value call(const Value &callee_value,
             const std::vector<Value> &args = {});
  std::string toString(const Value &value) const;
  bool toBoolPublic(const Value &value);
  void setDebugMode(bool enabled) override;
  void registerHostFunction(const std::string &name,
                            BytecodeHostFunction function) override;
  void registerHostFunction(const std::string &name, size_t arity,
                            BytecodeHostFunction function);
  bool hasHostFunction(const std::string &name) const override;
  uint32_t getHostFunctionIndex(const std::string &name);
  void throwError(const std::string &msg);
  
  // Phase 4 JIT: Hot function notification
  using HotFunctionCallback = std::function<void(const BytecodeFunction&)>;

  void setHotFunctionCallback(HotFunctionCallback cb) { hot_func_cb_ = std::move(cb); }

  // System object initializer - called after registerDefaultHostGlobals()
  void setSystemObjectInitializer(SystemObjectInitializer init) {
    system_object_initializer_ = std::move(init);
  }

  void setMaxCallDepth(size_t value);
  void setProfilingEnabled(bool enabled) { profiling_enabled_ = enabled; }
  uint64_t executedInstructionCount() const { return executed_instructions_; }
  uint64_t opcodeCount(OpCode opcode) const {
    return opcode_counts_[static_cast<uint8_t>(opcode)];
  }

  void setGcAllocationBudget(size_t value) { heap_.setAllocationBudget(value); }
  void runGarbageCollection() { collectGarbage(); }
  GCHeap::Stats gcStats() const { return heap_.stats(); }
  GCHeap& getHeap() { return heap_; }
  const GCHeap& getHeap() const { return heap_; }
  
  // Phase 2A: Event queue for variable change notifications
  void setEventQueue(class EventQueue* eq) { event_queue_ = eq; }
  
  void setGlobal(std::string name, Value value) {
    globals[std::move(name)] = std::move(value);
    // Phase 2A: Emit VAR_CHANGED event when global is modified
    emitVariableChanged(name);
  }
  [[nodiscard]] GCRoot makeRoot(const Value &value) {
    return GCRoot(*this, value);
  }
  ObjectRef createHostObject();
  ArrayRef createHostArray();
  StringRef createRuntimeString(std::string value);
  size_t getRuntimeStringLength(StringRef string_ref);
  void setHostObjectField(ObjectRef object_ref, const std::string &key,
                          Value value);
  void pushHostArrayValue(ArrayRef array_ref, Value value);

  // Array helpers
  size_t getHostArrayLength(ArrayRef array_ref);
  Value getHostArrayValue(ArrayRef array_ref, size_t index);
  void setHostArrayValue(ArrayRef array_ref, size_t index, Value value);
  Value popHostArrayValue(ArrayRef array_ref);
  void insertHostArrayValue(ArrayRef array_ref, size_t index,
                            Value value);
  Value removeHostArrayValue(ArrayRef array_ref, size_t index);

  // Range helpers
  bool isInRange(RangeRef range_ref, int64_t value);

  // Enum helpers
  uint32_t registerEnumType(const std::string &name,
                            const std::vector<std::string> &variants);
  EnumRef createEnum(uint32_t typeId, uint32_t tag, size_t payloadCount);
  uint32_t getEnumTag(EnumRef enum_ref);
  Value getEnumPayload(EnumRef enum_ref, size_t index);
  void setEnumPayload(EnumRef enum_ref, size_t index,
                      const Value &value);
  uint32_t getEnumPayloadCount(EnumRef enum_ref);
  std::string getEnumTypeName(uint32_t typeId) const;
  std::string getEnumVariantName(uint32_t typeId, uint32_t tag) const;
  uint32_t getEnumTypeVariantCount(uint32_t typeId) const;

  // Membership helpers
  bool arrayContains(ArrayRef array_ref, const Value &value);
  bool objectHasKey(ObjectRef object_ref, const std::string &key);

  // Iterator helpers
  IteratorRef createIterator(const Value &iterable);
  Value iteratorNext(IteratorRef iterRef);

  // Object helpers
  std::vector<std::string> getHostObjectKeys(ObjectRef object_ref);
  std::vector<std::pair<std::string, Value>>
  getHostObjectEntries(ObjectRef object_ref);
  bool hasHostObjectField(ObjectRef object_ref, const std::string &key);
  Value getHostObjectField(ObjectRef object_ref,
                                   const std::string &key);
  bool deleteHostObjectField(ObjectRef object_ref, const std::string &key);
  void setHostObjectFrozen(ObjectRef object_ref, bool frozen);
  void setHostObjectSealed(ObjectRef object_ref, bool sealed);

  // Function calling
  Value callHostFunction(const Value &fn,
                                 const std::vector<Value> &args);

  // General function call (handles both VM closures and host functions)
  Value callFunction(const Value &fn,
                             const std::vector<Value> &args);

  // Prototype system - methods on types
  void registerPrototypeMethod(const std::string &typeName,
                               const std::string &methodName,
                               uint32_t hostFuncIndex);
  void registerPrototypeMethodByName(const std::string &typeName,
                                     const std::string &methodName,
                                     const std::string &funcName);
  std::optional<uint32_t>
  getPrototypeMethod(const Value &value, const std::string &methodName);
  std::vector<std::string> getPrototypeMethods(const Value &value);

  // Get registered host functions (for copying to options)
  std::unordered_map<std::string, BytecodeHostFunction> &getHostFunctions() {
    return host_functions;
  }
  const std::unordered_map<std::string, BytecodeHostFunction> &
  getHostFunctions() const {
    return host_functions;
  }
  std::optional<std::string> getHostFunctionName(uint32_t index) const;

  uint64_t pinExternalRoot(const Value &value);
  bool unpinExternalRoot(uint64_t root_id);
  std::optional<Value> externalRootValue(uint64_t root_id) const;
  size_t externalRootCount() const { return heap_.externalRootCount(); }

  // Callback system - VM owns closures, systems use opaque IDs
  CallbackId registerCallback(const Value &closure);
  Value invokeCallback(CallbackId id,
                               const std::vector<Value> &args = {});
  void releaseCallback(CallbackId id);
  bool isValidCallback(CallbackId id) const;
  void beginHotkeyExecution();
  void endHotkeyExecution();
  void garbageCollectionSafePoint(size_t work_budget = 128);
  
  // Set timer check callback - called periodically during script execution
  void setTimerCheckFunction(TimerCheckFunction func) {
    timer_check_func_ = std::move(func);
  }

  // Value utility functions
  bool isNull(const Value &value) const;
  bool isTruthy(const Value &value);
  std::optional<int64_t> parseDuration(const Value &value) const;

  // Image helpers - create GC-managed images
  VMImage createImage(int width, int height, int stride, PixelFormat format,
                      const uint8_t *data);
  VMImage createImageFromRGBA(int width, int height,
                              const std::vector<uint8_t> &rgbaData);

  // ============================================================================
  // Execution Context System - Isolated execution with shared globals
  // ============================================================================

  // Lightweight execution context for async/threaded execution
  // Shares: globals, heap, host_functions, prototypes, chunk
  // Isolated: stack, locals, frames, open_upvalues
  struct VMExecutionContext {
  private:
    VM *parent_vm_ = nullptr;
    std::stack<Value> stack;
    std::vector<Value> locals;
    std::vector<CallFrame> frame_arena_;
    size_t frame_count_ = 0;
    std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>
        open_upvalues;
    bool has_current_exception_ = false;
    Value current_exception_ = nullptr;
    const BytecodeChunk *current_chunk = nullptr;

    friend class VM;

  public:
    VMExecutionContext() = default;
    ~VMExecutionContext() = default;

    // Non-copyable (contains unique execution state)
    VMExecutionContext(const VMExecutionContext &) = delete;
    VMExecutionContext &operator=(const VMExecutionContext &) = delete;

    // Movable
    VMExecutionContext(VMExecutionContext &&) = default;
    VMExecutionContext &operator=(VMExecutionContext &&) = default;

    // Execute a callback in this isolated context
    Value invokeCallback(CallbackId id,
                                 const std::vector<Value> &args = {});

    // Internal: execute single instruction in this context
    void executeInstructionInContext(const Instruction &instruction);

    // Check if context is valid (has parent VM)
    bool isValid() const { return parent_vm_ != nullptr; }
  };

  // Create a lightweight execution context that shares globals/heap but has
  // isolated stack This is the CORRECT way to execute hotkeys from threads
  VMExecutionContext createExecutionContext();

  // Thread-safe global variable access
  void setGlobalThreadSafe(const std::string &name, Value value);
  std::optional<Value>
  getGlobalThreadSafe(const std::string &name) const;

  // Get all globals (for module export collection)
  const std::unordered_map<std::string, Value> &getAllGlobals() const { return globals; }

    // Get _G as object (for module exports)
    Value getGlobalObject() {
        return globals["_G"];
    }

    void setGlobalObject(const std::string& name, const Value& value) {
        globals[name] = value;
    }

    Value runInContext(const std::string& source, Value context);

    Value loadModule(const std::string& path);
    void addModuleSearchPath(const std::string& path) { moduleLoader_.addSearchPath(path); }
    void setCurrentScriptDir(const std::string& dir) { current_script_dir_ = dir; }

    ModuleLoader& moduleLoader() { return moduleLoader_; }

    // Get the current bytecode chunk (for execution contexts)
  const BytecodeChunk *getCurrentChunk() const { return current_chunk; }
  void setCurrentChunk(const BytecodeChunk *chunk) { current_chunk = chunk; }

  // Resolve a Value that might be a string to an actual string
  std::string resolveStringKey(const Value &value) const;

  /** Injected embedder context; null if VM was default-constructed. */
  const HostContext *hostContext() const { return context_; }

private:
  std::atomic<uint32_t> active_hotkey_executions_{0};

  uint32_t app_args_array_id_ = 0;
  std::function<void()> restart_callback_;
  HotFunctionCallback hot_func_cb_;

public:
  void setAppArgs(uint32_t array_id) { app_args_array_id_ = array_id; }
  void setRestartCallback(std::function<void()> cb) { restart_callback_ = std::move(cb); }
};

} // namespace havel::compiler
