#pragma once

#include "BytecodeIR.hpp"
#include <memory>
#include <stack>
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
  std::unordered_map<std::string, BytecodeValue> globals;
  std::unordered_map<std::string, BytecodeHostFunction> host_functions;
  const BytecodeChunk *current_chunk;
  bool debug_mode = false;

  // Helper functions
  template <typename T> T getValue(const BytecodeValue &value);
  const CallFrame &currentFrame() const;
  CallFrame &currentFrame();
  BytecodeValue getConstant(uint32_t index);
  void executeInstruction(const Instruction &instruction);
  void doCall(BytecodeValue callee_value, std::vector<BytecodeValue> args);
  void closeFrameUpvalues(uint32_t locals_base, uint32_t locals_end);
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
  bool hasHostFunction(const std::string &name) const override;
};

} // namespace havel::compiler
