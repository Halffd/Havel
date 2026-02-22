#pragma once

#include "Bytecode.h"
#include "../ast/AST.h"
#include <memory>
#include <vector>

// Qt defines 'emit' as a macro - we need to undefine it for our method name
#ifdef emit
#undef emit
#endif

namespace havel::compiler {

class HavelBytecodeCompiler : public BytecodeCompiler {
public:
    std::unique_ptr<BytecodeChunk> compile(const ast::Program& program) override;

private:
    void emit(OpCode op);
    void emit(OpCode op, BytecodeValue operand);

    std::unique_ptr<BytecodeChunk> chunk;
    std::unique_ptr<BytecodeFunction> current_function;
};

} // namespace havel::compiler
