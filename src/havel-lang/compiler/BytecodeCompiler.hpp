#pragma once

#include "../ast/AST.h"
#include "../runtime/Interpreter.hpp"
#include "Bytecode.h"
#include <iostream>
#include <sstream>

namespace havel::compiler {

// Debug options for bytecode compiler
struct DebugOptions {
  bool bytecode = false;
};

class HavelBytecodeCompiler : public BytecodeCompiler {
private:
  std::unique_ptr<BytecodeChunk> chunk;
  size_t local_count = 0;
  DebugOptions debug;

  void emit(OpCode opcode, const std::vector<uint8_t> &operands = {});
  size_t addConstant(const BytecodeValue &value);
  void compileExpression(const ast::Expression &expr);
  void compileBinaryExpression(const ast::BinaryExpression &binary);
  void compileCallExpression(const ast::CallExpression &call);
  void compileArrayLiteral(const ast::ArrayLiteral &array);
  void compileObjectLiteral(const ast::ObjectLiteral &obj);
  size_t getVariableIndex(const std::string &name);

public:
  HavelBytecodeCompiler(const DebugOptions& debug_opts = {}) : debug(debug_opts) {}
  std::unique_ptr<BytecodeChunk> compile(const ast::Program &program) override;
};

} // namespace havel::compiler
