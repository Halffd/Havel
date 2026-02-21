#include "BytecodeCompiler.hpp"
#include "../ast/AST.h"
#include "../runtime/Interpreter.hpp"
#include "Bytecode.h"
#include <variant>

namespace havel::compiler {

std::unique_ptr<BytecodeChunk>
HavelBytecodeCompiler::compile(const ast::Program &program) {
  chunk = std::make_unique<BytecodeChunk>();
  // `Create main function
  current_function = std::make_unique<BytecodeFunction>("main", 0, 0);
  variable_indices.clear();
  next_var_index = 0;

  // Compile program statements
  for (const auto &stmt : program.body) {
    compileStatement(*stmt);
  }

  // Add return statement if none exists
  emit(OpCode::LOAD_CONST, BytecodeValue{nullptr});
  emit(OpCode::RETURN);

  chunk->addFunction(*current_function);

  if (debug.bytecode) {
    std::cout << "BYTECODE: Compiled " << chunk->getFunctionCount()
              << " functions:" << std::endl;
    for (const auto &func : chunk->getAllFunctions()) {
      std::cout << "  Function '" << func.name
                << "' (params: " << func.param_count
                << ", locals: " << func.local_count
                << ", instructions: " << func.instructions.size() << ")"
                << std::endl;
      for (size_t i = 0; i < func.instructions.size(); ++i) {
        const auto &inst = func.instructions[i];
        std::cout << "    " << i << ": " << static_cast<int>(inst.opcode);
        if (!inst.operands.empty()) {
          std::cout << " [";
          for (size_t j = 0; j < inst.operands.size(); ++j) {
            if (j > 0)
              std::cout << ", ";
            std::visit(
                [](auto &&arg) {
                  using T = std::decay_t<decltype(arg)>;
                  if constexpr (std::is_same_v<T, std::nullptr_t>)
                    std::cout << "null";
                  else if constexpr (std::is_same_v<T, bool>)
                    std::cout << (arg ? "true" : "false");
                  else if constexpr (std::is_same_v<T, int64_t>)
                    std::cout << arg;
                  else if constexpr (std::is_same_v<T, double>)
                    std::cout << arg;
                  else if constexpr (std::is_same_v<T, std::string>)
                    std::cout << "\"" << arg << "\"";
                  else if constexpr (std::is_same_v<T, uint32_t>)
                    std::cout << "const[" << arg << "]";
                },
                inst.operands[j]);
          }
          std::cout << "]";
        }
        std::cout << std::endl;
      }
    }
  }

  return std::move(chunk);
}

uint32_t HavelBytecodeCompiler::addConstant(const BytecodeValue &value) {
  current_function->constants.push_back(value);
  return current_function->constants.size() - 1;
}

uint32_t HavelBytecodeCompiler::getVariableIndex(const std::string &name) {
  auto it = variable_indices.find(name);
  if (it != variable_indices.end()) {
    return it->second;
  }
  uint32_t index = next_var_index++;
  variable_indices[name] = index;
  if (current_function) {
    current_function->local_count =
        std::max(current_function->local_count, next_var_index);
  }
  return index;
}

void HavelBytecodeCompiler::compileStatement(const ast::Statement &stmt) {
  if (auto expr_stmt = dynamic_cast<const ast::ExpressionStatement *>(&stmt)) {
    compileExpression(*expr_stmt->expression);
    emit(OpCode::POP); // Discard result
  } else if (auto let_decl = dynamic_cast<const ast::LetDeclaration *>(&stmt)) {
    compileExpression(*let_decl->value);
    uint32_t var_index = getVariableIndex(let_decl->pattern->toString());
    emit(OpCode::STORE_VAR, BytecodeValue{var_index});
  } else if (auto func_decl =
                 dynamic_cast<const ast::FunctionDeclaration *>(&stmt)) {
    compileFunction(*func_decl);
  } else if (auto if_stmt = dynamic_cast<const ast::IfStatement *>(&stmt)) {
    compileIfStatement(*if_stmt);
  } else if (auto while_stmt =
                 dynamic_cast<const ast::WhileStatement *>(&stmt)) {
    compileWhileStatement(*while_stmt);
  } else if (auto ret_stmt =
                 dynamic_cast<const ast::ReturnStatement *>(&stmt)) {
    if (ret_stmt->argument) {
      compileExpression(*ret_stmt->argument);
    } else {
      emit(OpCode::LOAD_CONST,
           BytecodeValue{addConstant(BytecodeValue{nullptr})});
    }
    emit(OpCode::RETURN);
  }
}

void HavelBytecodeCompiler::compileExpression(const ast::Expression &expr) {
  if (auto number_literal = dynamic_cast<const ast::NumberLiteral *>(&expr)) {
    emit(OpCode::LOAD_CONST,
         {BytecodeValue{std::in_place_index<3>, number_literal->value}});
  } else if (auto string_literal =
                 dynamic_cast<const ast::StringLiteral *>(&expr)) {
    emit(OpCode::LOAD_CONST,
         {BytecodeValue{std::in_place_index<4>, string_literal->value}});
  } else if (auto bool_literal = dynamic_cast<const ast::Identifier *>(&expr)) {
    if (bool_literal->symbol == "true") {
      emit(OpCode::LOAD_CONST, {BytecodeValue{std::in_place_index<1>, true}});
    } else if (bool_literal->symbol == "false") {
      emit(OpCode::LOAD_CONST, {BytecodeValue{std::in_place_index<1>, false}});
    } else {
      // Handle as regular identifier
      emit(OpCode::LOAD_VAR,
           {BytecodeValue{std::in_place_index<5>,
                          getVariableIndex(bool_literal->symbol)}});
    }
  } else if (auto identifier = dynamic_cast<const ast::Identifier *>(&expr)) {
    emit(OpCode::LOAD_VAR,
         {BytecodeValue{std::in_place_index<5>,
                        getVariableIndex(identifier->symbol)}});
  } else if (auto binary = dynamic_cast<const ast::BinaryExpression *>(&expr)) {
    compileBinaryExpression(*binary);
  } else if (auto call = dynamic_cast<const ast::CallExpression *>(&expr)) {
    compileCallExpression(*call);
  } else if (auto array_lit = dynamic_cast<const ast::ArrayLiteral *>(&expr)) {
    compileArrayLiteral(*array_lit);
  } else if (auto obj_lit = dynamic_cast<const ast::ObjectLiteral *>(&expr)) {
    compileObjectLiteral(*obj_lit);
  }
}

void HavelBytecodeCompiler::compileBinaryExpression(
    const ast::BinaryExpression &expr) {
  compileExpression(*expr.left);
  compileExpression(*expr.right);

  switch (expr.operator_) {
  case ast::BinaryOperator::Add:
    emit(OpCode::ADD);
    break;
  case ast::BinaryOperator::Sub:
    emit(OpCode::SUB);
    break;
  case ast::BinaryOperator::Mul:
    emit(OpCode::MUL);
    break;
  case ast::BinaryOperator::Div:
    emit(OpCode::DIV);
    break;
  case ast::BinaryOperator::Mod:
    emit(OpCode::MOD);
    break;
  case ast::BinaryOperator::Pow:
    // TODO: implement power operation
    break;
  case ast::BinaryOperator::Equal:
    emit(OpCode::EQ);
    break;
  case ast::BinaryOperator::NotEqual:
    emit(OpCode::NEQ);
    break;
  case ast::BinaryOperator::Less:
    emit(OpCode::LT);
    break;
  case ast::BinaryOperator::LessEqual:
    emit(OpCode::LTE);
    break;
  case ast::BinaryOperator::Greater:
    emit(OpCode::GT);
    break;
  case ast::BinaryOperator::GreaterEqual:
    emit(OpCode::GTE);
    break;
  case ast::BinaryOperator::And:
    emit(OpCode::AND);
    break;
  case ast::BinaryOperator::Or:
    emit(OpCode::OR);
    break;
  default:
    // TODO: handle other operators
    break;
  }
}

void HavelBytecodeCompiler::compileCallExpression(
    const ast::CallExpression &expr) {
  // Compile arguments
  for (const auto &arg : expr.args) {
    compileExpression(*arg);
  }

  // Compile callee: current VM expects a function name string on the stack
  if (auto callee_id =
          dynamic_cast<const ast::Identifier *>(expr.callee.get())) {
    emit(OpCode::LOAD_CONST,
         {BytecodeValue{std::in_place_index<5>,
                        addConstant(BytecodeValue{std::in_place_index<4>,
                                                  callee_id->symbol})}});
  } else {
    throw std::runtime_error(
        "BytecodeCompiler: CALL only supports identifier callees for now");
  }

  emit(OpCode::CALL, {BytecodeValue{std::in_place_index<5>,
                                    static_cast<uint32_t>(expr.args.size())}});
}

void HavelBytecodeCompiler::compileArrayLiteral(const ast::ArrayLiteral &expr) {
  for (const auto &element : expr.elements) {
    compileExpression(*element);
  }
  emit(OpCode::ARRAY_NEW,
       {BytecodeValue{std::in_place_index<5>,
                      static_cast<uint32_t>(expr.elements.size())}});
}

void HavelBytecodeCompiler::compileObjectLiteral(
    const ast::ObjectLiteral &expr) {
  for (const auto &[key, value] : expr.pairs) {
    compileExpression(*value);
    emit(OpCode::LOAD_CONST,
         {BytecodeValue{
             std::in_place_index<5>,
             addConstant(BytecodeValue{std::in_place_index<4>, key})}});
  }
  emit(OpCode::OBJECT_NEW,
       {BytecodeValue{std::in_place_index<5>,
                      static_cast<uint32_t>(expr.pairs.size())}});
}

void HavelBytecodeCompiler::compileFunction(
    const ast::FunctionDeclaration &func) {
  // Save current function state
  std::unique_ptr<BytecodeFunction> saved_function =
      std::move(current_function);
  auto saved_variables = variable_indices;
  uint32_t saved_next_var = next_var_index;

  // Create new function
  current_function = std::make_unique<BytecodeFunction>(
      func.name->symbol, func.parameters.size(), 0);
  variable_indices.clear();
  next_var_index = 0;

  // Add parameters as variables
  for (uint32_t i = 0; i < func.parameters.size(); i++) {
    variable_indices[func.parameters[i]->symbol] = i;
  }
  current_function->local_count =
      std::max(current_function->local_count,
               static_cast<uint32_t>(func.parameters.size()));

  // Compile function body
  for (const auto &stmt : func.body->body) {
    compileStatement(*stmt);
  }

  // Add return if none exists
  emit(OpCode::LOAD_CONST,
       {BytecodeValue{addConstant(BytecodeValue{std::in_place_index<0>})}});
  emit(OpCode::RETURN);

  chunk->addFunction(*current_function);

  // Restore state
  current_function = std::move(saved_function);
  variable_indices = saved_variables;
  next_var_index = saved_next_var;
}

void HavelBytecodeCompiler::compileIfStatement(const ast::IfStatement &stmt) {
  compileExpression(*stmt.condition);

  // Placeholder for jump if false
  size_t jump_false_pos = current_function->instructions.size();
  emit(OpCode::JUMP_IF_FALSE, {BytecodeValue{static_cast<uint32_t>(0)}});

  // Compile then branch
  if (auto then_block =
          dynamic_cast<const ast::BlockStatement *>(stmt.consequence.get())) {
    for (const auto &then_stmt : then_block->body) {
      compileStatement(*then_stmt);
    }
  } else {
    compileStatement(*stmt.consequence);
  }

  // Placeholder for jump over else
  size_t jump_else_pos = current_function->instructions.size();
  emit(OpCode::JUMP, {BytecodeValue{static_cast<uint32_t>(0)}});

  // Fix jump if false
  current_function->instructions[jump_false_pos].operands[0] = BytecodeValue{
      static_cast<uint32_t>(current_function->instructions.size())};

  // Compile else branch if exists
  if (stmt.alternative) {
    if (auto else_block =
            dynamic_cast<const ast::BlockStatement *>(stmt.alternative.get())) {
      for (const auto &else_stmt : else_block->body) {
        compileStatement(*else_stmt);
      }
    } else {
      compileStatement(*stmt.alternative);
    }
  }

  // Fix jump over else
  current_function->instructions[jump_else_pos].operands[0] = BytecodeValue{
      static_cast<uint32_t>(current_function->instructions.size())};
}

void HavelBytecodeCompiler::compileWhileStatement(
    const ast::WhileStatement &stmt) {
  size_t loop_start = current_function->instructions.size();

  compileExpression(*stmt.condition);

  size_t jump_false_pos = current_function->instructions.size();
  emit(OpCode::JUMP_IF_FALSE, {BytecodeValue{static_cast<uint32_t>(0)}});

  if (auto body_block =
          dynamic_cast<const ast::BlockStatement *>(stmt.body.get())) {
    for (const auto &body_stmt : body_block->body) {
      compileStatement(*body_stmt);
    }
  } else {
    compileStatement(*stmt.body);
  }

  emit(OpCode::JUMP, {BytecodeValue{static_cast<uint32_t>(loop_start)}});

  // Fix jump if false
  current_function->instructions[jump_false_pos].operands[0] = BytecodeValue{
      static_cast<uint32_t>(current_function->instructions.size())};
}

// Factory function
std::unique_ptr<BytecodeCompiler> createBytecodeCompiler() {
  return std::make_unique<HavelBytecodeCompiler>();
}

} // namespace havel::compiler
