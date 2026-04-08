#pragma once

#include "../core/BytecodeIR.hpp"
#include "../gc/GC.hpp"
#include "VMImage.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {
struct HostContext; // Forward declaration
}

namespace havel::compiler {

// Opaque callback handle - systems can store this without knowing VM internals
using CallbackId = uint32_t;
constexpr CallbackId INVALID_CALLBACK_ID = 0;

// Forward declare error system
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

class VM : public BytecodeInterpreter {
public:
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
  bool has_current_exception_ = false;
  Value current_exception_ = nullptr;

  // Prototype system - methods on types (String, Array, Object)
  // Maps type name -> method name -> host function index
  std::unordered_map<std::string,
                     std::unordered_map<std::string, uint32_t>>
      prototypes_;

  // Host context for service access (non-owning)
  const ::havel::HostContext *context_ = nullptr;

  const BytecodeChunk *current_chunk = nullptr;
  bool debug_mode = false;
  size_t max_call_depth_ = 1024;
  bool profiling_enabled_ = false;
  std::array<uint64_t, 256> opcode_counts_{};
  uint64_t executed_instructions_ = 0;

  // System object initializer - called after registerDefaultHostGlobals()
  using SystemObjectInitializer = std::function<void(VM *)>;
  SystemObjectInitializer system_object_initializer_;

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
  VM(const ::havel::HostContext &ctx);
  ~VM() override;
  Value execute(const BytecodeChunk &chunk,
                const std::string &function_name,
                const std::vector<Value> &args = {}) override;
  // Execute persistently (preserves globals and heap - for REPL)
  Value executePersistent(const BytecodeChunk &chunk,
                          const std::string &function_name,
                          const std::vector<Value> &args = {});
  Value call(const Value &callee_value,
             const std::vector<Value> &args = {});
  std::string toString(const Value &value) const;
  void setDebugMode(bool enabled) override;
  void registerHostFunction(const std::string &name,
                            BytecodeHostFunction function) override;
  void registerHostFunction(const std::string &name, size_t arity,
                            BytecodeHostFunction function);
  bool hasHostFunction(const std::string &name) const override;
  uint32_t getHostFunctionIndex(const std::string &name);

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
  void setGlobal(std::string name, Value value) {
    globals[std::move(name)] = std::move(value);
  }
  [[nodiscard]] GCRoot makeRoot(const Value &value) {
    return GCRoot(*this, value);
  }
  ObjectRef createHostObject();
  ArrayRef createHostArray();
  StringRef createRuntimeString(std::string value);
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

  // Get the current bytecode chunk (for execution contexts)
  const BytecodeChunk *getCurrentChunk() const { return current_chunk; }

  // Resolve a Value that might be a string to an actual string
  std::string resolveStringKey(const Value &value) const;

  /** Injected embedder context; null if VM was default-constructed. */
  const ::havel::HostContext *hostContext() const { return context_; }
};

} // namespace havel::compiler
