#pragma once

#include "Bytecode.h"
#include <stack>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

// Bytecode interpreter implementation
class HavelBytecodeInterpreter : public BytecodeInterpreter {
private:
  struct CallFrame {
    const BytecodeFunction *function = nullptr;
    size_t ip = 0;
    size_t locals_base = 0;
  };

  std::stack<BytecodeValue> stack;
  std::vector<BytecodeValue> locals;
  std::vector<CallFrame> frames;
  std::unordered_map<std::string, BytecodeValue> globals;
  const BytecodeChunk *current_chunk;
  bool debug_mode = false;

  // Helper functions
  template <typename T> T getValue(const BytecodeValue &value);
  const CallFrame &currentFrame() const;
  CallFrame &currentFrame();
  BytecodeValue getConstant(uint32_t index);
  void executeInstruction();
  void doCall(const std::string &function_name, uint8_t arg_count);

public:
  HavelBytecodeInterpreter() = default;
  BytecodeValue execute(const BytecodeChunk &chunk,
                        const std::string &function_name,
                        const std::vector<BytecodeValue> &args = {}) override;
  void setDebugMode(bool enabled) override;
};

} // namespace havel::compiler
