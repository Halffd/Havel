#pragma once

#include "BytecodeIR.hpp"
#include "Closure.hpp"
#include "GC.hpp"
#include <shared_mutex>
#include <stack>
#include <vector>
#include <unordered_map>

namespace havel::compiler {

// Forward declarations
class VM;
class BytecodeChunk;

// ============================================================================
// CallFrame - Represents a single function call frame
// ============================================================================
struct CallFrame {
  const BytecodeFunction* function = nullptr;
  size_t ip = 0;
  size_t localsBase = 0;
  uint32_t closureId = 0;
  std::vector<uint32_t> tryHandlers; // Indices into try handler table

  // Helper methods
  const Instruction& currentInstruction() const;
  const Instruction& nextInstruction();
  bool hasNext() const;
  void jump(uint32_t target);
};

// ============================================================================
// CallFrameManager - Manages the call stack and frame lifecycle
// ============================================================================
class CallFrameManager {
public:
  explicit CallFrameManager(size_t maxDepth = 1024);

  // Frame lifecycle
  CallFrame& pushFrame(const BytecodeFunction* function, size_t localsBase, uint32_t closureId = 0);
  void popFrame();

  // Frame access
  CallFrame& currentFrame();
  const CallFrame& currentFrame() const;
  bool hasFrames() const { return frameCount_ > 0; }
  size_t frameCount() const { return frameCount_; }

  // Stack depth management
  bool canPushFrame() const { return frameCount_ < maxDepth_; }
  size_t getMaxDepth() const { return maxDepth_; }
  void setMaxDepth(size_t depth) { maxDepth_ = depth; }

  // Frame navigation
  CallFrame& frameAt(size_t index);
  const CallFrame& frameAt(size_t index) const;

  // Stack trace generation
  std::string buildStackTrace() const;

  // Clear all frames
  void clear();

private:
  std::vector<CallFrame> frames_;
  size_t frameCount_ = 0;
  size_t maxDepth_ = 1024;
};

// ============================================================================
// VMStack - Manages the operand stack for the VM
// ============================================================================
class VMStack {
public:
  VMStack() = default;
  explicit VMStack(size_t initialCapacity);

  // Stack operations
  void push(const BytecodeValue& value);
  BytecodeValue pop();
  BytecodeValue& peek(size_t distance = 0);
  const BytecodeValue& peek(size_t distance = 0) const;

  // Bulk operations
  void pushMultiple(const std::vector<BytecodeValue>& values);
  std::vector<BytecodeValue> popMultiple(size_t count);

  // Stack inspection
  bool isEmpty() const { return stack_.empty(); }
  size_t size() const { return stack_.size(); }
  void clear() {
    while (!stack_.empty()) {
      stack_.pop();
    }
  }

  // Get all values for GC roots
  std::vector<BytecodeValue> getValues() const;

private:
  std::stack<BytecodeValue> stack_;
};

// ============================================================================
// VMLocals - Manages local variable slots
// ============================================================================
class VMLocals {
public:
  VMLocals() = default;
  explicit VMLocals(size_t initialCapacity);

  // Slot operations
  BytecodeValue& get(size_t index);
  const BytecodeValue& get(size_t index) const;
  void set(size_t index, const BytecodeValue& value);

  // Range operations
  void ensureCapacity(size_t capacity);
  size_t size() const { return locals_.size(); }
  void resize(size_t newSize);
  void clear() { locals_.clear(); }

  // Bulk operations for frame management
  size_t reserveSlots(size_t count);
  void releaseSlots(size_t count);

  // Get all values for GC roots
  std::vector<BytecodeValue> getValues() const;

private:
  std::vector<BytecodeValue> locals_;
};

// ============================================================================
// VMExecutionContext - Isolated execution context for async/threaded execution
// ============================================================================
class VMExecutionContext {
public:
  VMExecutionContext(VM& parent, const BytecodeChunk& chunk);

  // Execution
  BytecodeValue execute(const std::string& functionName,
                        const std::vector<BytecodeValue>& args = {});
  BytecodeValue callFunction(uint32_t functionIndex,
                              const std::vector<BytecodeValue>& args = {});
  BytecodeValue callClosure(uint32_t closureId,
                            const std::vector<BytecodeValue>& args = {});

  // Stack management
  void pushValue(const BytecodeValue& value);
  BytecodeValue popValue();

  // Frame management
  void enterFunction(uint32_t functionIndex, const std::vector<BytecodeValue>& args);
  void exitFunction();

  // Upvalue management
  void captureUpvalue(uint32_t localIndex);
  void closeUpvalues(uint32_t localsBase);

  // State queries
  bool isExecuting() const { return isExecuting_; }
  bool hasError() const { return !errorMessage_.empty(); }
  const std::string& getError() const { return errorMessage_; }

  // GC integration
  std::vector<BytecodeValue> getGCRoots() const;

private:
  VM& parent_;
  const BytecodeChunk& chunk_;

  // Execution state
  VMStack stack_;
  VMLocals locals_;
  CallFrameManager frames_;
  UpvalueManager upvalues_;

  // Control flags
  bool isExecuting_ = false;
  std::string errorMessage_;

  // Internal execution
  void executeInstruction(const Instruction& instruction);
  void executeBinaryOp(OpCode op);
  void executeCall(uint32_t argCount);
  void executeReturn();

  // Error handling
  void setError(const std::string& message);
  void clearError();
};

// ============================================================================
// VMGlobals - Thread-safe global variable storage
// ============================================================================
class VMGlobals {
public:
  void set(const std::string& name, const BytecodeValue& value);
  std::optional<BytecodeValue> get(const std::string& name) const;
  bool has(const std::string& name) const;
  void remove(const std::string& name);
  void clear();

  std::unordered_map<std::string, BytecodeValue> snapshot() const;
  void restore(const std::unordered_map<std::string, BytecodeValue>& snapshot);

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, BytecodeValue> globals_;
};

// ============================================================================
// VMHostBridge - Interface for calling host functions from VM
// ============================================================================
class VMHostBridge {
public:
  using HostFunction = std::function<BytecodeValue(const std::vector<BytecodeValue>&)>;

  void registerFunction(const std::string& name, HostFunction func);
  bool hasFunction(const std::string& name) const;
  BytecodeValue call(const std::string& name, const std::vector<BytecodeValue>& args);

  std::vector<std::string> getRegisteredFunctions() const;

private:
  std::unordered_map<std::string, HostFunction> functions_;
};

} // namespace havel::compiler
