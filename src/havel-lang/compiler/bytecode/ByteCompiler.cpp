#include "ByteCompiler.hpp"

#include <stdexcept>

namespace havel::compiler {

namespace {
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
    throw std::runtime_error(
        "Unsupported binary operator in bytecode compiler");
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
  lambda_indices_by_node_.clear();
  top_level_function_indices_by_name_.clear();
  top_level_struct_names_.clear();

  LexicalResolver resolver(host_builtin_names_, host_global_names_);
  lexical_resolution_ = resolver.resolve(program);
  if (!resolver.errors().empty()) {
    throw std::runtime_error("Lexical resolution failed: " +
                             resolver.errors().front());
  }

  // Reserve function indices so forward references and recursion emit stable
  // function objects.
  std::vector<const ast::FunctionDeclaration *> declared_functions;
  std::vector<const ast::LambdaExpression *> declared_lambdas;
  declared_functions.reserve(program.body.size());

  uint32_t next_function_index = 0;
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
      if (statement && statement->kind == ast::NodeType::StructDeclaration) {
        const auto &decl =
            static_cast<const ast::StructDeclaration &>(*statement);
        top_level_struct_names_.insert(decl.name);
      }
      continue;
    }
    const auto &decl =
        static_cast<const ast::FunctionDeclaration &>(*statement);
    if (!decl.name) {
      throw std::runtime_error("Function declaration missing name");
    }
    top_level_function_indices_by_name_[decl.name->symbol] =
        next_function_index;
    function_indices_by_node_[&decl] = next_function_index++;
  }

  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }
    collectFunctionDeclarations(*statement, declared_functions);
    collectLambdaExpressions(*statement, declared_lambdas);
  }

  for (const auto *decl : declared_functions) {
    if (!decl) {
      continue;
    }
    if (function_indices_by_node_.find(decl) !=
        function_indices_by_node_.end()) {
      continue;
    }
    function_indices_by_node_[decl] = next_function_index++;
  }

  for (const auto *lambda : declared_lambdas) {
    if (!lambda) {
      continue;
    }
    if (lambda_indices_by_node_.find(lambda) != lambda_indices_by_node_.end()) {
      continue;
    }
    lambda_indices_by_node_[lambda] = next_function_index++;
  }

  const uint32_t main_function_index = next_function_index++;
  compiled_functions.resize(main_function_index + 1);

  // Compile all declared functions (top-level + nested) before __main__.
  for (const auto *decl : declared_functions) {
    if (!decl) {
      continue;
    }
    compileFunction(*decl);
  }

  for (const auto *lambda : declared_lambdas) {
    if (!lambda) {
      continue;
    }
    compileLambda(*lambda);
  }

  enterFunction(BytecodeFunction("__main__", 0, 0), main_function_index);

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
    if (!function) {
      throw std::runtime_error("Missing compiled function for reserved slot");
    }
    chunk->addFunction(std::move(*function));
  }

  return std::move(chunk);
}

void ByteCompiler::emit(OpCode op) { emit(op, std::vector<BytecodeValue>{}); }

void ByteCompiler::emit(OpCode op, BytecodeValue operand) {
  emit(op, std::vector<BytecodeValue>{std::move(operand)});
}

void ByteCompiler::emit(OpCode op, std::vector<BytecodeValue> operands) {
  if (!current_function) {
    throw std::runtime_error(
        "Attempted to emit bytecode without active function");
  }
  current_function->instructions.emplace_back(op, std::move(operands));
  current_function->instruction_locations.push_back(
      current_source_location_.value_or(SourceLocation{}));
}

uint32_t ByteCompiler::addConstant(const BytecodeValue &value) {
  if (!current_function) {
    throw std::runtime_error(
        "Attempted to add constant without active function");
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

void ByteCompiler::patchJump(uint32_t jump_instruction_index, uint32_t target) {
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

  auto index_it = function_indices_by_node_.find(&function);
  if (index_it == function_indices_by_node_.end()) {
    throw std::runtime_error("Missing function index for declaration: " +
                             function.name->symbol);
  }

  enterFunction(
      BytecodeFunction(function.name->symbol,
                       static_cast<uint32_t>(function.parameters.size()), 0),
      index_it->second);
  auto upvalues_it = lexical_resolution_.function_upvalues.find(&function);
  if (upvalues_it != lexical_resolution_.function_upvalues.end()) {
    current_function->upvalues = upvalues_it->second;
  }

  for (const auto &param : function.parameters) {
    if (!param || !param->pattern) {
      throw std::runtime_error("Function parameter missing pattern");
    }
    collectParameterPatternSlots(*param->pattern);
  }

  if (function.body) {
    // Compile all statements except the last
    const auto &stmts = function.body->body;
    if (!stmts.empty()) {
      // Compile all but last statement normally (not in tail position)
      for (size_t i = 0; i < stmts.size() - 1; i++) {
        if (stmts[i]) {
          compileStatement(*stmts[i]);
        }
      }

      // Last statement: if it's an expression statement, return its value (Rust-like implicit return)
      const auto &lastStmt = stmts.back();
      if (lastStmt && lastStmt->kind == ast::NodeType::ExpressionStatement) {
        const auto &exprStmt = static_cast<const ast::ExpressionStatement &>(*lastStmt);
        if (exprStmt.expression) {
          // TCO: Enter tail position for the last expression
          enterTailPosition();
          clearTailCallFlag();
          compileExpression(*exprStmt.expression);
          exitTailPosition();
          // TCO: Only emit RETURN if we didn't emit TAIL_CALL
          if (!wasTailCall()) {
            emit(OpCode::RETURN);
          }
        } else {
          emit(OpCode::LOAD_CONST, addConstant(nullptr));
          emit(OpCode::RETURN);
        }
      } else if (lastStmt && lastStmt->kind == ast::NodeType::ReturnStatement) {
        // Last statement is an explicit return - compile with tail position
        enterTailPosition();
        compileStatement(*lastStmt);
        exitTailPosition();
      } else if (lastStmt) {
        // Last statement is not an expression or return - compile in tail position and add implicit return
        enterTailPosition();
        compileStatement(*lastStmt);
        exitTailPosition();
        // Only emit RETURN if the statement didn't already return (via tail call branches)
        if (!wasTailCall()) {
          emit(OpCode::RETURN);
        }
      } else {
        emit(OpCode::LOAD_CONST, addConstant(nullptr));
        emit(OpCode::RETURN);
      }
    } else {
      // Empty function body
      emit(OpCode::LOAD_CONST, addConstant(nullptr));
      emit(OpCode::RETURN);
    }
  } else {
    emit(OpCode::LOAD_CONST, addConstant(nullptr));
    emit(OpCode::RETURN);
  }
  leaveFunction();
}

void ByteCompiler::compileLambda(const ast::LambdaExpression &lambda) {
  auto index_it = lambda_indices_by_node_.find(&lambda);
  if (index_it == lambda_indices_by_node_.end()) {
    throw std::runtime_error("Missing function index for lambda expression");
  }

  enterFunction(BytecodeFunction("<lambda>",
                                 static_cast<uint32_t>(lambda.parameters.size()),
                                 0),
                index_it->second);

  auto upvalues_it = lexical_resolution_.lambda_upvalues.find(&lambda);
  if (upvalues_it != lexical_resolution_.lambda_upvalues.end()) {
    current_function->upvalues = upvalues_it->second;
  }

  // Compile parameter patterns - emit destructuring code for each parameter
  for (size_t i = 0; i < lambda.parameters.size(); i++) {
    const auto &param = lambda.parameters[i];
    if (!param || !param->pattern) {
      throw std::runtime_error("Lambda parameter missing pattern");
    }
    compileParameterPattern(*param->pattern, static_cast<uint32_t>(i));
  }

  if (lambda.body) {
    if (lambda.body->kind == ast::NodeType::ExpressionStatement) {
      const auto &expr_stmt =
          static_cast<const ast::ExpressionStatement &>(*lambda.body);
      if (expr_stmt.expression) {
        enterTailPosition();
        clearTailCallFlag();
        compileExpression(*expr_stmt.expression);
        exitTailPosition();
        if (!wasTailCall()) {
          emit(OpCode::RETURN);
        }
      } else {
        emit(OpCode::LOAD_CONST, addConstant(nullptr));
        emit(OpCode::RETURN);
      }
    } else if (lambda.body->kind == ast::NodeType::ReturnStatement) {
      enterTailPosition();
      compileStatement(*lambda.body);
      exitTailPosition();
    } else {
      enterTailPosition();
      clearTailCallFlag();
      compileStatement(*lambda.body);
      exitTailPosition();
      if (!wasTailCall()) {
        emit(OpCode::RETURN);
      }
    }
  } else {
    emit(OpCode::LOAD_CONST, addConstant(nullptr));
    emit(OpCode::RETURN);
  }

  leaveFunction();
}

// Compile a parameter pattern - extracts fields from parameter into locals
// paramIndex is the slot where the parameter value is stored
// For patterns, we allocate NEW slots for extracted values (not using declarationSlot)
void ByteCompiler::compileParameterPattern(const ast::Expression &pattern,
                                           uint32_t paramIndex) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    // Simple identifier parameter - reserve the slot for this parameter
    // The value will be placed here by CALL
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    reserveLocalSlot(declarationSlot(ident));
    break;
  }

  case ast::NodeType::ObjectPattern: {
    // Object destructuring: { x, y: alias }
    // First, reserve the slot for the parameter itself
    reserveLocalSlot(paramIndex);
    
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    for (const auto &prop : objPat.properties) {
      const std::string &key = prop.first;
      const auto &valuePattern = prop.second;

      // Emit: LOAD_VAR paramIndex, LOAD_CONST key, OBJECT_GET
      emit(OpCode::LOAD_VAR, paramIndex);
      emit(OpCode::LOAD_CONST, addConstant(key));
      emit(OpCode::OBJECT_GET);

      // Recursively compile the value pattern - result is on stack
      compileParameterPatternValue(*valuePattern);
    }
    break;
  }

  case ast::NodeType::ArrayPattern: {
    // Array destructuring: [a, b]
    // First, reserve the slot for the parameter itself
    reserveLocalSlot(paramIndex);
    
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    for (size_t i = 0; i < arrPat.elements.size(); i++) {
      const auto &elemPattern = arrPat.elements[i];

      // Emit: LOAD_VAR paramIndex, LOAD_CONST index, ARRAY_GET
      emit(OpCode::LOAD_VAR, paramIndex);
      emit(OpCode::LOAD_CONST, addConstant(static_cast<int64_t>(i)));
      emit(OpCode::ARRAY_GET);

      // Recursively compile the element pattern - result is on stack
      compileParameterPatternValue(*elemPattern);
    }
    break;
  }

  default:
    throw std::runtime_error("Unsupported parameter pattern type");
  }
}

// Compile a parameter pattern value - expects value on stack, stores into local
void ByteCompiler::compileParameterPatternValue(const ast::Expression &pattern) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    // Store value into the identifier's declared slot
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    emit(OpCode::STORE_VAR, declarationSlot(ident));
    break;
  }
  case ast::NodeType::ObjectPattern: {
    // Nested object pattern - value is on stack, extract fields
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    uint32_t tempSlot = next_local_index++;
    emit(OpCode::STORE_VAR, tempSlot);
    
    for (const auto &prop : objPat.properties) {
      const std::string &key = prop.first;
      const auto &valuePattern = prop.second;

      // Emit: LOAD_VAR tempSlot, LOAD_CONST key, OBJECT_GET
      emit(OpCode::LOAD_VAR, tempSlot);
      emit(OpCode::LOAD_CONST, addConstant(key));
      emit(OpCode::OBJECT_GET);

      compileParameterPatternValue(*valuePattern);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    // Nested array pattern - value is on stack, extract elements
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    uint32_t tempSlot = next_local_index++;
    emit(OpCode::STORE_VAR, tempSlot);
    
    for (size_t i = 0; i < arrPat.elements.size(); i++) {
      const auto &elemPattern = arrPat.elements[i];

      // Emit: LOAD_VAR tempSlot, LOAD_CONST index, ARRAY_GET
      emit(OpCode::LOAD_VAR, tempSlot);
      emit(OpCode::LOAD_CONST, addConstant(static_cast<int64_t>(i)));
      emit(OpCode::ARRAY_GET);

      compileParameterPatternValue(*elemPattern);
    }
    break;
  }
  default:
    throw std::runtime_error("Unsupported parameter pattern value type");
  }
}

// Collect all declaration slots from a parameter pattern (for function declarations)
void ByteCompiler::collectParameterPatternSlots(const ast::Expression &pattern) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    reserveLocalSlot(declarationSlot(ident));
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    for (const auto &prop : objPat.properties) {
      collectParameterPatternSlots(*prop.second);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    for (const auto &elem : arrPat.elements) {
      if (elem) {
        collectParameterPatternSlots(*elem);
      }
    }
    break;
  }
  default:
    break;
  }
}

void ByteCompiler::compileStatement(const ast::Statement &statement) {
  auto source_scope = atNode(statement);
  switch (statement.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &expr_stmt =
        static_cast<const ast::ExpressionStatement &>(statement);
    if (expr_stmt.expression) {
      compileExpression(*expr_stmt.expression);
      // TCO: Don't POP if in tail position (value is return value)
      if (!in_tail_position_) {
        emit(OpCode::POP);
      }
    }
    break;
  }

  case ast::NodeType::LetDeclaration: {
    const auto &let = static_cast<const ast::LetDeclaration &>(statement);
    if (auto *identifier =
            dynamic_cast<const ast::Identifier *>(let.pattern.get())) {
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

    if (let.pattern && let.pattern->kind == ast::NodeType::ListPattern) {
      const auto &pattern =
          static_cast<const ast::ArrayPattern &>(*let.pattern);
      if (!let.value) {
        throw std::runtime_error("Tuple/array destructuring requires a value");
      }

      bool optimized_tuple_literal = let.value->kind == ast::NodeType::TupleExpression;
      const ast::TupleExpression *tuple_value = optimized_tuple_literal
          ? static_cast<const ast::TupleExpression *>(let.value.get())
          : nullptr;

      uint32_t temp_slot = next_local_index;
      if (!optimized_tuple_literal) {
        reserveLocalSlot(temp_slot);
        compileExpression(*let.value);
        emit(OpCode::STORE_VAR, temp_slot);
      }

      for (size_t i = 0; i < pattern.elements.size(); ++i) {
        const auto *element_id =
            pattern.elements[i]
                ? dynamic_cast<const ast::Identifier *>(pattern.elements[i].get())
                : nullptr;
        if (!element_id) {
          throw std::runtime_error(
              "Tuple destructuring currently supports identifier elements only");
        }
        const uint32_t slot = declarationSlot(*element_id);
        reserveLocalSlot(slot);
        if (optimized_tuple_literal) {
          if (tuple_value && i < tuple_value->elements.size() &&
              tuple_value->elements[i]) {
            compileExpression(*tuple_value->elements[i]);
          } else {
            emit(OpCode::LOAD_CONST, addConstant(nullptr));
          }
        } else {
          emit(OpCode::LOAD_VAR, temp_slot);
          emit(OpCode::LOAD_CONST, addConstant(static_cast<int64_t>(i)));
          emit(OpCode::ARRAY_GET);
        }
        emit(OpCode::STORE_VAR, slot);
      }
      break;
    }

    throw std::runtime_error(
        "Bytecode compiler supports let patterns: identifier and tuple/array");
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

  case ast::NodeType::ForStatement:
    compileForStatement(static_cast<const ast::ForStatement &>(statement));
    break;

  case ast::NodeType::BlockStatement:
    compileBlockStatement(static_cast<const ast::BlockStatement &>(statement));
    break;

  case ast::NodeType::HotkeyBinding:
    compileHotkeyBinding(static_cast<const ast::HotkeyBinding &>(statement));
    break;

  case ast::NodeType::InputStatement:
    compileInputStatement(static_cast<const ast::InputStatement &>(statement));
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
      auto upvalues_it = lexical_resolution_.function_upvalues.find(&function);
      if (upvalues_it != lexical_resolution_.function_upvalues.end() &&
          !upvalues_it->second.empty()) {
        emit(OpCode::CLOSURE, index_it->second);
      } else {
        emit(OpCode::LOAD_CONST,
             addConstant(FunctionObject{.function_index = index_it->second}));
      }
      emit(OpCode::STORE_VAR, slot);
    }
    break;

  case ast::NodeType::TryExpression:
    compileTryStatement(static_cast<const ast::TryExpression &>(statement));
    break;

  case ast::NodeType::ThrowStatement: {
    const auto &throw_stmt = static_cast<const ast::ThrowStatement &>(statement);
    if (throw_stmt.value) {
      compileExpression(*throw_stmt.value);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(nullptr));
    }
    emit(OpCode::THROW);
    break;
  }

  // Type system declarations - register types at compile time
  case ast::NodeType::StructDeclaration: {
    const auto &structDecl = static_cast<const ast::StructDeclaration &>(statement);
    // Runtime registration: struct.define("Name", ["field1", ...])
    emit(OpCode::LOAD_CONST, addConstant(structDecl.name));
    emit(OpCode::ARRAY_NEW);
    for (const auto &field : structDecl.definition.fields) {
      emit(OpCode::LOAD_CONST, addConstant(field.name));
      emit(OpCode::ARRAY_PUSH);
    }
    emit(OpCode::CALL_HOST,
         std::vector<BytecodeValue>{"struct.define", static_cast<uint32_t>(2)});
    emit(OpCode::POP);
    break;
  }

  case ast::NodeType::EnumDeclaration: {
    const auto &enumDecl = static_cast<const ast::EnumDeclaration &>(statement);
    // Register enum type with its variants
    std::vector<std::string> variantNames;
    for (const auto& variant : enumDecl.definition.variants) {
      variantNames.push_back(variant.name);
    }
    break;
  }

  case ast::NodeType::TraitDeclaration: {
    const auto &traitDecl = static_cast<const ast::TraitDeclaration &>(statement);
    break;
  }

  case ast::NodeType::ImplDeclaration: {
    const auto &implDecl = static_cast<const ast::ImplDeclaration &>(statement);
    break;
  }

  default:
    throw std::runtime_error("Unsupported statement in bytecode compiler: " +
                             statement.toString());
  }
}

void ByteCompiler::compileTryStatement(const ast::TryExpression &statement) {
  if (!statement.tryBody) {
    throw std::runtime_error("try statement missing body");
  }

  const uint32_t try_enter_index =
      static_cast<uint32_t>(current_function->instructions.size());
  // Emit TRY_ENTER with placeholder operands (catch_ip and finally_ip patched later)
  emit(OpCode::TRY_ENTER, std::vector<BytecodeValue>{
      static_cast<uint32_t>(0),  // catch_ip - patched later
      static_cast<uint32_t>(0)   // finally_ip - patched later (0 if no finally)
  });

  compileStatement(*statement.tryBody);
  emit(OpCode::TRY_EXIT);

  // Finally block location (executed after try body on normal exit)
  uint32_t finally_ip = 0;
  uint32_t finally_end_ip = 0;
  if (statement.finallyBlock) {
    finally_ip = static_cast<uint32_t>(current_function->instructions.size());
    compileStatement(*statement.finallyBlock);
    finally_end_ip = static_cast<uint32_t>(current_function->instructions.size());
    // After finally on normal exit, jump to end
  }

  const uint32_t jump_after_try = emitJump(OpCode::JUMP);

  // Catch block location
  const uint32_t catch_ip =
      static_cast<uint32_t>(current_function->instructions.size());
  
  // Patch TRY_ENTER with catch_ip and finally_ip
  if (try_enter_index >= current_function->instructions.size()) {
    throw std::runtime_error("Invalid TRY_ENTER patch index");
  }
  current_function->instructions[try_enter_index].operands[0] = catch_ip;
  if (finally_ip != 0) {
    current_function->instructions[try_enter_index].operands[1] = finally_ip;
  }

  std::vector<uint32_t> end_jumps;

  if (statement.catchBody) {
    if (statement.catchVariable) {
      const uint32_t catch_slot = declarationSlot(*statement.catchVariable);
      reserveLocalSlot(catch_slot);
      emit(OpCode::LOAD_EXCEPTION);
      emit(OpCode::STORE_VAR, catch_slot);
    }
    compileStatement(*statement.catchBody);
    // After catch, execute finally if it exists and wasn't already executed
    if (statement.finallyBlock) {
      compileStatement(*statement.finallyBlock);
    }
    end_jumps.push_back(emitJump(OpCode::JUMP));
  } else if (statement.finallyBlock) {
    // No catch block - save exception, run finally, re-throw
    const uint32_t exception_slot = next_local_index;
    reserveLocalSlot(exception_slot);
    emit(OpCode::LOAD_EXCEPTION);
    emit(OpCode::STORE_VAR, exception_slot);
    compileStatement(*statement.finallyBlock);
    emit(OpCode::LOAD_VAR, exception_slot);
    emit(OpCode::THROW);
  }

  const uint32_t end_ip =
      static_cast<uint32_t>(current_function->instructions.size());
  patchJump(jump_after_try, end_ip);
  for (const uint32_t jump : end_jumps) {
    patchJump(jump, end_ip);
  }
}

void ByteCompiler::compileExpression(const ast::Expression &expression) {
  auto source_scope = atNode(expression);
  switch (expression.kind) {
  case ast::NodeType::NumberLiteral: {
    const auto &num = static_cast<const ast::NumberLiteral &>(expression);
    if (isIntegerLiteral(num.value)) {
      emit(OpCode::LOAD_CONST, addConstant(static_cast<int64_t>(num.value)));
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

  case ast::NodeType::InterpolatedStringExpression: {
    const auto &interp = static_cast<const ast::InterpolatedStringExpression &>(expression);

    // Build interpolated string by concatenating segments
    // Start with empty string
    emit(OpCode::LOAD_CONST, addConstant(std::string("")));

    for (const auto &segment : interp.segments) {
      if (segment.isString) {
        // Push string segment
        emit(OpCode::LOAD_CONST, addConstant(segment.stringValue));
        emit(OpCode::STRING_CONCAT);
      } else {
        // Evaluate pre-parsed expression and convert to string
        compileExpression(*segment.expression);
        emit(OpCode::TO_STRING);
        emit(OpCode::STRING_CONCAT);
      }
    }
    break;
  }

  case ast::NodeType::BooleanLiteral: {
    const auto &boolean = static_cast<const ast::BooleanLiteral &>(expression);
    emit(OpCode::LOAD_CONST, addConstant(boolean.value));
    break;
  }

  case ast::NodeType::ArrayLiteral: {
    const auto &array = static_cast<const ast::ArrayLiteral &>(expression);
    
    // Check if array contains spread expressions
    bool hasSpread = false;
    for (const auto &element : array.elements) {
      if (element && element->kind == ast::NodeType::SpreadExpression) {
        hasSpread = true;
        break;
      }
    }
    
    if (!hasSpread) {
      // Simple case: no spread, use standard compilation
      emit(OpCode::ARRAY_NEW);
      for (const auto &element : array.elements) {
        if (!element) {
          throw std::runtime_error("Array literal contains null element");
        }
        emit(OpCode::DUP);
        compileExpression(*element);
        emit(OpCode::ARRAY_PUSH);
        emit(OpCode::POP);  // Remove duplicate array from stack (ARRAY_PUSH pushes container back)
      }
    } else {
      // Complex case: has spread, build array element by element
      emit(OpCode::ARRAY_NEW);
      uint32_t resultSlot = next_local_index++;
      reserveLocalSlot(resultSlot);
      emit(OpCode::STORE_VAR, resultSlot);
      
      for (const auto &element : array.elements) {
        if (!element) {
          throw std::runtime_error("Array literal contains null element");
        }
        if (element->kind == ast::NodeType::SpreadExpression) {
          const auto &spread = static_cast<const ast::SpreadExpression &>(*element);
          if (!spread.target) {
            throw std::runtime_error("Spread expression missing target");
          }
          // Compile spread target
          compileExpression(*spread.target);
          uint32_t spreadArrSlot = next_local_index++;
          reserveLocalSlot(spreadArrSlot);
          emit(OpCode::STORE_VAR, spreadArrSlot);
          
          // Get length
          emit(OpCode::LOAD_VAR, spreadArrSlot);
          emit(OpCode::ARRAY_LEN);
          uint32_t lenSlot = next_local_index++;
          reserveLocalSlot(lenSlot);
          emit(OpCode::STORE_VAR, lenSlot);
          
          // Initialize index
          uint32_t idxSlot = next_local_index++;
          reserveLocalSlot(idxSlot);
          emit(OpCode::LOAD_CONST, addConstant(int64_t(0)));
          emit(OpCode::STORE_VAR, idxSlot);
          
          uint32_t loopStart = static_cast<uint32_t>(current_function->instructions.size());
          
          // Check if idx < len (continue loop if true)
          emit(OpCode::LOAD_VAR, idxSlot);
          emit(OpCode::LOAD_VAR, lenSlot);
          emit(OpCode::LT);
          uint32_t endJump = emitJump(OpCode::JUMP_IF_FALSE);
          
          // Get array element and push to result array
          emit(OpCode::LOAD_VAR, resultSlot);  // Load result array
          emit(OpCode::LOAD_VAR, spreadArrSlot);
          emit(OpCode::LOAD_VAR, idxSlot);
          emit(OpCode::ARRAY_GET);
          // Stack: result_array, element
          emit(OpCode::ARRAY_PUSH);
          // Stack: result_array
          emit(OpCode::STORE_VAR, resultSlot);  // Save result array back
          
          // Increment index
          emit(OpCode::LOAD_VAR, idxSlot);
          emit(OpCode::LOAD_CONST, addConstant(int64_t(1)));
          emit(OpCode::ADD);
          emit(OpCode::STORE_VAR, idxSlot);
          
          emit(OpCode::JUMP, loopStart);
          
          uint32_t loopEnd = static_cast<uint32_t>(current_function->instructions.size());
          patchJump(endJump, loopEnd);
        } else {
          emit(OpCode::LOAD_VAR, resultSlot);
          compileExpression(*element);
          emit(OpCode::ARRAY_PUSH);
          emit(OpCode::STORE_VAR, resultSlot);
        }
      }
      
      // Load result array for final use
      emit(OpCode::LOAD_VAR, resultSlot);
    }
    break;
  }
  case ast::NodeType::TupleExpression: {
    const auto &tuple = static_cast<const ast::TupleExpression &>(expression);
    emit(OpCode::ARRAY_NEW);
    for (const auto &element : tuple.elements) {
      if (!element) {
        emit(OpCode::LOAD_CONST, addConstant(nullptr));
      } else {
        compileExpression(*element);
      }
      emit(OpCode::ARRAY_PUSH);
    }
    break;
  }

  case ast::NodeType::SetExpression: {
    const auto &set = static_cast<const ast::SetExpression &>(expression);
    emit(OpCode::SET_NEW);
    for (const auto &element : set.elements) {
      if (!element) {
        throw std::runtime_error("Set literal contains null element");
      }
      emit(OpCode::DUP);
      compileExpression(*element);
      emit(OpCode::LOAD_CONST, addConstant(true));
      emit(OpCode::ARRAY_SET);
    }
    break;
  }

  case ast::NodeType::ObjectLiteral: {
    const auto &object = static_cast<const ast::ObjectLiteral &>(expression);
    emit(OpCode::OBJECT_NEW);
    for (const auto &pair : object.pairs) {
      if (!pair.second) {
        throw std::runtime_error("Object literal contains null value");
      }
      emit(OpCode::DUP);
      compileExpression(*pair.second);
      emit(OpCode::LOAD_CONST, addConstant(pair.first));
      emit(OpCode::OBJECT_SET);
    }
    break;
  }

  case ast::NodeType::MatchExpression: {
    const auto &match = static_cast<const ast::MatchExpression &>(expression);

    // Compile the value to match on
    if (!match.value) {
      throw std::runtime_error("Match expression missing value");
    }
    compileExpression(*match.value);

    // Store match value in temp variable
    uint32_t matchSlot = next_local_index++;
    reserveLocalSlot(matchSlot);
    emit(OpCode::STORE_VAR, matchSlot);

    std::vector<uint32_t> caseJumps;

    // Compile each case
    for (const auto &casePair : match.cases) {
      const auto &pattern = casePair.first;
      const auto &result = casePair.second;

      // For enum matching, use ENUM_MATCH opcode
      // For now, use simple equality comparison
      emit(OpCode::LOAD_VAR, matchSlot);
      compileExpression(*pattern);
      emit(OpCode::EQ);

      // Jump to result if not equal
      caseJumps.push_back(emitJump(OpCode::JUMP_IF_FALSE));

      // Compile result expression - TCO: inherit tail position
      compileExpression(*result);

      // Jump to end
      caseJumps.push_back(emitJump(OpCode::JUMP));
    }

    // Compile default case - TCO: inherit tail position
    uint32_t defaultTarget = static_cast<uint32_t>(current_function->instructions.size());
    if (match.defaultCase) {
      compileExpression(*match.defaultCase);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(nullptr));
    }

    uint32_t endTarget = static_cast<uint32_t>(current_function->instructions.size());

    // Patch all jumps
    for (size_t i = 0; i < caseJumps.size(); i += 2) {
      // Patch equality check jump to next case or default
      patchJump(caseJumps[i], defaultTarget);
      // Patch result jump to end
      patchJump(caseJumps[i + 1], endTarget);
    }

    break;
  }

  case ast::NodeType::LambdaExpression: {
    const auto &lambda = static_cast<const ast::LambdaExpression &>(expression);
    auto it = lambda_indices_by_node_.find(&lambda);
    if (it == lambda_indices_by_node_.end()) {
      throw std::runtime_error("Missing function index for lambda expression");
    }
    auto upvalues_it = lexical_resolution_.lambda_upvalues.find(&lambda);
    if (upvalues_it != lexical_resolution_.lambda_upvalues.end() &&
        !upvalues_it->second.empty()) {
      emit(OpCode::CLOSURE, it->second);
    } else {
      emit(OpCode::LOAD_CONST,
           addConstant(FunctionObject{.function_index = it->second}));
    }
    break;
  }

  case ast::NodeType::Identifier: {
    const auto &id = static_cast<const ast::Identifier &>(expression);
    
    // Global scope access (::identifier)
    if (id.isGlobalScope) {
      emit(OpCode::LOAD_GLOBAL, id.symbol);
      break;
    }
    
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
    case ResolvedBindingKind::GlobalFunction: {
      auto it = top_level_function_indices_by_name_.find(binding->name);
      if (it == top_level_function_indices_by_name_.end()) {
        throw std::runtime_error("Missing function index for: " +
                                 binding->name);
      }
      emit(OpCode::LOAD_CONST,
           addConstant(FunctionObject{.function_index = it->second}));
    } break;
    case ResolvedBindingKind::HostGlobal:
      emit(OpCode::LOAD_GLOBAL, binding->name);
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
    
    // Special handling for 'in' and 'not in' - compile as host function call
    if (binary.operator_ == ast::BinaryOperator::In || 
        binary.operator_ == ast::BinaryOperator::NotIn) {
      compileExpression(*binary.left);   // value to check
      compileExpression(*binary.right);  // container
      std::string fnName = binary.operator_ == ast::BinaryOperator::In ? "any.in" : "any.not_in";
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{fnName, static_cast<uint32_t>(2)});
    } else {
      compileExpression(*binary.left);
      compileExpression(*binary.right);
      emit(toBytecodeOperator(binary.operator_));
    }
    break;
  }

  case ast::NodeType::RangeExpression: {
    const auto &range = static_cast<const ast::RangeExpression &>(expression);
    if (!range.start || !range.end) {
      throw std::runtime_error("Malformed range expression");
    }
    compileExpression(*range.start);
    compileExpression(*range.end);
    
    if (range.step) {
      compileExpression(*range.step);
      emit(OpCode::RANGE_STEP_NEW);
    } else {
      emit(OpCode::RANGE_NEW);
    }
    break;
  }

  case ast::NodeType::PipelineExpression: {
    const auto &pipeline =
        static_cast<const ast::PipelineExpression &>(expression);
    if (pipeline.stages.empty()) {
      throw std::runtime_error("Pipeline expression has no stages");
    }

    // Compile first stage
    compileExpression(*pipeline.stages[0]);

    // For each subsequent stage, compile it as a function call with previous
    // result as argument
    for (size_t i = 1; i < pipeline.stages.size(); ++i) {
      auto &stage = pipeline.stages[i];
      if (!stage) {
        throw std::runtime_error("Pipeline stage is null");
      }

      // The stage should be a call expression or identifier that can be called
      // Result from previous stage is already on stack as first argument
      if (stage->kind == ast::NodeType::CallExpression) {
        const auto &call = static_cast<const ast::CallExpression &>(*stage);
        // Compile the callee (function to call)
        if (call.callee) {
          compileExpression(*call.callee);
        }
        // Add arguments from the call (previous result is already on stack)
        for (const auto &arg : call.args) {
          if (arg)
            compileExpression(*arg);
        }
        // Call with (previous_result + explicit_args) count
        emit(OpCode::CALL, static_cast<uint32_t>(1 + call.args.size()));
      } else {
        // Stage is just an identifier or member expression - call it with
        // previous result
        compileExpression(*stage);
        emit(OpCode::CALL, 1);
      }
    }
    break;
  }

  case ast::NodeType::AssignmentExpression: {
    const auto &assignment =
        static_cast<const ast::AssignmentExpression &>(expression);
    if (!assignment.value) {
      throw std::runtime_error("Assignment expression missing value");
    }

    const auto *target_id =
        assignment.target
            ? dynamic_cast<const ast::Identifier *>(assignment.target.get())
            : nullptr;
    const auto *target_member =
        assignment.target ? dynamic_cast<const ast::MemberExpression *>(
                                assignment.target.get())
                          : nullptr;
    const auto *target_index = assignment.target
                                   ? dynamic_cast<const ast::IndexExpression *>(
                                         assignment.target.get())
                                   : nullptr;

    auto emitStoreIdentifierWithResult = [&](const ResolvedBinding &binding) {
      if (binding.is_const) {
        throw std::runtime_error("Cannot assign to const binding: " +
                                 binding.name);
      }
      emit(OpCode::DUP);
      if (binding.kind == ResolvedBindingKind::Local) {
        emit(OpCode::STORE_VAR, binding.slot);
      } else if (binding.kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::STORE_UPVALUE, binding.slot);
      } else {
        throw std::runtime_error("Assignment target is not mutable");
      }
    };

    auto emitLoadIdentifier = [&](const ResolvedBinding &binding) {
      if (binding.is_const && assignment.operator_ != "=") {
        throw std::runtime_error("Cannot mutate const binding: " +
                                 binding.name);
      }
      if (binding.kind == ResolvedBindingKind::Local) {
        emit(OpCode::LOAD_VAR, binding.slot);
      } else if (binding.kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::LOAD_UPVALUE, binding.slot);
      } else {
        throw std::runtime_error("Assignment target is not mutable");
      }
    };

    auto emitStoreMemberWithResult = [&](const ast::MemberExpression &member) {
      auto *property =
          dynamic_cast<const ast::Identifier *>(member.property.get());
      if (!member.object || !property) {
        throw std::runtime_error(
            "Member assignment expects identifier property target");
      }
      uint32_t temp_slot = next_local_index;
      reserveLocalSlot(temp_slot);
      emit(OpCode::STORE_VAR, temp_slot);
      compileExpression(*member.object);
      emit(OpCode::LOAD_VAR, temp_slot);
      emit(OpCode::OBJECT_SET, property->symbol);
      emit(OpCode::LOAD_VAR, temp_slot);
    };

    auto emitStoreIndexWithResult =
        [&](const ast::IndexExpression &index_expr) {
          if (!index_expr.object || !index_expr.index) {
            throw std::runtime_error(
                "Index assignment expects object and index");
          }
          uint32_t temp_slot = next_local_index;
          reserveLocalSlot(temp_slot);
          emit(OpCode::STORE_VAR, temp_slot);
          compileExpression(*index_expr.object);
          compileExpression(*index_expr.index);
          emit(OpCode::LOAD_VAR, temp_slot);
          emit(OpCode::ARRAY_SET);
          emit(OpCode::LOAD_VAR, temp_slot);
        };

    if (assignment.operator_ == "=") {
      compileExpression(*assignment.value);
      if (target_id) {
        // Check for global scope assignment (::x = value)
        if (assignment.isGlobalScope) {
          emit(OpCode::DUP);
          emit(OpCode::STORE_GLOBAL, target_id->symbol);
          break;
        }
        
        const auto *binding = bindingFor(*target_id);
        if (!binding) {
          throw std::runtime_error(
              "Missing lexical binding for assignment target: " +
              target_id->symbol);
        }
        emitStoreIdentifierWithResult(*binding);
        break;
      }
      if (target_member) {
        emitStoreMemberWithResult(*target_member);
        break;
      }
      if (target_index) {
        emitStoreIndexWithResult(*target_index);
        break;
      }
      throw std::runtime_error("Unsupported assignment target");
    }

    auto emitCompound = [&](OpCode math_op) {
      if (target_id) {
        const auto *binding = bindingFor(*target_id);
        if (!binding) {
          throw std::runtime_error(
              "Missing lexical binding for assignment target: " +
              target_id->symbol);
        }
        emitLoadIdentifier(*binding);
        compileExpression(*assignment.value);
        emit(math_op);
        emitStoreIdentifierWithResult(*binding);
        return;
      }
      if (target_member) {
        auto *property = dynamic_cast<const ast::Identifier *>(
            target_member->property.get());
        if (!target_member->object || !property) {
          throw std::runtime_error(
              "Member assignment expects identifier property target");
        }
        uint32_t temp_object = next_local_index;
        reserveLocalSlot(temp_object);
        compileExpression(*target_member->object);
        emit(OpCode::STORE_VAR, temp_object);
        emit(OpCode::LOAD_VAR, temp_object);
        emit(OpCode::LOAD_CONST, addConstant(property->symbol));
        emit(OpCode::OBJECT_GET);
        compileExpression(*assignment.value);
        emit(math_op);
        uint32_t temp_result = next_local_index;
        reserveLocalSlot(temp_result);
        emit(OpCode::DUP);
        emit(OpCode::STORE_VAR, temp_result);
        emit(OpCode::LOAD_VAR, temp_object);
        emit(OpCode::LOAD_VAR, temp_result);
        emit(OpCode::OBJECT_SET, property->symbol);
        emit(OpCode::LOAD_VAR, temp_result);
        return;
      }
      if (target_index) {
        if (!target_index->object || !target_index->index) {
          throw std::runtime_error("Index assignment expects object and index");
        }
        uint32_t temp_object = next_local_index;
        reserveLocalSlot(temp_object);
        uint32_t temp_index = next_local_index;
        reserveLocalSlot(temp_index);
        compileExpression(*target_index->object);
        emit(OpCode::STORE_VAR, temp_object);
        compileExpression(*target_index->index);
        emit(OpCode::STORE_VAR, temp_index);
        emit(OpCode::LOAD_VAR, temp_object);
        emit(OpCode::LOAD_VAR, temp_index);
        emit(OpCode::ARRAY_GET);
        compileExpression(*assignment.value);
        emit(math_op);
        uint32_t temp_result = next_local_index;
        reserveLocalSlot(temp_result);
        emit(OpCode::DUP);
        emit(OpCode::STORE_VAR, temp_result);
        emit(OpCode::LOAD_VAR, temp_object);
        emit(OpCode::LOAD_VAR, temp_index);
        emit(OpCode::LOAD_VAR, temp_result);
        emit(OpCode::ARRAY_SET);
        emit(OpCode::LOAD_VAR, temp_result);
        return;
      }
      throw std::runtime_error("Unsupported compound assignment target");
    };

    if (assignment.operator_ == "+=") {
      emitCompound(OpCode::ADD);
      break;
    }
    if (assignment.operator_ == "-=") {
      emitCompound(OpCode::SUB);
      break;
    }
    if (assignment.operator_ == "*=") {
      emitCompound(OpCode::MUL);
      break;
    }
    if (assignment.operator_ == "/=") {
      emitCompound(OpCode::DIV);
      break;
    }

    throw std::runtime_error("Unsupported assignment operator: " +
                             assignment.operator_);
  }

  case ast::NodeType::CallExpression:
    compileCallExpression(static_cast<const ast::CallExpression &>(expression));
    break;

  case ast::NodeType::MemberExpression: {
    const auto &member = static_cast<const ast::MemberExpression &>(expression);
    auto *property =
        dynamic_cast<const ast::Identifier *>(member.property.get());
    if (!member.object || !property) {
      throw std::runtime_error("Unsupported member expression");
    }
    compileExpression(*member.object);
    emit(OpCode::LOAD_CONST, addConstant(property->symbol));
    emit(OpCode::OBJECT_GET);
    break;
  }

  case ast::NodeType::IndexExpression: {
    const auto &index = static_cast<const ast::IndexExpression &>(expression);
    if (!index.object || !index.index) {
      throw std::runtime_error("Malformed index expression");
    }
    compileExpression(*index.object);
    compileExpression(*index.index);
    emit(OpCode::ARRAY_GET);
    break;
  }

  case ast::NodeType::SpreadExpression: {
    const auto &spread = static_cast<const ast::SpreadExpression &>(expression);
    if (!spread.target) {
      throw std::runtime_error("Spread expression missing target");
    }
    compileExpression(*spread.target);
    emit(OpCode::SPREAD);
    break;
  }

  case ast::NodeType::AwaitExpression: {
    const auto &await_expr =
        static_cast<const ast::AwaitExpression &>(expression);
    if (!await_expr.argument) {
      throw std::runtime_error("Await expression missing argument");
    }
    compileExpression(*await_expr.argument);
    emit(OpCode::CALL_HOST,
         std::vector<BytecodeValue>{"async.await", static_cast<uint32_t>(1)});
    break;
  }

  default:
    throw std::runtime_error("Unsupported expression in bytecode compiler: " +
                             expression.toString());
  }
}

void ByteCompiler::compileCallExpression(
    const ast::CallExpression &expression) {
  if (!expression.callee) {
    throw std::runtime_error("Call expression missing callee");
  }

  uint32_t arg_count = static_cast<uint32_t>(expression.args.size());
  bool hasKwargs = !expression.kwargs.empty();

  if (expression.callee->kind == ast::NodeType::Identifier) {
    const auto &callee_id =
        static_cast<const ast::Identifier &>(*expression.callee);
    const auto *binding = bindingFor(callee_id);
    if (binding && binding->kind == ResolvedBindingKind::Builtin &&
        top_level_struct_names_.find(callee_id.symbol) !=
            top_level_struct_names_.end()) {
      emit(OpCode::LOAD_CONST, addConstant(callee_id.symbol));
      uint32_t totalArgs = 1; // type name
      for (const auto &arg : expression.args) {
        if (!arg) {
          throw std::runtime_error("Call expression contains null argument");
        }
        compileExpression(*arg);
        totalArgs++;
      }
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{"struct.new", totalArgs});
      return;
    }
  }

  // Check for method call on primitive (prototype method)
  if (expression.callee->kind == ast::NodeType::MemberExpression) {
    const auto &member =
        static_cast<const ast::MemberExpression &>(*expression.callee);
    auto *property =
        dynamic_cast<const ast::Identifier *>(member.property.get());
    if (!member.object || !property) {
      throw std::runtime_error("Unsupported member call expression");
    }

    // Array higher-order methods should work for both simple receivers
    // (nums.map(...)) and chained receivers (nums.sort().map(...)).
    bool is_array_module_call = false;
    if (member.object->kind == ast::NodeType::Identifier) {
      const auto &receiver_id =
          static_cast<const ast::Identifier &>(*member.object);
      if (const auto *receiver_binding = bindingFor(receiver_id)) {
        if (receiver_binding->kind == ResolvedBindingKind::HostGlobal &&
            receiver_binding->name == "array") {
          is_array_module_call = true;
        }
      }
    }

    if (!is_array_module_call &&
        (property->symbol == "map" || property->symbol == "filter" ||
        property->symbol == "reduce" || property->symbol == "foreach" ||
        property->symbol == "sort")) {
      compileExpression(*member.object);
      if (property->symbol == "map") {
        if (expression.args.size() != 1 || !expression.args[0]) {
          throw std::runtime_error("map() requires 1 callback argument");
        }
        compileExpression(*expression.args[0]);
        emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"any.map",
                                                           static_cast<uint32_t>(2)});
        return;
      }
      if (property->symbol == "filter") {
        if (expression.args.size() != 1 || !expression.args[0]) {
          throw std::runtime_error("filter() requires 1 callback argument");
        }
        compileExpression(*expression.args[0]);
        emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"any.filter",
                                                           static_cast<uint32_t>(2)});
        return;
      }
      if (property->symbol == "reduce") {
        if (expression.args.size() != 2 || !expression.args[0] ||
            !expression.args[1]) {
          throw std::runtime_error("reduce() requires callback and initial value");
        }
        compileExpression(*expression.args[0]);
        compileExpression(*expression.args[1]);
        emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"any.reduce",
                                                           static_cast<uint32_t>(3)});
        return;
      }
      if (property->symbol == "foreach") {
        if (expression.args.size() != 1 || !expression.args[0]) {
          throw std::runtime_error("foreach() requires 1 callback argument");
        }
        compileExpression(*expression.args[0]);
        emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"any.foreach",
                                                           static_cast<uint32_t>(2)});
        return;
      }
      // sort
      for (const auto &arg : expression.args) {
        if (!arg) {
          throw std::runtime_error("Call expression contains null argument");
        }
        compileExpression(*arg);
      }
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{
               "any.sort", static_cast<uint32_t>(expression.args.size() + 1)});
      return;
    }

    const bool is_simple_receiver =
        member.object->kind == ast::NodeType::StringLiteral ||
        member.object->kind == ast::NodeType::ArrayLiteral ||
        member.object->kind == ast::NodeType::ObjectLiteral ||
        member.object->kind == ast::NodeType::Identifier;

    // Method chaining path: receiver is an arbitrary expression (e.g.
    // nums.map(f).filter(g)). Route through dynamic any.* dispatch.
    if (!is_simple_receiver) {
      compileExpression(*member.object);
      for (const auto &arg : expression.args) {
        if (!arg) {
          throw std::runtime_error("Call expression contains null argument");
        }
        compileExpression(*arg);
      }
      uint32_t totalArgs = arg_count + 1;
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          emit(OpCode::DUP);
          compileExpression(*kwarg.value);
          emit(OpCode::OBJECT_SET, kwarg.name);
        }
        totalArgs++;
      }
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{"any." + property->symbol, totalArgs});
      return;
    }

    // Check if object is a literal or identifier (variable)
    bool isPrimitiveObject =
        member.object->kind == ast::NodeType::StringLiteral ||
        member.object->kind == ast::NodeType::ArrayLiteral ||
        member.object->kind == ast::NodeType::ObjectLiteral;
    bool isVariableObject = member.object->kind == ast::NodeType::Identifier;

    if (property && (isPrimitiveObject || isVariableObject)) {
      // Compile prototype method call:
      // 1. Load the object (literal or variable) as first argument
      // 2. Load remaining arguments
      // 3. Call the prototype method (e.g., "string.upper")

      std::string methodName;
      std::string typeName;

      // Determine type from object kind
      if (member.object->kind == ast::NodeType::StringLiteral) {
        typeName = "string";
      } else if (member.object->kind == ast::NodeType::ArrayLiteral) {
        typeName = "array";
      } else if (member.object->kind == ast::NodeType::ObjectLiteral) {
        typeName = "object";
      } else if (member.object->kind == ast::NodeType::Identifier) {
        // Check if this is a host global (like "math", "string", etc.)
        const auto *ident =
            static_cast<const ast::Identifier *>(member.object.get());
        auto binding = bindingFor(*ident);
        if (binding && binding->kind == ResolvedBindingKind::HostGlobal) {
          // Use the host global name as the type (lowercase for
          // case-insensitive matching)
          typeName = binding->name;
          // Convert to lowercase for case-insensitive module access
          std::transform(typeName.begin(), typeName.end(), typeName.begin(),
                         ::tolower);
        } else {
          // For variables, we'll use a runtime type check
          // The method name will be resolved at runtime based on the value's
          // type For now, we'll try all three types
          typeName = "any";
        }
      }

      if (typeName != "any") {
        // Check for object.* VM intrinsics (object.keys, object.values, etc.)
        if (typeName == "object") {
          if (property->symbol == "keys") {
            // object.keys(obj) → OBJECT_KEYS opcode
            for (const auto &arg : expression.args) {
              if (!arg) {
                throw std::runtime_error(
                    "Call expression contains null argument");
              }
              compileExpression(*arg);
            }
            emit(OpCode::OBJECT_KEYS);
            return;
          } else if (property->symbol == "values") {
            // object.values(obj) → OBJECT_VALUES opcode
            for (const auto &arg : expression.args) {
              if (!arg) {
                throw std::runtime_error(
                    "Call expression contains null argument");
              }
              compileExpression(*arg);
            }
            emit(OpCode::OBJECT_VALUES);
            return;
          } else if (property->symbol == "entries") {
            // object.entries(obj) → OBJECT_ENTRIES opcode
            for (const auto &arg : expression.args) {
              if (!arg) {
                throw std::runtime_error(
                    "Call expression contains null argument");
              }
              compileExpression(*arg);
            }
            emit(OpCode::OBJECT_ENTRIES);
            return;
          } else if (property->symbol == "has") {
            // object.has(obj, key) → OBJECT_HAS opcode
            for (const auto &arg : expression.args) {
              if (!arg) {
                throw std::runtime_error(
                    "Call expression contains null argument");
              }
              compileExpression(*arg);
            }
            emit(OpCode::OBJECT_HAS);
            return;
          } else if (property->symbol == "del") {
            // object.del(obj, key) → OBJECT_DELETE opcode
            for (const auto &arg : expression.args) {
              if (!arg) {
                throw std::runtime_error(
                    "Call expression contains null argument");
              }
              compileExpression(*arg);
            }
            emit(OpCode::OBJECT_DELETE);
            return;
          } else if (property->symbol == "set") {
            // object.set(obj, key, value) → OBJECT_SET opcode
            // Stack: [..., obj, value, key] → pops all, pushes obj
            if (expression.args.size() < 3) {
              throw std::runtime_error("object.set() requires 3 arguments");
            }
            compileExpression(*expression.args[0]);  // obj
            compileExpression(*expression.args[2]);  // value
            compileExpression(*expression.args[1]);  // key
            emit(OpCode::OBJECT_SET);
            emit(OpCode::POP);  // Remove obj from stack, keep void
            return;
          } else if (property->symbol == "get") {
            // object.get(obj, key) → OBJECT_GET opcode
            // Stack: [..., obj, key] → pops both, pushes value
            if (expression.args.size() < 2) {
              throw std::runtime_error("object.get() requires 2 arguments");
            }
            compileExpression(*expression.args[0]);  // obj
            compileExpression(*expression.args[1]);  // key
            emit(OpCode::OBJECT_GET);
            return;
          }
        }
      }  // if (typeName != "any")

      // Check for array.* VM intrinsics (for variables too - VM checks type at runtime)
      if (typeName == "array" || typeName == "any") {
        if (property->symbol == "len" || property->symbol == "length") {
          compileExpression(*member.object);
          emit(OpCode::ARRAY_LEN);
          return;
        } else if (property->symbol == "push") {
          compileExpression(*member.object);
          for (const auto &arg : expression.args) {
            if (!arg) throw std::runtime_error("Call expression contains null argument");
            compileExpression(*arg);
          }
          emit(OpCode::ARRAY_PUSH);
          return;
        } else if (property->symbol == "pop") {
          compileExpression(*member.object);
          emit(OpCode::ARRAY_POP);
          return;
        } else if (property->symbol == "has") {
          compileExpression(*member.object);
          for (const auto &arg : expression.args) {
            if (!arg) throw std::runtime_error("Call expression contains null argument");
            compileExpression(*arg);
          }
          emit(OpCode::ARRAY_HAS);
          return;
        } else if (property->symbol == "find") {
          compileExpression(*member.object);
          for (const auto &arg : expression.args) {
            if (!arg) throw std::runtime_error("Call expression contains null argument");
            compileExpression(*arg);
          }
          emit(OpCode::ARRAY_FIND);
          return;
        } else if (property->symbol == "map") {
          compileExpression(*member.object);
          compileExpression(*expression.args[0]);
          emit(OpCode::ARRAY_MAP);
          return;
        } else if (property->symbol == "filter") {
          compileExpression(*member.object);
          compileExpression(*expression.args[0]);
          emit(OpCode::ARRAY_FILTER);
          return;
        } else if (property->symbol == "reduce") {
          compileExpression(*member.object);
          compileExpression(*expression.args[0]);
          compileExpression(*expression.args[1]);
          emit(OpCode::ARRAY_REDUCE);
          return;
        } else if (property->symbol == "foreach") {
          compileExpression(*member.object);
          compileExpression(*expression.args[0]);
          emit(OpCode::ARRAY_FOREACH);
          return;
        } else if (property->symbol == "sort") {
          for (const auto &arg : expression.args) {
            if (!arg) throw std::runtime_error("Call expression contains null argument");
            compileExpression(*arg);
          }
          emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"array.sort", static_cast<uint32_t>(expression.args.size())});
          return;
        }
      }

      if (typeName != "any") {
        // Check for string.* VM intrinsics
        if (typeName == "string") {
          if (property->symbol == "len" || property->symbol == "length") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_LEN);
            return;
          } else if (property->symbol == "upper") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_UPPER);
            return;
          } else if (property->symbol == "lower") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_LOWER);
            return;
          } else if (property->symbol == "trim") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_TRIM);
            return;
          } else if (property->symbol == "includes" || property->symbol == "has") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_HAS);
            return;
          } else if (property->symbol == "startswith" || property->symbol == "starts") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_STARTS);
            return;
          } else if (property->symbol == "endswith" || property->symbol == "ends") {
            for (const auto &arg : expression.args) {
              if (!arg) throw std::runtime_error("Call expression contains null argument");
              compileExpression(*arg);
            }
            emit(OpCode::STRING_ENDS);
            return;
          }
        }

        // Check if this is a module.function call (like http.get, network.post)
        // In this case, the module name is just a namespace prefix, not an object to pass
        std::string fullMethodName = typeName + "." + property->symbol;
        
        // Check if this is a registered host function with the full name
        if (host_builtin_names_.find(fullMethodName) != host_builtin_names_.end()) {
          // This is a module.function call - don't pass the module as an argument
          // Just compile the arguments and call the function directly
          for (const auto &arg : expression.args) {
            if (!arg) {
              throw std::runtime_error(
                  "Call expression contains null argument");
            }
            compileExpression(*arg);
          }

          // Compile kwargs as object if present
          uint32_t totalArgs = arg_count;
          if (hasKwargs) {
            emit(OpCode::OBJECT_NEW);
            for (const auto &kwarg : expression.kwargs) {
              emit(OpCode::DUP);
              compileExpression(*kwarg.value);
              emit(OpCode::OBJECT_SET, kwarg.name);
            }
            totalArgs++;
          }

          // Call the host function directly
          emit(OpCode::CALL_HOST,
               std::vector<BytecodeValue>{fullMethodName, totalArgs});
          return;
        }
        
        // Otherwise, treat as prototype method (pass object as first arg)
        methodName = typeName + "." + property->symbol;

        // Check if this is a registered prototype method
        if (host_builtin_names_.find(methodName) != host_builtin_names_.end()) {
          // Compile the object (push as first argument)
          compileExpression(*member.object);

          // Compile positional arguments
          for (const auto &arg : expression.args) {
            if (!arg) {
              throw std::runtime_error(
                  "Call expression contains null argument");
            }
            compileExpression(*arg);
          }

          // Compile kwargs as object if present
          uint32_t totalArgs = arg_count + 1;  // +1 for object (self/this)
          if (hasKwargs) {
            emit(OpCode::OBJECT_NEW);
            for (const auto &kwarg : expression.kwargs) {
              emit(OpCode::DUP);
              compileExpression(*kwarg.value);
              emit(OpCode::OBJECT_SET, kwarg.name);
            }
            totalArgs++;
          }

          // Call the prototype method (object is already first arg)
          emit(OpCode::CALL_HOST,
               std::vector<BytecodeValue>{methodName, totalArgs});
          return;
        }
      } else {
        // For variables, check if this is a module.function call (like http.get)
        // In this case, don't pass the module as an argument
        const auto *ident = static_cast<const ast::Identifier *>(member.object.get());
        if (ident) {
          std::string fullMethodName = ident->symbol + "." + property->symbol;
          
          // Check if this is a registered host function with the full name
          if (host_builtin_names_.find(fullMethodName) != host_builtin_names_.end()) {
            // This is a module.function call - don't pass the module as an argument
            // Just compile the arguments and call the function directly
            for (const auto &arg : expression.args) {
              if (!arg) {
                throw std::runtime_error(
                    "Call expression contains null argument");
              }
              compileExpression(*arg);
            }

            // Compile kwargs as object if present
            uint32_t totalArgs = arg_count;
            if (hasKwargs) {
              emit(OpCode::OBJECT_NEW);
              for (const auto &kwarg : expression.kwargs) {
                emit(OpCode::DUP);
                compileExpression(*kwarg.value);
                emit(OpCode::OBJECT_SET, kwarg.name);
              }
              totalArgs++;
            }

            // Call the host function directly
            emit(OpCode::CALL_HOST,
                 std::vector<BytecodeValue>{fullMethodName, totalArgs});
            return;
          }
        }
        
        // For variables, emit runtime method dispatch via any.*
        // First compile the object to get its value
        compileExpression(*member.object);

        // Compile positional arguments
        for (const auto &arg : expression.args) {
          if (!arg) {
            throw std::runtime_error("Call expression contains null argument");
          }
          compileExpression(*arg);
        }

        // Compile kwargs as object if present
        uint32_t totalArgs = arg_count + 1;  // +1 for object (self/this)
        if (hasKwargs) {
          emit(OpCode::OBJECT_NEW);
          for (const auto &kwarg : expression.kwargs) {
            emit(OpCode::DUP);
            compileExpression(*kwarg.value);
            emit(OpCode::OBJECT_SET, kwarg.name);
          }
          totalArgs++;
        }

        // Emit CALL_HOST with any.* method name
        // The HostBridge any.* dispatcher will determine type and call appropriate method
        methodName = "any." + property->symbol;
        emit(OpCode::CALL_HOST,
             std::vector<BytecodeValue>{methodName, totalArgs});
        return;
      }
    }
  }

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
      
      // Compile kwargs as object if present
      uint32_t totalArgs = arg_count;
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          emit(OpCode::DUP);
          compileExpression(*kwarg.value);
          emit(OpCode::OBJECT_SET, kwarg.name);
        }
        totalArgs++;
      }
      
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{binding->name, totalArgs});
      return;
    }
  }

  if (expression.callee->kind == ast::NodeType::Identifier) {
    auto callee_name = getCalleeName(*expression.callee);
    if (callee_name &&
        host_builtin_names_.find(*callee_name) != host_builtin_names_.end()) {
      for (const auto &arg : expression.args) {
        if (!arg) {
          throw std::runtime_error("Call expression contains null argument");
        }
        compileExpression(*arg);
      }
      
      // Compile kwargs as object if present
      uint32_t totalArgs = arg_count;
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          emit(OpCode::DUP);
          compileExpression(*kwarg.value);
          emit(OpCode::OBJECT_SET, kwarg.name);
        }
        totalArgs++;
      }
      
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{*callee_name, totalArgs});
      return;
    }
  }

  compileExpression(*expression.callee);
  
  // Compile arguments, handling spread expressions
  uint32_t actualArgCount = 0;
  for (const auto &arg : expression.args) {
    if (!arg) {
      throw std::runtime_error("Call expression contains null argument");
    }
    if (arg->kind == ast::NodeType::SpreadExpression) {
      const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
      // Handle spread over array literal: expand at compile time
      if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
        const auto &arrLit = static_cast<const ast::ArrayLiteral &>(*spread.target);
        for (const auto &elem : arrLit.elements) {
          if (elem) {
            compileExpression(*elem);
            actualArgCount++;
          }
        }
      } else {
        // Dynamic spread not yet supported - skip for now
        // TODO: Implement runtime spread expansion
        throw std::runtime_error("Spread operator in function calls only supports array literals for now");
      }
    } else {
      compileExpression(*arg);
      actualArgCount++;
    }
  }

  // Compile kwargs as object if present
  if (hasKwargs) {
    emit(OpCode::OBJECT_NEW);
    for (const auto &kwarg : expression.kwargs) {
      emit(OpCode::DUP);
      compileExpression(*kwarg.value);
      emit(OpCode::OBJECT_SET, kwarg.name);
    }
    actualArgCount++;
  }

  // TCO: Emit TAIL_CALL if in tail position and callee is a user-defined function
  if (in_tail_position_ && expression.callee->kind == ast::NodeType::Identifier) {
    const auto &callee_id = static_cast<const ast::Identifier &>(*expression.callee);
    const auto *binding = bindingFor(callee_id);
    if (binding && (binding->kind == ResolvedBindingKind::Local ||
                    binding->kind == ResolvedBindingKind::Upvalue ||
                    binding->kind == ResolvedBindingKind::GlobalFunction)) {
      emit(OpCode::TAIL_CALL, actualArgCount);
      emitted_tail_call_ = true;
      return;
    }
  }

  emit(OpCode::CALL, actualArgCount);
}

void ByteCompiler::compileIfStatement(const ast::IfStatement &statement) {
  if (!statement.condition || !statement.consequence) {
    throw std::runtime_error("Malformed if statement");
  }

  compileExpression(*statement.condition);
  uint32_t else_jump = emitJump(OpCode::JUMP_IF_FALSE);

  // TCO: If we're in tail position, both branches are also in tail position
  // But we need to track if EACH branch emits a tail call
  bool was_tail = in_tail_position_;
  enterTailPosition();
  clearTailCallFlag();
  compileStatement(*statement.consequence);
  bool consequence_was_tail = wasTailCall();
  exitTailPosition();
  
  // If consequence didn't emit tail call, emit RETURN
  if (was_tail && !consequence_was_tail) {
    emit(OpCode::RETURN);
  }

  if (statement.alternative) {
    uint32_t end_jump = emitJump(OpCode::JUMP);
    uint32_t else_target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(else_jump, else_target);

    enterTailPosition();
    clearTailCallFlag();
    compileStatement(*statement.alternative);
    bool alternative_was_tail = wasTailCall();
    exitTailPosition();
    
    // If alternative didn't emit tail call, emit RETURN
    if (was_tail && !alternative_was_tail) {
      emit(OpCode::RETURN);
    }
    
    uint32_t end_target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(end_jump, end_target);
    
    // TCO: Only set tail call flag if BOTH branches emitted tail calls
    if (was_tail && consequence_was_tail && alternative_was_tail) {
      emitted_tail_call_ = true;
    }
  } else {
    uint32_t target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(else_jump, target);
    
    // If there's no alternative, we can't do TCO (the if might not execute)
    if (was_tail) {
      emitted_tail_call_ = false;
    }
  }
  
  // Restore tail position state
  if (was_tail) {
    enterTailPosition();
  }
}

void ByteCompiler::compileWhileStatement(const ast::WhileStatement &statement) {
  if (!statement.condition || !statement.body) {
    throw std::runtime_error("Malformed while statement");
  }

  uint32_t loop_start =
      static_cast<uint32_t>(current_function->instructions.size());

  compileExpression(*statement.condition);
  uint32_t end_jump = emitJump(OpCode::JUMP_IF_FALSE);

  compileStatement(*statement.body);
  emit(OpCode::JUMP, loop_start);

  uint32_t loop_end =
      static_cast<uint32_t>(current_function->instructions.size());
  patchJump(end_jump, loop_end);
}

void ByteCompiler::compileForStatement(const ast::ForStatement &statement) {
  if (statement.iterators.empty() || !statement.iterable || !statement.body) {
    throw std::runtime_error("Malformed for statement");
  }

  bool multiVar = statement.iterators.size() > 1;
  
  // Get iterator variable slots
  std::vector<uint32_t> iterSlots;
  for (const auto &iter : statement.iterators) {
    uint32_t slot = declarationSlot(*iter);
    reserveLocalSlot(slot);
    iterSlots.push_back(slot);
  }

  // Compile iterable and create iterator: iter(iterable)
  compileExpression(*statement.iterable);
  emit(OpCode::ITER_NEW);

  // Create temp variable for iterator
  uint32_t iterVarSlot = next_local_index++;
  reserveLocalSlot(iterVarSlot);
  emit(OpCode::STORE_VAR, iterVarSlot);

  uint32_t loop_start =
      static_cast<uint32_t>(current_function->instructions.size());

  // Call iterator.next()
  emit(OpCode::LOAD_VAR, iterVarSlot);
  emit(OpCode::ITER_NEXT);

  // Store result in temp
  uint32_t resultSlot = next_local_index++;
  reserveLocalSlot(resultSlot);
  emit(OpCode::STORE_VAR, resultSlot);

  // Check result.done - if true, exit loop
  emit(OpCode::LOAD_VAR, resultSlot);
  emit(OpCode::LOAD_CONST, addConstant(std::string("done")));
  emit(OpCode::OBJECT_GET);
  uint32_t end_jump = emitJump(OpCode::JUMP_IF_TRUE);

  // Get result.value
  emit(OpCode::LOAD_VAR, resultSlot);
  emit(OpCode::LOAD_CONST, addConstant(std::string("value")));
  emit(OpCode::OBJECT_GET);
  
  if (multiVar && iterSlots.size() >= 2) {
    // For multi-variable iteration: first var gets key, second gets value
    // The iterator returns the key, we need to look up the value from the iterable
    emit(OpCode::STORE_VAR, iterSlots[0]);  // Store key in first var
    
    // Look up value: iterable[key]
    compileExpression(*statement.iterable);  // Reload iterable
    emit(OpCode::LOAD_VAR, iterSlots[0]);    // Load key
    emit(OpCode::OBJECT_GET);                // Get value
    emit(OpCode::STORE_VAR, iterSlots[1]);   // Store value in second var
  } else {
    // Single variable - just store the value (key for objects)
    emit(OpCode::STORE_VAR, iterSlots[0]);
  }

  // Execute body
  compileStatement(*statement.body);

  // Jump back to loop start
  emit(OpCode::JUMP, loop_start);

  // Patch end jump
  uint32_t loop_end =
      static_cast<uint32_t>(current_function->instructions.size());
  patchJump(end_jump, loop_end);
}

void ByteCompiler::compileBlockStatement(const ast::BlockStatement &block) {
  const auto &stmts = block.body;
  if (stmts.empty()) {
    return;
  }
  
  // Compile all but last statement (not in tail position)
  for (size_t i = 0; i < stmts.size() - 1; i++) {
    if (!stmts[i]) {
      continue;
    }
    compileStatement(*stmts[i]);
  }
  
  // Last statement: inherit tail position
  if (stmts.back()) {
    compileStatement(*stmts.back());
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

  case ast::NodeType::TryExpression: {
    const auto &try_expr = static_cast<const ast::TryExpression &>(statement);
    if (try_expr.tryBody) {
      collectFunctionDeclarations(*try_expr.tryBody, out);
    }
    if (try_expr.catchBody) {
      collectFunctionDeclarations(*try_expr.catchBody, out);
    }
    if (try_expr.finallyBlock) {
      collectFunctionDeclarations(*try_expr.finallyBlock, out);
    }
    break;
  }

  default:
    break;
  }
}

void ByteCompiler::collectLambdaExpressions(
    const ast::Statement &statement,
    std::vector<const ast::LambdaExpression *> &out) const {
  switch (statement.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &expr_statement =
        static_cast<const ast::ExpressionStatement &>(statement);
    if (expr_statement.expression) {
      collectLambdaExpressions(*expr_statement.expression, out);
    }
    break;
  }
  case ast::NodeType::LetDeclaration: {
    const auto &let_statement =
        static_cast<const ast::LetDeclaration &>(statement);
    if (let_statement.value) {
      collectLambdaExpressions(*let_statement.value, out);
    }
    break;
  }
  case ast::NodeType::ReturnStatement: {
    const auto &return_statement =
        static_cast<const ast::ReturnStatement &>(statement);
    if (return_statement.argument) {
      collectLambdaExpressions(*return_statement.argument, out);
    }
    break;
  }
  case ast::NodeType::BlockStatement: {
    const auto &block_statement =
        static_cast<const ast::BlockStatement &>(statement);
    for (const auto &nested : block_statement.body) {
      if (nested) {
        collectLambdaExpressions(*nested, out);
      }
    }
    break;
  }
  case ast::NodeType::IfStatement: {
    const auto &if_statement = static_cast<const ast::IfStatement &>(statement);
    if (if_statement.condition) {
      collectLambdaExpressions(*if_statement.condition, out);
    }
    if (if_statement.consequence) {
      collectLambdaExpressions(*if_statement.consequence, out);
    }
    if (if_statement.alternative) {
      collectLambdaExpressions(*if_statement.alternative, out);
    }
    break;
  }
  case ast::NodeType::WhileStatement: {
    const auto &while_statement =
        static_cast<const ast::WhileStatement &>(statement);
    if (while_statement.condition) {
      collectLambdaExpressions(*while_statement.condition, out);
    }
    if (while_statement.body) {
      collectLambdaExpressions(*while_statement.body, out);
    }
    break;
  }
  case ast::NodeType::ForStatement: {
    const auto &for_statement = static_cast<const ast::ForStatement &>(statement);
    if (for_statement.iterable) {
      collectLambdaExpressions(*for_statement.iterable, out);
    }
    if (for_statement.body) {
      collectLambdaExpressions(*for_statement.body, out);
    }
    break;
  }
  case ast::NodeType::FunctionDeclaration: {
    const auto &function =
        static_cast<const ast::FunctionDeclaration &>(statement);
    if (function.body) {
      for (const auto &nested : function.body->body) {
        if (nested) {
          collectLambdaExpressions(*nested, out);
        }
      }
    }
    break;
  }
  default:
    break;
  }
}

void ByteCompiler::collectLambdaExpressions(
    const ast::Expression &expression,
    std::vector<const ast::LambdaExpression *> &out) const {
  switch (expression.kind) {
  case ast::NodeType::LambdaExpression: {
    const auto &lambda = static_cast<const ast::LambdaExpression &>(expression);
    out.push_back(&lambda);
    if (lambda.body) {
      collectLambdaExpressions(*lambda.body, out);
    }
    break;
  }
  case ast::NodeType::BinaryExpression: {
    const auto &binary = static_cast<const ast::BinaryExpression &>(expression);
    if (binary.left) {
      collectLambdaExpressions(*binary.left, out);
    }
    if (binary.right) {
      collectLambdaExpressions(*binary.right, out);
    }
    break;
  }
  case ast::NodeType::CallExpression: {
    const auto &call = static_cast<const ast::CallExpression &>(expression);
    if (call.callee) {
      collectLambdaExpressions(*call.callee, out);
    }
    for (const auto &arg : call.args) {
      if (arg) {
        collectLambdaExpressions(*arg, out);
      }
    }
    for (const auto &kwarg : call.kwargs) {
      if (kwarg.value) {
        collectLambdaExpressions(*kwarg.value, out);
      }
    }
    break;
  }
  case ast::NodeType::AssignmentExpression: {
    const auto &assignment =
        static_cast<const ast::AssignmentExpression &>(expression);
    if (assignment.target) {
      collectLambdaExpressions(*assignment.target, out);
    }
    if (assignment.value) {
      collectLambdaExpressions(*assignment.value, out);
    }
    break;
  }
  case ast::NodeType::MemberExpression: {
    const auto &member = static_cast<const ast::MemberExpression &>(expression);
    if (member.object) {
      collectLambdaExpressions(*member.object, out);
    }
    break;
  }
  case ast::NodeType::IndexExpression: {
    const auto &index = static_cast<const ast::IndexExpression &>(expression);
    if (index.object) {
      collectLambdaExpressions(*index.object, out);
    }
    if (index.index) {
      collectLambdaExpressions(*index.index, out);
    }
    break;
  }
  case ast::NodeType::ArrayLiteral: {
    const auto &array = static_cast<const ast::ArrayLiteral &>(expression);
    for (const auto &element : array.elements) {
      if (element) {
        collectLambdaExpressions(*element, out);
      }
    }
    break;
  }
  case ast::NodeType::SetExpression: {
    const auto &set = static_cast<const ast::SetExpression &>(expression);
    for (const auto &element : set.elements) {
      if (element) {
        collectLambdaExpressions(*element, out);
      }
    }
    break;
  }
  case ast::NodeType::ObjectLiteral: {
    const auto &object = static_cast<const ast::ObjectLiteral &>(expression);
    for (const auto &pair : object.pairs) {
      if (pair.second) {
        collectLambdaExpressions(*pair.second, out);
      }
    }
    break;
  }
  case ast::NodeType::SpreadExpression: {
    const auto &spread = static_cast<const ast::SpreadExpression &>(expression);
    if (spread.target) {
      collectLambdaExpressions(*spread.target, out);
    }
    break;
  }
  case ast::NodeType::InterpolatedStringExpression: {
    const auto &interp =
        static_cast<const ast::InterpolatedStringExpression &>(expression);
    for (const auto &segment : interp.segments) {
      if (!segment.isString && segment.expression) {
        collectLambdaExpressions(*segment.expression, out);
      }
    }
    break;
  }
  case ast::NodeType::AwaitExpression: {
    const auto &await_expr =
        static_cast<const ast::AwaitExpression &>(expression);
    if (await_expr.argument) {
      collectLambdaExpressions(*await_expr.argument, out);
    }
    break;
  }
  case ast::NodeType::AsyncExpression: {
    const auto &async_expr =
        static_cast<const ast::AsyncExpression &>(expression);
    if (async_expr.body) {
      collectLambdaExpressions(*async_expr.body, out);
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
    auto *property =
        dynamic_cast<const ast::Identifier *>(member.property.get());
    if (!object || !property) {
      return std::nullopt;
    }

    return object->symbol + "." + property->symbol;
  }

  return std::nullopt;
}

const ResolvedBinding *
ByteCompiler::bindingFor(const ast::Identifier &id) const {
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

void ByteCompiler::enterFunction(BytecodeFunction &&function,
                                 std::optional<uint32_t> slot) {
  if (current_function) {
    throw std::runtime_error("Nested function compilation is not supported");
  }

  current_function = std::make_unique<BytecodeFunction>(std::move(function));
  current_function_slot_ = slot;
  resetLocals();
}

void ByteCompiler::leaveFunction() {
  if (!current_function) {
    throw std::runtime_error("No active function to close");
  }

  current_function->local_count = next_local_index;
  if (current_function_slot_.has_value()) {
    const uint32_t slot = *current_function_slot_;
    if (slot >= compiled_functions.size()) {
      compiled_functions.resize(slot + 1);
    }
    compiled_functions[slot] = std::move(current_function);
  } else {
    compiled_functions.push_back(std::move(current_function));
  }
  current_function_slot_.reset();
  resetLocals();
}

void ByteCompiler::resetLocals() { next_local_index = 0; }

// Tail call optimization - track tail position context
void ByteCompiler::enterTailPosition() { in_tail_position_ = true; }

void ByteCompiler::exitTailPosition() { in_tail_position_ = false; }

bool ByteCompiler::isInTailPosition() const { return in_tail_position_; }

bool ByteCompiler::wasTailCall() const { return emitted_tail_call_; }

void ByteCompiler::clearTailCallFlag() { emitted_tail_call_ = false; }

// Compile hotkey binding: hotkey => action
void ByteCompiler::compileHotkeyBinding(const ast::HotkeyBinding &binding) {
  // Compile the action (body)
  if (binding.action) {
    compileStatement(*binding.action);
  }

  // For each hotkey in the binding, register it
  for (const auto &hotkeyExpr : binding.hotkeys) {
    if (!hotkeyExpr)
      continue;

    // Hotkey expression should be a string or identifier
    // We need to call hotkey.register(hotkey_string, callback_function)
    // The callback is a closure that wraps the action

    // For now, emit a host function call to hotkey.register
    // The hotkey string needs to be evaluated
    compileExpression(*hotkeyExpr);

    // The action body was already compiled above
    // We need to create a closure for the callback
    // This is complex - for now we'll emit a placeholder
    // TODO: Properly implement hotkey registration with closures
  }
}

// Compile input statement: > "text" or > {Enter}
void ByteCompiler::compileInputStatement(const ast::InputStatement &statement) {
  // Each input command becomes a host function call to io.send or similar
  for (const auto &cmd : statement.commands) {
    switch (cmd.type) {
    case ast::InputCommand::SendText:
      // io.send(cmd.text)
      emit(OpCode::LOAD_CONST, addConstant(cmd.text));
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"io.send", 1});
      break;
    case ast::InputCommand::SendKey:
      // io.sendKey(cmd.key)
      emit(OpCode::LOAD_CONST, addConstant(cmd.key));
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"io.sendKey", 1});
      break;
    case ast::InputCommand::MouseClick:
      // io.mouseClick(cmd.text) - text contains button name
      emit(OpCode::LOAD_CONST, addConstant(cmd.text));
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"io.mouseClick", 1});
      break;
    case ast::InputCommand::MouseMove:
      // io.mouseMove(x, y) - xExprStr and yExprStr contain coordinates
      // For now, treat as strings and evaluate them
      // TODO: Properly evaluate expressions
      emit(OpCode::LOAD_CONST, addConstant(cmd.xExprStr));
      emit(OpCode::LOAD_CONST, addConstant(cmd.yExprStr));
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"io.mouseMove", 2});
      break;
    case ast::InputCommand::MouseRelative:
      // Similar to MouseMove but relative
      emit(OpCode::LOAD_CONST, addConstant(cmd.xExprStr));
      emit(OpCode::LOAD_CONST, addConstant(cmd.yExprStr));
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"io.mouseMoveRel", 2});
      break;
    case ast::InputCommand::Sleep:
      // sleep_ms(cmd.duration)
      emit(OpCode::LOAD_CONST, addConstant(std::stoi(cmd.duration)));
      emit(OpCode::CALL_HOST, std::vector<BytecodeValue>{"sleep_ms", 1});
      break;
    default:
      // Other commands not yet implemented
      break;
    }
  }
}

} // namespace havel::compiler
