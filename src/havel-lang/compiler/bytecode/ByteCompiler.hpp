#pragma once

#include "LexicalResolver.hpp"
#include "BytecodeIR.hpp"
#include "../../ast/AST.h"
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Qt defines 'emit' as a macro - we need to undefine it for our method name
#ifdef emit
#undef emit
#endif

namespace havel::compiler {

class ByteCompiler : public BytecodeCompiler {
public:
    std::unique_ptr<BytecodeChunk> compile(const ast::Program& program) override;
    const LexicalResolutionResult& lexicalResolution() const {
      return lexical_resolution_;
    }

private:
    void emit(OpCode op);
    void emit(OpCode op, BytecodeValue operand);
    void emit(OpCode op, std::vector<BytecodeValue> operands);
    uint32_t addConstant(const BytecodeValue& value);
    uint32_t emitJump(OpCode op);
    void patchJump(uint32_t jump_instruction_index, uint32_t target);

    void compileFunction(const ast::FunctionDeclaration& function);
    void compileStatement(const ast::Statement& statement);
    void compileExpression(const ast::Expression& expression);
    void compileCallExpression(const ast::CallExpression& expression);
    void compileIfStatement(const ast::IfStatement& statement);
    void compileWhileStatement(const ast::WhileStatement& statement);
    void compileBlockStatement(const ast::BlockStatement& block);
    std::optional<std::string> getCalleeName(const ast::Expression& callee) const;
    const ResolvedBinding* bindingFor(const ast::Identifier& id) const;
    uint32_t declarationSlot(const ast::Identifier& id) const;
    void reserveLocalSlot(uint32_t slot);

    void enterFunction(BytecodeFunction&& function);
    void leaveFunction();
    void resetLocals();

    std::unique_ptr<BytecodeChunk> chunk;
    std::unique_ptr<BytecodeFunction> current_function;
    std::vector<std::unique_ptr<BytecodeFunction>> compiled_functions;
    std::unordered_map<std::string, uint32_t> function_indices_by_name_;
    uint32_t next_local_index = 0;
    LexicalResolutionResult lexical_resolution_;
};

} // namespace havel::compiler
