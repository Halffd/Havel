#include "BytecodeCompiler.hpp"

namespace havel::compiler {

std::unique_ptr<BytecodeChunk> HavelBytecodeCompiler::compile(const ast::Program& program) {
    chunk = std::make_unique<BytecodeChunk>();
    
    // TODO: Implement actual compilation logic
    // For now, return an empty chunk as a placeholder
    
    return std::move(chunk);
}

void HavelBytecodeCompiler::emit(OpCode op) {
    if (current_function) {
        current_function->instructions.emplace_back(op);
    }
}

void HavelBytecodeCompiler::emit(OpCode op, BytecodeValue operand) {
    if (current_function) {
        current_function->instructions.emplace_back(op, std::vector<BytecodeValue>{operand});
    }
}

} // namespace havel::compiler
