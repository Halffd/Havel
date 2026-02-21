#pragma once

#include "Bytecode.h"
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace havel {
namespace ast {
struct Program;
struct Expression;
struct BinaryExpression;
struct CallExpression;
struct ArrayLiteral;
struct ObjectLiteral;
struct Statement;
struct FunctionDeclaration;
struct IfStatement;
struct WhileStatement;
struct ReturnStatement;
struct LetDeclaration;
} // namespace ast
} // namespace havel

namespace havel::compiler {

// Forward declarations
class BytecodeCompiler;
class BytecodeInterpreter;
class JITCompiler;
class HavelBytecodeCompiler;

// Type aliases
typedef std::vector<BytecodeValue> BytecodeValueVector;

// Debug options for bytecode compiler
struct DebugOptions {
  bool bytecode = false;
};

class HavelBytecodeCompiler : public BytecodeCompiler {
private:
  std::unique_ptr<BytecodeChunk> chunk;
  DebugOptions debug;
  std::unique_ptr<BytecodeFunction> current_function;
  std::unordered_map<std::string, uint32_t> variable_indices;
  uint32_t next_var_index = 0;

  void emit(OpCode op) {
    current_function->instructions.emplace_back(op,
                                                std::vector<BytecodeValue>{});
  }
  void emit(OpCode op, BytecodeValue operand) {
    current_function->instructions.emplace_back(
        op, std::vector<BytecodeValue>{std::move(operand)});
  }
  uint32_t addConstant(const BytecodeValue &value);
  void compileExpression(const ast::Expression &expr);
  void compileBinaryExpression(const ast::BinaryExpression &binary);
  void compileCallExpression(const ast::CallExpression &call);
  void compileArrayLiteral(const ast::ArrayLiteral &array);
  void compileObjectLiteral(const ast::ObjectLiteral &obj);
  uint32_t getVariableIndex(const std::string &name);
  void compileStatement(const ast::Statement &stmt);
  void compileFunction(const ast::FunctionDeclaration &func);
  void compileIfStatement(const ast::IfStatement &stmt);
  void compileWhileStatement(const ast::WhileStatement &stmt);

public:
  HavelBytecodeCompiler(const DebugOptions &debug_opts = {})
      : debug(debug_opts) {}
  std::unique_ptr<BytecodeChunk> compile(const ast::Program &program) override;
};

} // namespace havel::compiler
