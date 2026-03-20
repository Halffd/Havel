#pragma once

#include "BytecodeIR.hpp"
#include <array>
#include <memory>
#include <optional>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

// Bytecode interpreter implementation
class VM : public BytecodeInterpreter {
private:
  struct RuntimeClosure {
    uint32_t function_index = 0;
    struct UpvalueCell {
      bool is_open = false;
      uint32_t open_index = 0;
      BytecodeValue closed_value = nullptr;
    };
    std::vector<std::shared_ptr<UpvalueCell>> upvalues;
  };

  struct CallFrame {
    const BytecodeFunction *function = nullptr;
    size_t ip = 0;
    size_t locals_base = 0;
    uint32_t closure_id = 0;
  };

  std::stack<BytecodeValue> stack;
  std::vector<BytecodeValue> locals;
  std::vector<CallFrame> frames;
  std::unordered_map<uint32_t, RuntimeClosure> closures;
  std::unordered_map<uint32_t, std::shared_ptr<RuntimeClosure::UpvalueCell>>
      open_upvalues;
  uint32_t next_closure_id = 1;
  std::unordered_map<uint32_t, std::vector<BytecodeValue>> arrays_;
  std::unordered_map<uint32_t, std::unordered_map<std::string, BytecodeValue>>
      objects_;
  std::unordered_map<uint32_t, std::unordered_map<std::string, BytecodeValue>>
      sets_;
  uint32_t next_array_id_ = 1;
  uint32_t next_object_id_ = 1;
  uint32_t next_set_id_ = 1;
  size_t gc_allocation_budget_ = 1024;
  size_t gc_allocations_since_last_ = 0;
  std::unordered_map<uint64_t, BytecodeValue> external_roots_;
  uint64_t next_external_root_id_ = 1;
  std::unordered_map<std::string, BytecodeValue> globals;
  std::unordered_map<std::string, BytecodeHostFunction> host_functions;
  const BytecodeChunk *current_chunk;
  bool debug_mode = false;
  size_t max_call_depth_ = 1024;
  bool profiling_enabled_ = false;
  std::array<uint64_t, 256> opcode_counts_{};
  uint64_t executed_instructions_ = 0;

  // Helper functions
  template <typename T> T getValue(const BytecodeValue &value);
  const CallFrame &currentFrame() const;
  CallFrame &currentFrame();
  BytecodeValue getConstant(uint32_t index);
  void executeInstruction(const Instruction &instruction);
  void doCall(BytecodeValue callee_value, std::vector<BytecodeValue> args);
  void closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end);
  void maybeCollectGarbage();
  void markValue(const BytecodeValue &value, std::unordered_set<uint32_t> &marked_arrays,
                 std::unordered_set<uint32_t> &marked_objects,
                 std::unordered_set<uint32_t> &marked_sets,
                 std::unordered_set<uint32_t> &marked_closures) const;
  void collectGarbage();
  void registerDefaultHostFunctions();
  BytecodeValue invokeHostFunction(const std::string &name,
                                   uint32_t arg_count);

public:
  VM();
  BytecodeValue execute(const BytecodeChunk &chunk,
                        const std::string &function_name,
                        const std::vector<BytecodeValue> &args = {}) override;
  void setDebugMode(bool enabled) override;
  void registerHostFunction(const std::string &name,
                            BytecodeHostFunction function) override;
  void registerHostFunction(const std::string &name, size_t arity,
                            BytecodeHostFunction function);
  bool hasHostFunction(const std::string &name) const override;
  void setMaxCallDepth(size_t value) { max_call_depth_ = value; }
  void setProfilingEnabled(bool enabled) { profiling_enabled_ = enabled; }
  uint64_t executedInstructionCount() const { return executed_instructions_; }
  uint64_t opcodeCount(OpCode opcode) const {
    return opcode_counts_[static_cast<uint8_t>(opcode)];
  }
  void setGcAllocationBudget(size_t value) { gc_allocation_budget_ = value; }
  void runGarbageCollection() { collectGarbage(); }
  uint64_t pinExternalRoot(const BytecodeValue &value);
  bool unpinExternalRoot(uint64_t root_id);
};

} // namespace havel::compiler
