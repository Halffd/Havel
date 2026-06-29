#pragma once

#include "../core/BytecodeIR.hpp"
#include "../gc/GC.hpp"
#include "VMImage.hpp"
#include "../../runtime/HostContext.hpp"
#include "../../runtime/ModuleLoader.hpp"

namespace havel { class Loader; }

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
#include <thread>
#include <mutex>
#include <queue>

#include "utils/RobinHoodHashMap.hpp"

namespace havel::compiler {

// Forward declarations
class Fiber;
class Scheduler;
class WatcherRegistry;
enum class FiberPriority : uint8_t;
enum class HotkeyPolicy : uint8_t;
using CallbackId = uint32_t;
constexpr CallbackId INVALID_CALLBACK_ID = 0;

} // close havel::compiler

namespace havel::errors {
struct HavelError;
} // close havel::errors

namespace havel::compiler {

// Error handling with stack traces
struct ScriptThrow final {
  Value value;
};

// JIT coroutine signal — thrown by JIT runtime stubs to exit native code
// when a coroutine/scheduler opcode (YIELD, AWAIT, GO_ASYNC, FIBER_SLEEP,
// YIELD_RESUME) requires returning control to the scheduler.
// executeCompiled() catches this and translates it into a suspension request.
struct JitCoroutineSignal final {
  enum class Op : uint8_t {
    YIELD,        // YIELD opcode hit
    YIELD_RESUME, // YIELD_RESUME opcode hit
    AWAIT,        // FIBER_AWAIT opcode hit
    GO_ASYNC,     // GO_ASYNC opcode hit
    SLEEP,        // FIBER_SLEEP opcode hit
  };
  Op op;
  Value value;          // The value involved (yield value, awaitable, function ref, ms)
};

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
// LAZY MODULE LOADING
struct ModuleDescriptor {
    std::string name;
    std::function<void(struct VMApi&)> initFn;
    bool loaded = false;
    std::vector<std::string> aliases;
};

// ============================================================================
// VM EXECUTION RESULT
//
// Result of executing a single bytecode instruction in a Fiber

// A pre-resolved direct function call that bypasses VM dispatch.
// Used by the hotkey fast path: instead of spawning a goroutine and
// running the bytecode dispatch loop, we iterate these calls directly.
struct DirectCall {
    uint32_t host_func_idx;
    std::vector<Value> args;
};

// A sequence of direct calls for a host-only hotkey callback.
struct DirectCallThunk {
    std::vector<DirectCall> calls;
};

// ============================================================================
struct VMExecutionResult {
  enum Type : uint8_t {
    YIELD,       // Instruction completed normally, return value on stack
    SUSPENDED,   // Fiber suspended (waiting for event), state frozen
    RETURNED,    // Function returned, value in result_value
    ERROR,       // Exception thrown, message in error_message
    DEBUG_BREAK  // Breakpoint hit, debugger should take control
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

  static VMExecutionResult DebugBreak() {
    VMExecutionResult r;
    r.type = DEBUG_BREAK;
    r.result_value = Value();
    return r;
  }
};

struct VMConfig {
    // Heap / GC
    uint64_t heap_min_bytes = 64 * 1024 * 1024;
    uint64_t heap_max_bytes = 4ULL * 1024 * 1024 * 1024;
    size_t gc_budget = 65536;
    bool gc_incremental = true;
    bool gc_stop_the_world = true;
    size_t gc_full_collection_interval = 8;
    uint8_t gc_promotion_age = 2;

    // Call / stack limits
    size_t max_call_depth = 16384;
    size_t max_stack_depth = 1 << 20;
    uint64_t max_instructions = 0;

    // Scheduler / goroutine
    uint64_t goroutine_tick_instructions = 10000;
    uint64_t goroutine_hotkey_tick_instructions = 100000;

    // Tiering (JIT)
    bool tiering_enabled = false;
    uint64_t tier1_threshold = 1000;
    uint64_t tier2_threshold = 10000;
    bool tier2_flush_on_shutdown = false;

    // Timer check interval (instructions between timer checks)
    size_t timer_check_interval = 1000;
};

class __attribute__((visibility("default"))) VM : public BytecodeInterpreter {
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

  enum class DebugStepMode : uint8_t {
    Continue,
    StepInto,
    StepOver,
    StepOut
  };

private:

 struct TryHandler {
    uint32_t catch_ip = 0;
    uint32_t finally_ip = 0; // 0 if no finally block
    uint32_t finally_return_ip =
        0; // Where to go after finally (catch_ip or re-throw)
    size_t stack_depth = 0;
  };

struct CallFrame {
  const BytecodeFunction *function = nullptr;
  const BytecodeChunk *chunk = nullptr;
  size_t ip = 0;
  size_t locals_base = 0;
  uint32_t closure_id = 0;
  bool owns_globals = false;
  std::vector<TryHandler> try_stack;
  size_t stack_depth = 0; // Expression stack depth at call time
  std::vector<Value> defer_stack; // Deferred closures to execute on scope exit
};
  public:

  std::stack<Value> stack;
  std::vector<Value> locals;
  std::vector<CallFrame> frame_arena_;
 size_t frame_count_ = 0;
 int bc_execute_depth_ = 0;
 GCHeap heap_;
  std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>
      open_upvalues;
    std::unordered_map<std::string, Value> globals;
    mutable std::shared_mutex globals_mutex_; // Thread-safe access to globals
    std::unordered_set<std::string> immutable_globals_; // val-declared globals
    std::unordered_set<uint32_t> immutable_locals_; // val-declared local indices (per-frame)
  utils::RobinHoodHashMap<std::string, BytecodeHostFunction> host_functions;
  std::vector<std::string> host_function_names_; // Index -> name mapping
  utils::RobinHoodHashMap<std::string, Value> host_function_globals_; // Name -> HostFuncId Value
    std::unordered_map<std::string, uint64_t> host_function_gc_roots_; // Name -> pinned GC root ID
  std::vector<std::shared_ptr<std::unordered_map<std::string, Value>>> imported_module_globals_; // GC root for wrapped module functions
 
  
        // Function properties support (fn.prop = value for static state, memoization, etc.)
        std::unordered_map<uint32_t, ObjectRef> function_properties_; // function_index -> properties object
        std::unordered_map<uint32_t, ObjectRef> closure_properties_;  // closure_id -> properties object
        std::unordered_map<uint32_t, ObjectRef> hostfunc_properties_; // host_func_idx -> properties object
    std::unordered_map<CallbackId, DirectCallThunk> direct_call_thunks_;

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
// Keep the main chunk alive so hotkey/event callbacks can execute after __main__ returns
std::shared_ptr<BytecodeChunk> main_chunk_;
// Keep REPL chunks alive so closures/functions from previous lines remain valid
std::vector<std::shared_ptr<BytecodeChunk>> repl_chunks_;
// Keep persistent execution chunks alive so function objects remain valid
std::vector<std::shared_ptr<BytecodeChunk>> persistent_chunks_;

    // Canonical module loader for path resolution and caching
    ModuleLoader moduleLoader_;

    // Path to directory containing self-hosted module bytecode (e.g., "out/")
    std::string self_hosted_modules_path_;

    // Plugin loader for dynamic .so module discovery
    havel::Loader *pluginLoader_ = nullptr;

// Lazy module registry — descriptors are registered at startup,
// init functions are called on first use (import/access)
std::unordered_map<std::string, ModuleDescriptor> lazy_modules_;

    std::vector<std::unordered_map<std::string, Value>> globals_stack_;
 std::unordered_map<std::string, Value> rootGlobals_;

  // ObjectId of the _G heap object; UINT32_MAX = unset.
  // OBJECT_GET/OBJECT_SET/ITER_NEW check this to delegate to live globals maps.
    uint32_t globals_mirror_object_id_ = UINT32_MAX;

    std::unordered_map<uint32_t, uint64_t> backedge_counters_;

    // Coroutine support (Lua-style coroutines)
uint32_t current_coroutine_id_ = UINT32_MAX; // Currently executing coroutine (UINT32_MAX = main)
  std::unordered_map<uint32_t, uint32_t> coroutine_to_frame_; // Map coroutine ID to frame index

  
  // When an operation needs to wait (thread.join, channel.recv, etc)
  // it sets these flags to signal the VM to suspend the current fiber
  bool suspension_requested_ = false; // True if suspension needed
  uint8_t suspension_reason_ = 0; // Why is it suspending? (SuspensionReason enum value)
  void* suspension_context_ = nullptr; // Context pointer (thread_id, channel*, etc)
  bool executing_in_fiber_ = false; // True when executeOneStep runs with non-null current_fiber
  std::atomic<bool> jit_yield_requested_{false}; // Scheduler sets this to preempt JIT

// Last suspension info preserved after runDispatchLoop exits
// Used by processGoroutines to set WaitHandle on the goroutine
uint8_t last_suspension_reason_ = 0;
void* last_suspension_context_ = nullptr;

 
 // Maps thread_id -> Fiber* for fibers suspended on THREAD_JOIN
 // Used to unpark fibers when threads complete
 std::unordered_map<uint32_t, Fiber*> thread_wait_map_;
 mutable std::shared_mutex thread_wait_mutex_;

    // Thread/timeout/interval result storage for <- await
    std::unordered_map<uint32_t, Value> thread_results_;
    std::unordered_map<uint32_t, Value> timeout_results_;
    std::unordered_map<uint32_t, Value> interval_results_;

    // Closures captured by timer/thread callbacks (invisible to GC otherwise)
    std::unordered_map<uint32_t, Value> interval_captured_closures_;
    std::unordered_map<uint32_t, Value> timeout_captured_closures_;
    std::unordered_map<uint32_t, Value> thread_captured_closures_;

	
 class EventQueue* event_queue_ = nullptr;
 struct PendingTimerCallback {
 Value closure;
 uint32_t timer_id;
 bool is_timeout;
 };
  std::vector<PendingTimerCallback> pending_timer_callbacks_;
  std::mutex pending_timer_mutex_;
  bool timer_handler_registered_ = false;
 void executePendingTimerCallbacks();

 WatcherRegistry* watcher_registry_ = nullptr;
 Scheduler* scheduler_ = nullptr;

 struct SignalBinding {
     std::string name;
     uint32_t func_id;
     size_t ip;
     std::unordered_set<std::string> dependencies;
 };
 std::vector<SignalBinding> signalBindings_;

 void emitVariableChanged(const std::string& var_name);
  
  // Prototype system - methods on types (String, Array, Object)
  // Maps type name -> method name -> host function index
  std::unordered_map<std::string,
  std::unordered_map<std::string, uint32_t>>
  prototypes_;

  // Method overloading tracker (maps classObjId.methodName -> list of candidate function values)
  std::unordered_map<std::string, std::vector<Value>> overloaded_methods_;

  // Protocol system - protocol declarations and implementations
  // protocol_contracts_: protocol name -> set of required method names
  std::unordered_map<std::string, std::unordered_set<std::string>> protocol_contracts_;
  // protocol_impls_: protocol name -> set of type names that implement it
  std::unordered_map<std::string, std::unordered_set<std::string>> protocol_impls_;
  // type_protocols_: type name -> set of protocol names it implements
  std::unordered_map<std::string, std::unordered_set<std::string>> type_protocols_;

  // Host context for service access (non-owning)
  const HostContext *context_ = nullptr;

const BytecodeChunk *current_chunk = nullptr;
  bool debug_mode = false;
    bool host_globals_registered_ = false;
bool prototypes_registered_ = false;
VMConfig vm_config_{};
size_t max_call_depth_ = 16384;
size_t tail_call_depth_ = 0;
    bool profiling_enabled_ = false;
    bool trace_execution_ = false;
    std::array<uint64_t, 256> opcode_counts_{};
    uint64_t executed_instructions_ = 0;
    uint64_t max_instructions_ = 0; // 0 = no limit

    // System object initializer - called after registerDefaultHostGlobals()
    using SystemObjectInitializer = std::function<void(VM *)>;
    SystemObjectInitializer system_object_initializer_;

    // Timer check function - called periodically during execution
    TimerCheckFunction timer_check_func_;
    size_t instructions_since_timer_check_ = 0;
    size_t timer_check_interval_ = 1000;

    // Debugger state
    DebugStepMode debug_step_mode_ = DebugStepMode::Continue;
    std::unordered_map<std::string, std::unordered_set<uint32_t>> debug_breakpoints_;
    bool debugger_attached_ = false;
    bool debug_break_on_step_ = false;
    size_t debug_step_frame_depth_ = 0;
    std::function<void()> debug_break_cb_;
    std::unordered_set<std::string> debug_function_breakpoints_;
    bool debug_break_on_throw_ = false;
    bool debug_break_on_uncaught_ = false;
    std::atomic<bool> debug_pause_requested_{false};
    std::string debug_last_break_file_;
    uint32_t debug_last_break_line_ = 0;
    size_t debug_last_break_depth_ = 0;

  template <typename T> T getValue(const Value &value);
 std::string toStringInternal(const Value &value,
                                  std::unordered_set<uint32_t> &visitedIds,
                                  int depth);
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
	bool execCollectionOp(const Instruction &instruction);
	bool execConcurrencyOp(const Instruction &instruction);
	bool execControlFlowOp(const Instruction &instruction);
	bool execBuiltinOp(const Instruction &instruction);

  void doCall(Value callee_value, std::vector<Value> args);
  void doTailCall(Value callee_value,
                  std::vector<Value> args); // TCO
  void runDispatchLoop(size_t stop_frame_depth);
  void runDispatchFast(size_t stop_frame_depth);
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
    std::optional<std::string> resolveKey(const Value &value) const;
    bool valuesEqualDeep(const Value &left, const Value &right) const;
  bool valuesEqualDeep(const Value &left, const Value &right,
                       std::unordered_set<uint64_t> &visited_array_pairs,
                       std::unordered_set<uint64_t> &visited_object_pairs) const;


  std::vector<Value> stackValuesForRoots() const;
  std::vector<uint32_t> activeClosureIdsForRoots() const;
    void maybeCollectGarbage();
    void collectGarbage();
    void stepGarbageCollection(size_t work_budget = 128);
    void drainFinalizers();
    void registerDefaultHostFunctions();
    void registerDefaultHostGlobals();
    void registerDefaultPrototypes();
  Value invokeHostFunction(const std::string &name, uint32_t arg_count);

public:
    // Value utility functions (public for prototype method implementations and JIT bridges)
    int64_t toIntPublic(const Value &value) const { return toInt(value); }
    double toFloatPublic(const Value &value) const { return toFloat(value); }
    bool toBoolPublic(const Value &value) const { return toBool(value); }
    bool valuesEqualDeepPublic(const Value &left, const Value &right) const { return valuesEqualDeep(left, right); }
    Value callFunctionSyncPublic(const Value &fn, const std::vector<Value> &args) { return callFunctionSync(fn, args); }
    std::optional<std::string> resolveKeyPublic(const Value &value) const { return resolveKey(value); }
    void pushStackPublic(Value value) { pushStack(std::move(value)); }
    Value popStackPublic() { return popStack(); }
    void loadFiberStatePublic(Fiber* fiber) { loadFiberState(fiber); }
    void saveFiberStatePublic(Fiber* fiber) { saveFiberState(fiber); }
    // Replace top-of-stack with a new value (used when resuming from await)
    void replaceStackTop(Value value) {
        if (!stack.empty()) {
            stack.top() = std::move(value);
        } else {
            pushStack(std::move(value));
        }
    }
  // Upvalue/closure access for JIT bridges
  uint32_t currentClosureIdPublic() const { return currentFrame().closure_id; }
  GCHeap::RuntimeClosure* currentClosurePublic() {
    uint32_t id = jit_active_closure_id_ != 0 ? jit_active_closure_id_
                                               : currentFrame().closure_id;
    if (id == 0) return nullptr;
    return heap_.closure(id);
  }
  uint32_t setJITActiveClosurePublic(uint32_t closure_id) {
    uint32_t prev = jit_active_closure_id_;
    jit_active_closure_id_ = closure_id;
    return prev;
  }
  Value readLocalPublic(uint32_t abs_index) {
    ensureLocalIndex(abs_index);
    return locals[abs_index];
  }
  void writeLocalPublic(uint32_t abs_index, Value value) {
    ensureLocalIndex(abs_index);
    locals[abs_index] = std::move(value);
  }
  uint32_t toAbsoluteLocalPublic(uint32_t local_index) { return toAbsoluteLocal(local_index); }
  void closeFrameUpvaluesPublic(uint32_t locals_base, uint32_t locals_end) { closeFrameUpvalues(locals_base, locals_end); }
  size_t currentLocalsBasePublic() const { return frame_count_ > 0 ? currentFrame().locals_base : 0; }
  const Value* getLocalsPointerPublic(size_t base) const { 
    if (base >= locals.size()) return nullptr;
    return &locals[base]; 
  }
    void pushFramePublic(const BytecodeFunction* function, size_t ip, size_t locals_base, uint32_t closure_id) {
        if (frame_count_ >= frame_arena_.size()) {
 frame_arena_.push_back(CallFrame{function, nullptr, ip, locals_base, closure_id, {}, {}, {}, {}});
 } else {
 frame_arena_[frame_count_] = CallFrame{function, nullptr, ip, locals_base, closure_id, {}, {}, {}, {}};
        }
        frame_count_++;
    }
  size_t currentLocalsSizePublic() const { return locals.size(); }
  std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>& openUpvaluesPublic() { return open_upvalues; }
  void doTailCallPublic(Value callee_value, std::vector<Value> args) {
    doTailCall(std::move(callee_value), std::move(args));
  }
  
  
  void setJitTailCall(bool occurred) { jit_tail_call_occurred_ = occurred; }
  bool hasJitTailCall() const { return jit_tail_call_occurred_; }

  const BytecodeFunction* currentFunction() const {
    return frame_count_ > 0 ? frame_arena_[frame_count_ - 1].function : nullptr;
  }
  void runDispatchLoopPublic(size_t stop_frame_depth) { runDispatchLoop(stop_frame_depth); }
  size_t frameCountPublic() const { return frame_count_; }
  void tryEnterPublic(uint32_t catch_ip, uint32_t finally_ip,
                      size_t stack_depth) {
    currentFrame().try_stack.push_back(
        TryHandler{.catch_ip = catch_ip,
                   .finally_ip = finally_ip,
                   .finally_return_ip = 0,
                   .stack_depth = stack_depth});
  }
  void tryExitPublic() { if (!currentFrame().try_stack.empty()) currentFrame().try_stack.pop_back(); }
  Value currentExceptionPublic() const { return has_current_exception_ ? current_exception_ : Value::makeNull(); }
  bool hasCurrentExceptionPublic() const { return has_current_exception_; }
    void setCurrentExceptionPublic(const Value& v) { has_current_exception_ = true; current_exception_ = v; }

    // OBJECT_GET_RAW support — dynamic key lookup with class/parent chain walk
    uint32_t globalsMirrorObjectId() const { return globals_mirror_object_id_; }
    uint64_t objectShapeVersion(uint32_t obj_id) const {
        auto *obj = heap_.object(obj_id);
        return obj ? obj->shape_version.load(std::memory_order_relaxed) : 0;
    }

    uint64_t arrayVersion(uint32_t array_id) const {
        return heap_.arrayVersion(array_id);
    }

    uint64_t setVersion(uint32_t set_id) const {
        return heap_.setVersion(set_id);
    }

    uint64_t objectLookupVersion(uint32_t obj_id) const {
        uint64_t hash = 1469598103934665603ULL;
        const GCHeap::ObjectEntry *current = heap_.object(obj_id);
        for (size_t depth = 0; current && depth < 8; ++depth) {
            const uint64_t version = current->shape_version.load(std::memory_order_relaxed);
            hash ^= version + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
            auto *parent_val = current->get("__proto");
            if (!parent_val) parent_val = current->get("__class");
            if (!parent_val) parent_val = current->get("__struct");
            if (!parent_val) parent_val = current->get("__parent");
            if (parent_val && parent_val->isObjectId()) {
                current = heap_.object(parent_val->asObjectId());
            } else {
                current = nullptr;
            }
        }
        return hash;
    }
    Value objectGetWithClassChain(uint32_t obj_id, const std::string& key) {
        GCHeap::ObjectEntry *current_obj = heap_.object(obj_id);
        while (current_obj) {
            auto *val = current_obj->get(key);
            if (val) return *val;
            auto* parent_val = current_obj->get("__proto");
            if (!parent_val) parent_val = current_obj->get("__class");
            if (!parent_val) parent_val = current_obj->get("__struct");
            if (!parent_val) parent_val = current_obj->get("__parent");
            if (parent_val && parent_val->isObjectId()) {
                current_obj = heap_.object(parent_val->asObjectId());
            } else {
                current_obj = nullptr;
            }
        }
        return Value::makeNull();
    }
Value lookupGlobalByKey(const std::string& key) {
    auto it = globals.find(key);
    if (it != globals.end()) return it->second;
    auto hostIt = host_function_globals_.find(key);
    if (hostIt != host_function_globals_.end()) return hostIt->second;
    return Value::makeNull();
  }

    // Backedge loop detection
    void recordBackedgePublic(uint32_t ip) {
        ++backedge_counters_[ip];
    }

  // Direct invocation (bypasses stack, takes args as vector)
  Value invokeHostFunctionDirect(const std::string &name,
                                 const std::vector<Value> &args);

  VM();
VM(const VMConfig& cfg);
VM(const HostContext &ctx);
VM(const HostContext &ctx, const VMConfig& cfg);
  ~VM() override;
  Value execute(const BytecodeChunk &chunk,
                const std::string &function_name,
                const std::vector<Value> &args = {}) override;
  // Execute persistently (preserves globals and heap - for REPL)
  Value executePersistent(const BytecodeChunk &chunk,
                          const std::string &function_name,
                          const std::vector<Value> &args = {});
  
  
  // Evaluate a condition bytecode (for reactive watchers)
  // Executes condition code and returns result as boolean
  // Used by WatcherRegistry to re-evaluate conditions on variable changes
  // @param func_index The function index in bytecode (condition function)
  // @param ip Optional instruction pointer within function (default: 0)
  // @return Boolean result of condition evaluation
   bool evaluateConditionBytecode(uint32_t func_index, uint32_t ip = 0);

   Value evaluateExpressionBytecode(uint32_t func_index, size_t ip = 0);

   void registerSignal(const std::string& name, uint32_t func_id);
   void processSignalBindings(const std::string& changed_var);
  
  
  // Execute exactly one bytecode instruction in the current fiber
  // Returns immediately after one instruction (never blocks)
  // Used by main event loop for cooperative fiber scheduling
  // @param current_fiber The Fiber to execute in (must be RUNNABLE state)
  // @return VMExecutionResult indicating what happened
  VMExecutionResult executeOneStep(Fiber *current_fiber);
  
  
    // Load fiber's state into VM's global execution state
    // Must be called before executeOneStep() to restore suspended fiber
    // @param fiber The Fiber to load (must have SUSPENDED or pending state)
    void loadFiberState(Fiber *fiber);

    // Save VM's current execution state back to fiber
    // Must be called after executeOneStep() to persist execution progress
    // @param fiber The Fiber to save to (preserves IP for resumption)
    void saveFiberState(Fiber *fiber);

    // Initialize a newly-spawned goroutine for first execution
// Sets up the initial call frame, resolves chunk from closure if needed,
// and copies arguments into locals. If the function is JIT-compiled,
// executes it via JIT directly and returns JITExecuted.
// @param function_id Bytecode function index to execute
// @param closure_id Closure context (0 for plain functions)
// @param args Arguments to pass to the function
// @return GoroutineCallResult indicating success and execution mode
enum class GoroutineCallResult { Failed, Interpreter, JITExecuted };
GoroutineCallResult startGoroutineCall(uint32_t function_id, uint32_t closure_id,
    const std::vector<Value> &args);

  
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

    // ========== THREAD RESULTS (for await thread join) ==========
    // Get the result stored for a completed thread (used by ExecutionEngine to
    // populate wait_handle.resume_value when unparking a waiting goroutine)
    Value getThreadResult(uint32_t thread_id) const {
        auto it = thread_results_.find(thread_id);
        if (it != thread_results_.end()) return it->second;
        return Value::makeNull();
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
  std::string toString(const Value &value);
  const std::string* getStringPtr(const Value &value) const;
  bool toBoolPublic(const Value &value);
  void setDebugMode(bool enabled) override;

  struct DebugFrameInfo {
    std::string function_name;
    std::string source_file;
    uint32_t line = 0;
    uint32_t column = 0;
    size_t frame_depth = 0;
  };

  struct DebugVarInfo {
    std::string name;
    std::string type;
    std::string value;
  };

  DebugStepMode debugStepMode() const { return debug_step_mode_; }
  void setDebugStepMode(DebugStepMode mode) { debug_step_mode_ = mode; }
  void setDebugStepFrameDepth(size_t depth) { debug_step_frame_depth_ = depth; }

  using DebugBreakCallback = std::function<void()>;
  void setDebugBreakCallback(DebugBreakCallback cb) { debug_break_cb_ = std::move(cb); }

  void setBreakpoint(const std::string& file, uint32_t line);
  void clearBreakpoint(const std::string& file, uint32_t line);
  void clearAllBreakpoints();
  bool hasBreakpoint(const std::string& file, uint32_t line) const;

  void setFunctionBreakpoint(const std::string& name) { debug_function_breakpoints_.insert(name); }
  void clearFunctionBreakpoint(const std::string& name) { debug_function_breakpoints_.erase(name); }
  void clearAllFunctionBreakpoints() { debug_function_breakpoints_.clear(); }
  bool hasFunctionBreakpoint(const std::string& name) const {
    return debug_function_breakpoints_.count(name) > 0;
  }

  void setBreakOnThrow(bool b) { debug_break_on_throw_ = b; }
  bool breakOnThrow() const { return debug_break_on_throw_; }
  void setBreakOnUncaught(bool b) { debug_break_on_uncaught_ = b; }
  bool breakOnUncaught() const { return debug_break_on_uncaught_; }

  Value currentExceptionValue() const { return current_exception_; }

  bool isPauseRequested() const { return debug_pause_requested_.load(); }
  void requestPause() { debug_pause_requested_.store(true); }

  std::vector<DebugFrameInfo> getStackFrames() const;
  DebugFrameInfo getCurrentFrameInfo() const;
  std::vector<DebugVarInfo> getLocals(int depth = -1);
  std::vector<DebugVarInfo> getDebugGlobals();
  Value evaluateInFrame(const std::string& expr, int depth = -1);

  bool checkDebugBreak();
  void attachDebugger();
  void detachDebugger();
  bool isDebuggerAttached() const { return debugger_attached_; }

  void registerHostFunction(const std::string &name,
                            BytecodeHostFunction function) override;
  void registerHostFunction(const std::string &name, size_t arity,
                            BytecodeHostFunction function);
  bool hasHostFunction(const std::string &name) const override;
  
  
  // Spawn a new goroutine (fiber) to execute a closure or function concurrently
  // @param callee The closure or function object to execute
  // @param args Arguments to pass to the function
  // @return Goroutine ID
  uint32_t spawnGoroutine(const Value &callee, const std::vector<Value> &args = {});

    // Spawn a goroutine from a registered callback
    uint32_t spawnCallback(CallbackId id, const std::vector<Value> &args = {});

    // Spawn a goroutine from a registered callback with explicit priority
    uint32_t spawnCallback(CallbackId id, FiberPriority priority, const std::vector<Value> &args = {});

    // Create a persistent goroutine for a hotkey callback.
    // Unlike spawnCallback, this goroutine:
    //   - Is immediately parked (Suspended) after creation
    //   - Is recycled on each trigger via Scheduler::requeueFront()
    //   - Never transitions to Done (re-suspends in handleReturned)
    // This eliminates per-press goroutine allocation for hotkeys.
    uint32_t createPersistentHotkeyCallback(CallbackId id, FiberPriority priority,
        const std::vector<Value> &args = {}, HotkeyPolicy policy = HotkeyPolicy(0),
        const std::string &alias = "");

    // Build a direct-call thunk for a hotkey callback.
    // Analyzes the callback's bytecode; if it's host-only (only calls host
    // functions with pre-resolvable arguments), returns a thunk that invokes
    // the host functions directly, bypassing VM dispatch entirely.
    DirectCallThunk buildDirectCallThunk(CallbackId id);

    // Retrieve a stored thunk by callback ID (empty if none).
    DirectCallThunk getDirectCallThunk(CallbackId id) const {
        auto it = direct_call_thunks_.find(id);
        if (it != direct_call_thunks_.end()) return it->second;
        return {};
    }
    void storeDirectCallThunk(CallbackId id, DirectCallThunk thunk) {
        direct_call_thunks_[id] = std::move(thunk);
    }

    // Execute a DirectCallThunk: calls each host function directly without
    // VM dispatch. Returns true if all calls succeeded.
    bool executeDirectCallThunk(const DirectCallThunk& thunk);

  void garbageCollectionSafePoint(size_t work_budget = 0);

  
  void requestSuspension(uint8_t reason, void* context = nullptr) {
    suspension_requested_ = true;
    suspension_reason_ = reason;
    suspension_context_ = context;
  }
  bool isSuspensionRequested() const { return suspension_requested_; }
  void clearSuspensionRequest() { suspension_requested_ = false; }
  // JIT yield-point: scheduler calls this to request preemption of
  // a long-running JIT-compiled function. The JIT's havel_vm_check_yield()
  // stub reads this flag and throws JitCoroutineSignal if set.
  void requestJitYield() { jit_yield_requested_.store(true, std::memory_order_release); }
  bool consumeJitYieldRequest() {
    return jit_yield_requested_.exchange(false, std::memory_order_acq_rel);
  }
uint8_t getSuspensionReason() const { return suspension_reason_; }
void* getSuspensionContext() const { return suspension_context_; }

uint8_t getLastSuspensionReason() const { return last_suspension_reason_; }
void* getLastSuspensionContext() const { return last_suspension_context_; }
void clearLastSuspension() { last_suspension_reason_ = 0; last_suspension_context_ = nullptr; }

  uint32_t getHostFunctionIndex(const std::string &name);
  void throwError(const std::string &msg);
  
  
  using HotFunctionCallback = std::function<void(const BytecodeFunction&)>;
  void setHotFunctionCallback(HotFunctionCallback cb) { hot_func_cb_ = std::move(cb); }

  
  void setJITCompiler(JITCompiler* jit) { jit_compiler_ = jit; }
  JITCompiler* getJITCompiler() const { return jit_compiler_; }

  // System object initializer - called after registerDefaultHostGlobals()
  void setSystemObjectInitializer(SystemObjectInitializer init) {
    system_object_initializer_ = std::move(init);
  }

  void setMaxCallDepth(size_t value);
    void setProfilingEnabled(bool enabled) { profiling_enabled_ = enabled; }
    void setTraceExecution(bool enabled) { trace_execution_ = enabled; }
    bool isTraceExecution() const { return trace_execution_; }
    uint64_t executedInstructionCount() const { return executed_instructions_; }
    void setMaxInstructions(uint64_t limit) { max_instructions_ = limit; }
    uint64_t maxInstructions() const { return max_instructions_; }
    uint64_t opcodeCount(OpCode opcode) const {
        return opcode_counts_[static_cast<uint8_t>(opcode)];
    }

void setGcAllocationBudget(size_t value) { heap_.setAllocationBudget(value); }
void runGarbageCollection() { collectGarbage(); }
GCHeap::Stats gcStats() const { return heap_.stats(); }
GCHeap& getHeap() { return heap_; }
const GCHeap& getHeap() const { return heap_; }
const VMConfig& vmConfig() const { return vm_config_; }
uint64_t getMemoryUsage() const { return heap_.approxHeapBytes(); }
uint64_t getHeapMaxBytes() const { return heap_.heapMaxBytes(); }
  
	
 void setEventQueue(class EventQueue* eq);
	class EventQueue* getEventQueue() { return event_queue_; }
        void processPendingEvents();

	void setWatcherRegistry(WatcherRegistry* wr) { watcher_registry_ = wr; }
	WatcherRegistry* getWatcherRegistry() const { return watcher_registry_; }

	void setScheduler(Scheduler* sched) { scheduler_ = sched; }
 	Scheduler* getScheduler() const { return scheduler_; }

    // When true, the script requested program exit (via exit() host function)
    std::atomic<bool> exit_requested_{false};
    bool exitRequested() const { return exit_requested_.load(); }
    // The exit code passed to the exit() function
    std::atomic<int> exit_code_{0};
  
    void setGlobal(std::string name, Value value) {
        auto key = name;
        globals[std::move(name)] = std::move(value);
        emitVariableChanged(key);
    }
  void eraseGlobal(const std::string &name) {
    globals.erase(name);
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
    Value execLengthOp(Value v);
 Value execLengthOpPublic(Value v) { return execLengthOp(v); }
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

    void addIntervalResult(uint32_t id, Value result) { interval_results_[id] = std::move(result); }
    void addTimeoutResult(uint32_t id, Value result) { timeout_results_[id] = std::move(result); }
    Value getIntervalResult(uint32_t id) const {
        auto it = interval_results_.find(id);
        return (it != interval_results_.end()) ? it->second : Value::makeNull();
    }
    Value getTimeoutResult(uint32_t id) const {
        auto it = timeout_results_.find(id);
        return (it != timeout_results_.end()) ? it->second : Value::makeNull();
    }

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

  // Protocol system
  void registerProtocol(const std::string &protocolName,
                        const std::unordered_set<std::string> &methods);
  void registerProtocolImpl(const std::string &protocolName,
                            const std::string &typeName);
  bool typeImplementsProtocol(const std::string &typeName,
                              const std::string &protocolName) const;
  std::unordered_set<std::string> getTypeProtocols(const std::string &typeName) const;
  std::unordered_set<std::string> getProtocolMethods(const std::string &protocolName) const;
  std::vector<std::string> getProtocolNames() const;
  std::string getTypeName(const Value &value) const;

  // Get registered host functions (for copying to options)
  utils::RobinHoodHashMap<std::string, BytecodeHostFunction> &getHostFunctions() {
    return host_functions;
  }
  const utils::RobinHoodHashMap<std::string, BytecodeHostFunction> &
  getHostFunctions() const {
    return host_functions;
  }
  const std::vector<std::string>& getHostFunctionNames() const {
    return host_function_names_;
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

  // ========== IMAGE HELPERS ==========
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

    Value deepMaterializeStrings(Value value, const BytecodeChunk* chunk);
Value deepMaterializeStrings(Value value, const BytecodeChunk* chunk, std::unordered_set<uint32_t>& visited);
  Value deepWrapModuleFunctions(Value value, std::shared_ptr<BytecodeChunk> chunk,
                                std::shared_ptr<std::unordered_map<std::string, Value>> moduleGlobals,
                                const std::string& canonicalKey, const std::string& fieldPath,
                                int depth = 0, std::unordered_set<uint32_t>* visited = nullptr);

    Value loadModule(const std::string& path);
  Value loadScript(const std::string& path);
void registerLazyModule(const std::string &name, std::function<void(struct VMApi&)> initFn, const std::vector<std::string> &aliases = {});
  bool ensureModuleLoaded(const std::string &name);
  bool isLazyModuleRegistered(const std::string &name) const;
  void activateLazyModule(const std::string &name);
bool isLazyModuleLoaded(const std::string &name) const;
    void addModuleSearchPath(const std::string& path) { moduleLoader_.addSearchPath(path); }
    void setCurrentScriptDir(const std::string& dir) { current_script_dir_ = dir; }

    ModuleLoader& moduleLoader() { return moduleLoader_; }
    void setPluginLoader(havel::Loader *loader) { pluginLoader_ = loader; }
    havel::Loader *pluginLoader() const { return pluginLoader_; }

 // Get the current bytecode chunk (for execution contexts)
  const BytecodeChunk *getCurrentChunk() const { return current_chunk; }
  void setCurrentChunkPublic(const BytecodeChunk* chunk) { current_chunk = chunk; }
 void setCurrentChunk(const BytecodeChunk *chunk) { current_chunk = chunk; }
    void storeMainChunk(std::shared_ptr<BytecodeChunk> chunk) {
        main_chunk_ = std::move(chunk);
        current_chunk = main_chunk_.get();
    }
    void setMainChunkShared(const std::shared_ptr<BytecodeChunk>& chunk) {
        main_chunk_ = chunk;
        current_chunk = main_chunk_.get();
    }
 const std::shared_ptr<BytecodeChunk>& getMainChunk() const { return main_chunk_; }
  std::unordered_map<std::string, Value>& getGlobals() { return globals; }
  auto& hostFunctionGlobals() { return host_function_globals_; }
    void storeReplChunk(std::shared_ptr<BytecodeChunk> chunk) {
        repl_chunks_.push_back(chunk);
        if (repl_chunks_.size() > 64) {
            repl_chunks_.erase(repl_chunks_.begin());
        }
        current_chunk = chunk.get();
    }
void storePersistentChunk(std::shared_ptr<BytecodeChunk> chunk) {
        persistent_chunks_.push_back(std::move(chunk));
        if (persistent_chunks_.size() > 64) {
            persistent_chunks_.erase(persistent_chunks_.begin());
        }
}

        void clearPersistentChunks() {
            persistent_chunks_.clear();
        }

  // Resolve a Value that might be a string to an actual string
  std::string resolveStringKey(const Value &value) const;

  /** Injected embedder context; null if VM was default-constructed. */
  const HostContext *hostContext() const { return context_; }

private:
    std::atomic<uint32_t> active_hotkey_executions_{0};
    mutable std::recursive_mutex execution_mutex_; // Main VM execution lock
    bool jit_tail_call_occurred_ = false;

    uint32_t app_args_array_id_ = 0;
    std::function<void()> restart_callback_;
    HotFunctionCallback hot_func_cb_;
    JITCompiler* jit_compiler_ = nullptr;
    bool tiering_enabled_ = false;
    uint64_t tier1_threshold_ = 1000;
    uint64_t tier2_threshold_ = 10000;
    std::unordered_set<std::string> tier1_compiled_;
    std::unordered_set<std::string> tier2_compiled_;
    std::unordered_set<std::string> tier2_queued_or_compiling_;
    std::mutex tier2_queue_mutex_;
    std::queue<BytecodeFunction> tier2_queue_;
    std::thread tier2_worker_;
    std::atomic<bool> tier2_worker_running_{false};
    std::atomic<bool> vm_in_execute_{false};
    std::atomic<uint64_t> tier1_transition_count_{0};
    std::atomic<uint64_t> tier2_enqueue_count_{0};
    std::atomic<uint64_t> tier2_compile_count_{0};
    std::atomic<uint64_t> tier2_skip_duplicate_count_{0};
    bool tier2_flush_on_shutdown_ = false;
    uint32_t jit_active_closure_id_ = 0;
    std::function<void(VM&)> post_reset_setup_;
    int gc_suspend_counter_ = 0;
    void* serviceRegistry_ = nullptr;

public:
    bool isInExecute() const { return vm_in_execute_.load(std::memory_order_acquire); }
    void setServiceRegistry(void* sr) { serviceRegistry_ = sr; }
    void* getServiceRegistry() const { return serviceRegistry_; }

    void setPostResetSetup(std::function<void(VM&)> cb) { post_reset_setup_ = std::move(cb); }

    void suspendGC() { gc_suspend_counter_++; }
    void resumeGC() {
        if (--gc_suspend_counter_ <= 0) {
            gc_suspend_counter_ = 0;
            maybeCollectGarbage();
        }
    }
    void resumeGCWithFullCollect() {
        if (--gc_suspend_counter_ <= 0) {
            gc_suspend_counter_ = 0;
            collectGarbage();
        }
    }
    bool gcSuspended() const { return gc_suspend_counter_ > 0; }

public:
  void setAppArgs(uint32_t array_id) {
    app_args_array_id_ = array_id;
    auto it = globals.find("app");
    if (it != globals.end() && it->second.isObjectId()) {
      ObjectRef ref{it->second.asObjectId(), true};
      setHostObjectField(ref, "args", Value::makeArrayId(array_id));
    }
  }
  void setRestartCallback(std::function<void()> cb) { restart_callback_ = std::move(cb); }
  std::recursive_mutex& getExecutionMutex() const { return execution_mutex_; }
};

} // namespace havel::compiler
