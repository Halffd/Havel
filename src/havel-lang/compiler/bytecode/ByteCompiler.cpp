#include "ByteCompiler.hpp"

#include <stdexcept>

namespace havel::compiler {

namespace {
bool isHostBuiltin(const std::string &name) {
  return name == "print" || name == "sleep_ms" || name == "clock_ms";
}

OpCode toBytecodeOperator(ast::BinaryOperator op) {
  switch (op) {
  case ast::BinaryOperator::Add:
    return OpCode::ADD;
  case ast::BinaryOperator::Sub:
    return OpCode::SUB;
  case ast::BinaryOperator::Mul:
    return OpCode::MUL;
  case ast::BinaryOperator::Div:
    return OpCode::DIV;
  case ast::BinaryOperator::Mod:
    return OpCode::MOD;
  case ast::BinaryOperator::Pow:
    return OpCode::POW;
  case ast::BinaryOperator::Equal:
    return OpCode::EQ;
  case ast::BinaryOperator::NotEqual:
    return OpCode::NEQ;
  case ast::BinaryOperator::Less:
    return OpCode::LT;
  case ast::BinaryOperator::LessEqual:
    return OpCode::LTE;
  case ast::BinaryOperator::Greater:
    return OpCode::GT;
  case ast::BinaryOperator::GreaterEqual:
    return OpCode::GTE;
  case ast::BinaryOperator::And:
    return OpCode::AND;
  case ast::BinaryOperator::Or:
    return OpCode::OR;
  default:
    throw std::runtime_error("Unsupported binary operator in bytecode compiler");
  }
}

bool isIntegerLiteral(double value) {
  return static_cast<double>(static_cast<int64_t>(value)) == value;
}

} // namespace

std::unique_ptr<BytecodeChunk>
ByteCompiler::compile(const ast::Program &program) {
  chunk = std::make_unique<BytecodeChunk>();
  compiled_functions.clear();
  function_indices_by_node_.clear();
  top_level_function_indices_by_name_.clear();

  LexicalResolver resolver;
  lexical_resolution_ = resolver.resolve(program);
  if (!resolver.errors().empty()) {
    throw std::runtime_error("Lexical resolution failed: " +
                             resolver.errors().front());
  }

  for (const auto &[function, captures] : lexical_resolution_.captured_variables) {
    if (!function || captures.empty()) {
      continue;
    }

    throw std::runtime_error(
        "Phase 2 boundary: closure capture codegen is not enabled yet for '" +
        (function->name ? function->name->symbol : std::string("<anonymous>")) +
        "' (captures: " + captures.front() + ")");
  }

  // Reserve function indices so forward references and recursion emit stable
  // function objects.
  std::vector<const ast::FunctionDeclaration *> declared_functions;
  declared_functions.reserve(program.body.size());

  uint32_t next_function_index = 0;
  for (const auto &statement : program.body) {
    if (!statement ||
        statement->kind != ast::NodeType::FunctionDeclaration) {
      continue;
    }
    const auto &decl =
        static_cast<const ast::FunctionDeclaration &>(*statement);
    if (!decl.name) {
      throw std::runtime_error("Function declaration missing name");
    }
    top_level_function_indices_by_name_[decl.name->symbol] = next_function_index;
    function_indices_by_node_[&decl] = next_function_index++;
  }

  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }
    collectFunctionDeclarations(*statement, declared_functions);
  }

  for (const auto *decl : declared_functions) {
    if (!decl) {
      continue;
    }
    if (function_indices_by_node_.find(decl) != function_indices_by_node_.end()) {
      continue;
    }
    function_indices_by_node_[decl] = next_function_index++;
  }

  // Compile all declared functions (top-level + nested) before __main__.
  for (const auto *decl : declared_functions) {
    if (!decl) {
      continue;
    }
    compileFunction(*decl);
  }

  enterFunction(BytecodeFunction("__main__", 0, 0));

  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }

    if (statement->kind == ast::NodeType::FunctionDeclaration) {
      continue;
    }

    compileStatement(*statement);
  }

  emit(OpCode::LOAD_CONST, addConstant(nullptr));
  emit(OpCode::RETURN);
  leaveFunction();

  for (auto &function : compiled_functions) {
    chunk->addFunction(std::move(*function));
  }

  return std::move(chunk);
}

void ByteCompiler::emit(OpCode op) {
  emit(op, std::vector<BytecodeValue>{});
}

void ByteCompiler::emit(OpCode op, BytecodeValue operand) {
  emit(op, std::vector<BytecodeValue>{std::move(operand)});
}

void ByteCompiler::emit(OpCode op, std::vector<BytecodeValue> operands) {
  if (!current_function) {
    throw std::runtime_error("Attempted to emit bytecode without active function");
  }
  current_function->instructions.emplace_back(op, std::move(operands));
}

uint32_t ByteCompiler::addConstant(const BytecodeValue &value) {
  if (!current_function) {
    throw std::runtime_error("Attempted to add constant without active function");
  }

  current_function->constants.push_back(value);
  return static_cast<uint32_t>(current_function->constants.size() - 1);
}

uint32_t ByteCompiler::emitJump(OpCode op) {
  if (op != OpCode::JUMP && op != OpCode::JUMP_IF_FALSE &&
      op != OpCode::JUMP_IF_TRUE) {
    throw std::runtime_error("Invalid jump opcode");
  }

  if (!current_function) {
    throw std::runtime_error("Attempted to emit jump without active function");
  }

  uint32_t index = static_cast<uint32_t>(current_function->instructions.size());
  emit(op, static_cast<uint32_t>(0));
  return index;
}

void ByteCompiler::patchJump(uint32_t jump_instruction_index,
                                      uint32_t target) {
  if (!current_function) {
    throw std::runtime_error("Attempted to patch jump without active function");
  }

  if (jump_instruction_index >= current_function->instructions.size()) {
    throw std::runtime_error("Invalid jump patch index");
  }

  auto &instruction = current_function->instructions[jump_instruction_index];
  if (instruction.operands.empty()) {
    throw std::runtime_error("Jump instruction missing operand");
  }

  instruction.operands[0] = target;
}

void ByteCompiler::compileFunction(const ast::FunctionDeclaration &function) {
  if (!function.name) {
    throw std::runtime_error("Function declaration missing name");
  }

  enterFunction(BytecodeFunction(function.name->symbol,
                                 static_cast<uint32_t>(function.parameters.size()),
                                 0));

  for (const auto &param : function.parameters) {
    if (!param || !param->paramName) {
      throw std::runtime_error("Function parameter missing identifier");
    }
    reserveLocalSlot(declarationSlot(*param->paramName));
  }

  if (function.body) {
    compileBlockStatement(*function.body);
  }

  emit(OpCode::LOAD_CONST, addConstant(nullptr));
  emit(OpCode::RETURN);
  leaveFunction();
}

void ByteCompiler::compileStatement(const ast::Statement &statement) {
  switch (statement.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &expr_stmt =
        static_cast<const ast::ExpressionStatement &>(statement);
    if (expr_stmt.expression) {
      compileExpression(*expr_stmt.expression);
      emit(OpCode::POP);
    }
    break;
  }

  case ast::NodeType::LetDeclaration: {
    const auto &let = static_cast<const ast::LetDeclaration &>(statement);
    auto *identifier = dynamic_cast<const ast::Identifier *>(let.pattern.get());
    if (!identifier) {
      throw std::runtime_error(
          "Bytecode compiler only supports identifier patterns for let");
    }

    if (let.value) {
      compileExpression(*let.value);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(nullptr));
    }

    uint32_t slot = declarationSlot(*identifier);
    reserveLocalSlot(slot);
    emit(OpCode::STORE_VAR, slot);
    break;
  }

  case ast::NodeType::ReturnStatement: {
    const auto &ret = static_cast<const ast::ReturnStatement &>(statement);
    if (ret.argument) {
      compileExpression(*ret.argument);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(nullptr));
    }
    emit(OpCode::RETURN);
    break;
  }

  case ast::NodeType::IfStatement:
    compileIfStatement(static_cast<const ast::IfStatement &>(statement));
    break;

  case ast::NodeType::WhileStatement:
    compileWhileStatement(static_cast<const ast::WhileStatement &>(statement));
    break;

  case ast::NodeType::BlockStatement:
    compileBlockStatement(static_cast<const ast::BlockStatement &>(statement));
    break;

  case ast::NodeType::FunctionDeclaration:
    // Function declarations evaluate to function objects stored in local slots.
    // Top-level declarations are skipped in __main__ and do not hit this path.
    {
      const auto &function =
          static_cast<const ast::FunctionDeclaration &>(statement);
      if (!function.name) {
        throw std::runtime_error("Function declaration missing name");
      }

      auto index_it = function_indices_by_node_.find(&function);
      if (index_it == function_indices_by_node_.end()) {
        throw std::runtime_error("Missing function index for declaration: " +
                                 function.name->symbol);
      }

      uint32_t slot = declarationSlot(*function.name);
      reserveLocalSlot(slot);
      emit(OpCode::LOAD_CONST,
           addConstant(FunctionObject{.function_index = index_it->second}));
      emit(OpCode::STORE_VAR, slot);
    }
    break;

  default:
    throw std::runtime_error("Unsupported statement in bytecode compiler: " +
                             statement.toString());
  }
}

void ByteCompiler::compileExpression(const ast::Expression &expression) {
  switch (expression.kind) {
  case ast::NodeType::NumberLiteral: {
    const auto &num = static_cast<const ast::NumberLiteral &>(expression);
    if (isIntegerLiteral(num.value)) {
      emit(OpCode::LOAD_CONST,
           addConstant(static_cast<int64_t>(num.value)));
    } else {
      emit(OpCode::LOAD_CONST, addConstant(num.value));
    }
    break;
  }

  case ast::NodeType::StringLiteral: {
    const auto &str = static_cast<const ast::StringLiteral &>(expression);
    emit(OpCode::LOAD_CONST, addConstant(str.value));
    break;
  }

  case ast::NodeType::BooleanLiteral: {
    const auto &boolean = static_cast<const ast::BooleanLiteral &>(expression);
    emit(OpCode::LOAD_CONST, addConstant(boolean.value));
    break;
  }

  case ast::NodeType::Identifier: {
    const auto &id = static_cast<const ast::Identifier &>(expression);
    const auto *binding = bindingFor(id);
    if (!binding) {
      throw std::runtime_error("Missing lexical binding for identifier: " +
                               id.symbol);
    }

    switch (binding->kind) {
    case ResolvedBindingKind::Local:
      emit(OpCode::LOAD_VAR, binding->slot);
      break;
    case ResolvedBindingKind::Upvalue:
      // Resolver is now upvalue-aware; runtime support is still pending.
      emit(OpCode::LOAD_UPVALUE, binding->slot);
      break;
    case ResolvedBindingKind::GlobalFunction:
      {
        auto it = top_level_function_indices_by_name_.find(binding->name);
        if (it == top_level_function_indices_by_name_.end()) {
          throw std::runtime_error("Missing function index for: " +
                                   binding->name);
        }
        emit(OpCode::LOAD_CONST,
             addConstant(FunctionObject{.function_index = it->second}));
      }
      break;
    case ResolvedBindingKind::Builtin:
      emit(OpCode::LOAD_CONST, addConstant(binding->name));
      break;
    }
    break;
  }

  case ast::NodeType::BinaryExpression: {
    const auto &binary = static_cast<const ast::BinaryExpression &>(expression);
    if (!binary.left || !binary.right) {
      throw std::runtime_error("Malformed binary expression");
    }
    compileExpression(*binary.left);
    compileExpression(*binary.right);
    emit(toBytecodeOperator(binary.operator_));
    break;
  }

  case ast::NodeType::CallExpression:
    compileCallExpression(static_cast<const ast::CallExpression &>(expression));
    break;

  default:
    throw std::runtime_error("Unsupported expression in bytecode compiler: " +
                             expression.toString());
  }
}

void ByteCompiler::compileCallExpression(const ast::CallExpression &expression) {
  if (!expression.callee) {
    throw std::runtime_error("Call expression missing callee");
  }

  uint32_t arg_count = static_cast<uint32_t>(expression.args.size());

  if (expression.callee->kind == ast::NodeType::Identifier) {
    const auto &callee_id =
        static_cast<const ast::Identifier &>(*expression.callee);
    const auto *binding = bindingFor(callee_id);
    if (!binding) {
      throw std::runtime_error("Missing lexical binding for callee: " +
                               callee_id.symbol);
    }

    if (binding->kind == ResolvedBindingKind::Builtin) {
      for (const auto &arg : expression.args) {
        if (!arg) {
          throw std::runtime_error("Call expression contains null argument");
        }
        compileExpression(*arg);
      }
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{binding->name, arg_count});
      return;
    }
  }

  if (auto callee_name = getCalleeName(*expression.callee);
      callee_name && isHostBuiltin(*callee_name)) {
    for (const auto &arg : expression.args) {
      if (!arg) {
        throw std::runtime_error("Call expression contains null argument");
      }
      compileExpression(*arg);
    }
    emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{*callee_name, arg_count});
    return;
  }

  compileExpression(*expression.callee);
  for (const auto &arg : expression.args) {
    if (!arg) {
      throw std::runtime_error("Call expression contains null argument");
    }
    compileExpression(*arg);
  }
  emit(OpCode::CALL, arg_count);
}

void ByteCompiler::compileIfStatement(const ast::IfStatement &statement) {
  if (!statement.condition || !statement.consequence) {
    throw std::runtime_error("Malformed if statement");
  }

  compileExpression(*statement.condition);
  uint32_t else_jump = emitJump(OpCode::JUMP_IF_FALSE);

  compileStatement(*statement.consequence);

  if (statement.alternative) {
    uint32_t end_jump = emitJump(OpCode::JUMP);
    uint32_t else_target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(else_jump, else_target);

    compileStatement(*statement.alternative);
    uint32_t end_target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(end_jump, end_target);
  } else {
    uint32_t target = static_cast<uint32_t>(current_function->instructions.size());
    patchJump(else_jump, target);
  }
}

void ByteCompiler::compileWhileStatement(const ast::WhileStatement &statement) {
  if (!statement.condition || !statement.body) {
    throw std::runtime_error("Malformed while statement");
  }

  uint32_t loop_start = static_cast<uint32_t>(current_function->instructions.size());

  compileExpression(*statement.condition);
  uint32_t end_jump = emitJump(OpCode::JUMP_IF_FALSE);

  compileStatement(*statement.body);
  emit(OpCode::JUMP, loop_start);

  uint32_t loop_end = static_cast<uint32_t>(current_function->instructions.size());
  patchJump(end_jump, loop_end);
}

void ByteCompiler::compileBlockStatement(const ast::BlockStatement &block) {
  for (const auto &statement : block.body) {
    if (!statement) {
      continue;
    }
    compileStatement(*statement);
  }
}

void ByteCompiler::collectFunctionDeclarations(
    const ast::Statement &statement,
    std::vector<const ast::FunctionDeclaration *> &out) const {
  switch (statement.kind) {
  case ast::NodeType::FunctionDeclaration: {
    const auto &function =
        static_cast<const ast::FunctionDeclaration &>(statement);
    out.push_back(&function);
    if (function.body) {
      for (const auto &nested : function.body->body) {
        if (!nested) {
          continue;
        }
        collectFunctionDeclarations(*nested, out);
      }
    }
    break;
  }

  case ast::NodeType::BlockStatement: {
    const auto &block = static_cast<const ast::BlockStatement &>(statement);
    for (const auto &nested : block.body) {
      if (!nested) {
        continue;
      }
      collectFunctionDeclarations(*nested, out);
    }
    break;
  }

  case ast::NodeType::IfStatement: {
    const auto &if_statement = static_cast<const ast::IfStatement &>(statement);
    if (if_statement.consequence) {
      collectFunctionDeclarations(*if_statement.consequence, out);
    }
    if (if_statement.alternative) {
      collectFunctionDeclarations(*if_statement.alternative, out);
    }
    break;
  }

  case ast::NodeType::WhileStatement: {
    const auto &while_statement =
        static_cast<const ast::WhileStatement &>(statement);
    if (while_statement.body) {
      collectFunctionDeclarations(*while_statement.body, out);
    }
    break;
  }

  default:
    break;
  }
}

std::optional<std::string>
ByteCompiler::getCalleeName(const ast::Expression &callee) const {
  if (callee.kind == ast::NodeType::Identifier) {
    return static_cast<const ast::Identifier &>(callee).symbol;
  }

  if (callee.kind == ast::NodeType::MemberExpression) {
    const auto &member = static_cast<const ast::MemberExpression &>(callee);

    auto *object = dynamic_cast<const ast::Identifier *>(member.object.get());
    auto *property = dynamic_cast<const ast::Identifier *>(member.property.get());
    if (!object || !property) {
      return std::nullopt;
    }

    return object->symbol + "." + property->symbol;
  }

  return std::nullopt;
}

const ResolvedBinding *ByteCompiler::bindingFor(const ast::Identifier &id) const {
  auto it = lexical_resolution_.identifier_bindings.find(&id);
  if (it == lexical_resolution_.identifier_bindings.end()) {
    return nullptr;
  }
  return &it->second;
}

uint32_t ByteCompiler::declarationSlot(const ast::Identifier &id) const {
  auto it = lexical_resolution_.declaration_slots.find(&id);
  if (it == lexical_resolution_.declaration_slots.end()) {
    throw std::runtime_error("Missing declaration slot for: " + id.symbol);
  }
  return it->second;
}

void ByteCompiler::reserveLocalSlot(uint32_t slot) {
  if (slot >= next_local_index) {
    next_local_index = slot + 1;
  }
}

void ByteCompiler::enterFunction(BytecodeFunction &&function) {
  if (current_function) {
    throw std::runtime_error("Nested function compilation is not supported");
  }

  current_function = std::make_unique<BytecodeFunction>(std::move(function));
  resetLocals();
}

void ByteCompiler::leaveFunction() {
  if (!current_function) {
    throw std::runtime_error("No active function to close");
  }

  current_function->local_count = next_local_index;
  compiled_functions.push_back(std::move(current_function));
  resetLocals();
}

void ByteCompiler::resetLocals() {
  next_local_index = 0;
}

} // namespace havel::compiler
