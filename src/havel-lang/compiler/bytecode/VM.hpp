#pragma once

#include "BytecodeIR.hpp"
#include "GC.hpp"
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
class HostContext; // Forward declaration
}

namespace havel::compiler {

// Opaque callback handle - systems can store this without knowing VM internals
using CallbackId = uint32_t;
constexpr CallbackId INVALID_CALLBACK_ID = 0;

// Error handling with stack traces
struct ScriptError final {
  BytecodeValue value;
  std::string message;
  std::string stackTrace;
  uint32_t line = 0;
  uint32_t column = 0;

  ScriptError(BytecodeValue val, const std::string &msg,
              const std::string &trace, uint32_t ln = 0, uint32_t col = 0)
      : value(std::move(val)), message(msg), stackTrace(trace), line(ln),
        column(col) {}

  const char *what() const noexcept { return message.c_str(); }
};

class VM : public BytecodeInterpreter {
public:
  class GCRoot {
  public:
    GCRoot() = default;
    GCRoot(VM &vm, const BytecodeValue &value) : vm_(&vm) {
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

    std::optional<BytecodeValue> get() const {
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

  std::stack<BytecodeValue> stack;
  std::vector<BytecodeValue> locals;
  std::vector<CallFrame> frame_arena_;
  size_t frame_count_ = 0;
  GCHeap heap_;
  std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>
      open_upvalues;
  std::unordered_map<std::string, BytecodeValue> globals;
  mutable std::shared_mutex globals_mutex_; // Thread-safe access to globals
  std::unordered_map<std::string, BytecodeHostFunction> host_functions;
  std::unordered_map<std::string, uint32_t> struct_type_ids_by_name_;
  std::unordered_map<std::string, uint32_t> class_type_ids_by_name_;
  bool has_current_exception_ = false;
  BytecodeValue current_exception_ = nullptr;

  // Prototype system - methods on types (String, Array, Object)
  // Maps type name -> method name -> function
  std::unordered_map<std::string,
                     std::unordered_map<std::string, HostFunctionRef>>
      prototypes_;

  // Host context for service access (non-owning)
  const class havel::HostContext *context_ = nullptr;

  const BytecodeChunk *current_chunk = nullptr;
  bool debug_mode = false;
  size_t max_call_depth_ = 1024;
  bool profiling_enabled_ = false;
  std::array<uint64_t, 256> opcode_counts_{};
  uint64_t executed_instructions_ = 0;

  // System object initializer - called after registerDefaultHostGlobals()
  using SystemObjectInitializer = std::function<void(VM *)>;
  SystemObjectInitializer system_object_initializer_;

  template <typename T> T getValue(const BytecodeValue &value);
  const CallFrame &currentFrame() const;
  CallFrame &currentFrame();
  BytecodeValue getConstant(uint32_t index);

  // State snapshot for re-entrant calls (HOF callbacks)
  struct ExecutionState {
    std::stack<BytecodeValue> stack;
    std::vector<BytecodeValue> locals;
    std::vector<CallFrame> frames;
    size_t frame_count = 0;
  };
  ExecutionState saveState() const;
  void restoreState(const ExecutionState &state);

  // Callback queue for non-reentrant execution
  struct PendingCall {
    BytecodeValue fn;
    std::vector<BytecodeValue> args;
    BytecodeValue *result; // Pointer to store result
    bool *completed;       // Flag to signal completion
  };
  std::vector<PendingCall> pending_calls;
  void scheduleCall(const BytecodeValue &fn,
                    const std::vector<BytecodeValue> &args,
                    BytecodeValue &result, bool &completed);
  void processPendingCalls();
  BytecodeValue callFunctionSync(const BytecodeValue &fn,
                                 const std::vector<BytecodeValue> &args);
  void executeInstruction(const Instruction &instruction);
  void doCall(BytecodeValue callee_value, std::vector<BytecodeValue> args,
              bool advance_caller_ip = true);
  void doTailCall(BytecodeValue callee_value,
                  std::vector<BytecodeValue> args); // TCO
  void runDispatchLoop(size_t stop_frame_depth);
  bool handleScriptThrow(const BytecodeValue &value);
  std::string buildStackTrace(size_t frame_count) const;
  void closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end);

  std::vector<BytecodeValue> stackValuesForRoots() const;
  std::vector<uint32_t> activeClosureIdsForRoots() const;
  void maybeCollectGarbage();
  void collectGarbage();
  void registerDefaultHostFunctions();
  void registerDefaultHostGlobals();
  BytecodeValue invokeHostFunction(const std::string &name, uint32_t arg_count);

public:
  VM();
  VM(const class havel::HostContext &ctx);
  ~VM() override;
  BytecodeValue execute(const BytecodeChunk &chunk,
                        const std::string &function_name,
                        const std::vector<BytecodeValue> &args = {}) override;
  // Execute persistently (preserves globals and heap - for REPL)
  BytecodeValue executePersistent(const BytecodeChunk &chunk,
                                  const std::string &function_name,
                                  const std::vector<BytecodeValue> &args = {});
  BytecodeValue call(const BytecodeValue &callee_value,
                     const std::vector<BytecodeValue> &args = {});
  void setDebugMode(bool enabled) override;
  void registerHostFunction(const std::string &name,
                            BytecodeHostFunction function) override;
  void registerHostFunction(const std::string &name, size_t arity,
                            BytecodeHostFunction function);
  bool hasHostFunction(const std::string &name) const override;

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
  void setGlobal(std::string name, BytecodeValue value) {
    globals[std::move(name)] = std::move(value);
  }
  [[nodiscard]] GCRoot makeRoot(const BytecodeValue &value) {
    return GCRoot(*this, value);
  }
  ObjectRef createHostObject();
  ArrayRef createHostArray();
  void setHostObjectField(ObjectRef object_ref, const std::string &key,
                          BytecodeValue value);
  void pushHostArrayValue(ArrayRef array_ref, BytecodeValue value);

  // Array helpers
  size_t getHostArrayLength(ArrayRef array_ref);
  BytecodeValue getHostArrayValue(ArrayRef array_ref, size_t index);
  void setHostArrayValue(ArrayRef array_ref, size_t index, BytecodeValue value);
  BytecodeValue popHostArrayValue(ArrayRef array_ref);
  void insertHostArrayValue(ArrayRef array_ref, size_t index,
                            BytecodeValue value);
  BytecodeValue removeHostArrayValue(ArrayRef array_ref, size_t index);

  // Range helpers
  bool isInRange(RangeRef range_ref, int64_t value);

  // Struct helpers
  uint32_t registerStructType(const std::string &name,
                              const std::vector<std::string> &fields);
  StructRef createStruct(uint32_t typeId, size_t fieldCount);
  BytecodeValue getStructField(StructRef struct_ref, size_t index);
  void setStructField(StructRef struct_ref, size_t index,
                      const BytecodeValue &value);
  uint32_t getStructTypeId(StructRef struct_ref);

  // Class helpers
  uint32_t registerClassType(const std::string &name,
                             const std::vector<std::string> &fields,
                             uint32_t parentTypeId = 0);
  ClassRef createClass(uint32_t typeId, size_t fieldCount,
                       uint32_t parentInstanceId = 0);
  uint32_t getClassParentTypeId(uint32_t typeId) const;
  void registerClassMethod(uint32_t typeId, const std::string &methodName,
                           uint32_t functionIndex);
  std::optional<uint32_t> findClassMethod(uint32_t typeId,
                                          const std::string &methodName) const;
  BytecodeValue getClassField(ClassRef class_ref, size_t index);
  void setClassField(ClassRef class_ref, size_t index,
                     const BytecodeValue &value);
  uint32_t getClassTypeId(ClassRef class_ref);

  // Copy a struct (value type semantics)
  StructRef copyStruct(StructRef struct_ref);

  // Enum helpers
  uint32_t registerEnumType(const std::string &name,
                            const std::vector<std::string> &variants);
  EnumRef createEnum(uint32_t typeId, uint32_t tag, size_t payloadCount);
  uint32_t getEnumTag(EnumRef enum_ref);
  BytecodeValue getEnumPayload(EnumRef enum_ref, size_t index);
  void setEnumPayload(EnumRef enum_ref, size_t index,
                      const BytecodeValue &value);

  // Membership helpers
  bool arrayContains(ArrayRef array_ref, const BytecodeValue &value);
  bool objectHasKey(ObjectRef object_ref, const std::string &key);

  // Iterator helpers
  IteratorRef createIterator(const BytecodeValue &iterable);
  BytecodeValue iteratorNext(IteratorRef iterRef);

  // Object helpers
  std::vector<std::string> getHostObjectKeys(ObjectRef object_ref);
  std::vector<std::pair<std::string, BytecodeValue>>
  getHostObjectEntries(ObjectRef object_ref);
  bool hasHostObjectField(ObjectRef object_ref, const std::string &key);
  BytecodeValue getHostObjectField(ObjectRef object_ref,
                                   const std::string &key);
  bool deleteHostObjectField(ObjectRef object_ref, const std::string &key);
  void setHostObjectFrozen(ObjectRef object_ref, bool frozen);
  void setHostObjectSealed(ObjectRef object_ref, bool sealed);

  // Function calling
  BytecodeValue callHostFunction(const BytecodeValue &fn,
                                 const std::vector<BytecodeValue> &args);

  // General function call (handles both VM closures and host functions)
  BytecodeValue callFunction(const BytecodeValue &fn,
                             const std::vector<BytecodeValue> &args);

  // Prototype system - methods on types
  void registerPrototypeMethod(const std::string &typeName,
                               const std::string &methodName,
                               HostFunctionRef method);
  std::optional<HostFunctionRef>
  getPrototypeMethod(const BytecodeValue &value, const std::string &methodName);
  std::vector<std::string> getPrototypeMethods(const BytecodeValue &value);

  // Get registered host functions (for copying to options)
  std::unordered_map<std::string, BytecodeHostFunction> &getHostFunctions() {
    return host_functions;
  }
  const std::unordered_map<std::string, BytecodeHostFunction> &
  getHostFunctions() const {
    return host_functions;
  }

  uint64_t pinExternalRoot(const BytecodeValue &value);
  bool unpinExternalRoot(uint64_t root_id);
  std::optional<BytecodeValue> externalRootValue(uint64_t root_id) const;
  size_t externalRootCount() const { return heap_.externalRootCount(); }

  // Callback system - VM owns closures, systems use opaque IDs
  CallbackId registerCallback(const BytecodeValue &closure);
  BytecodeValue invokeCallback(CallbackId id,
                               const std::vector<BytecodeValue> &args = {});
  void releaseCallback(CallbackId id);
  bool isValidCallback(CallbackId id) const;

  // Value utility functions
  bool isNull(const BytecodeValue &value) const;
  bool isTruthy(const BytecodeValue &value);
  std::optional<int64_t> parseDuration(const BytecodeValue &value) const;

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
    std::stack<BytecodeValue> stack;
    std::vector<BytecodeValue> locals;
    std::vector<CallFrame> frame_arena_;
    size_t frame_count_ = 0;
    std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>>
        open_upvalues;
    bool has_current_exception_ = false;
    BytecodeValue current_exception_ = nullptr;
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
    BytecodeValue invokeCallback(CallbackId id,
                                 const std::vector<BytecodeValue> &args = {});

    // Internal: execute single instruction in this context
    void executeInstructionInContext(const Instruction &instruction);

    // Check if context is valid (has parent VM)
    bool isValid() const { return parent_vm_ != nullptr; }
  };

  // Create a lightweight execution context that shares globals/heap but has
  // isolated stack This is the CORRECT way to execute hotkeys from threads
  VMExecutionContext createExecutionContext();

  // Thread-safe global variable access
  void setGlobalThreadSafe(const std::string &name, BytecodeValue value);
  std::optional<BytecodeValue>
  getGlobalThreadSafe(const std::string &name) const;

  // Get the current bytecode chunk (for execution contexts)
  const BytecodeChunk *getCurrentChunk() const { return current_chunk; }
};

} // namespace havel::compiler
