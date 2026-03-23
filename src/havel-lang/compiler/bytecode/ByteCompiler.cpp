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
  top_level_function_indices_by_name_.clear();

  LexicalResolver resolver(host_builtin_names_, host_global_names_);
  lexical_resolution_ = resolver.resolve(program);
  if (!resolver.errors().empty()) {
    throw std::runtime_error("Lexical resolution failed: " +
                             resolver.errors().front());
  }

  // Reserve function indices so forward references and recursion emit stable
  // function objects.
  std::vector<const ast::FunctionDeclaration *> declared_functions;
  declared_functions.reserve(program.body.size());

  uint32_t next_function_index = 0;
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
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

  enterFunction(
      BytecodeFunction(function.name->symbol,
                       static_cast<uint32_t>(function.parameters.size()), 0));
  auto upvalues_it = lexical_resolution_.function_upvalues.find(&function);
  if (upvalues_it != lexical_resolution_.function_upvalues.end()) {
    current_function->upvalues = upvalues_it->second;
  }

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
  auto source_scope = atNode(statement);
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

  default:
    throw std::runtime_error("Unsupported statement in bytecode compiler: " +
                             statement.toString());
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

  case ast::NodeType::BooleanLiteral: {
    const auto &boolean = static_cast<const ast::BooleanLiteral &>(expression);
    emit(OpCode::LOAD_CONST, addConstant(boolean.value));
    break;
  }

  case ast::NodeType::ArrayLiteral: {
    const auto &array = static_cast<const ast::ArrayLiteral &>(expression);
    emit(OpCode::ARRAY_NEW);
    for (const auto &element : array.elements) {
      if (!element) {
        throw std::runtime_error("Array literal contains null element");
      }
      emit(OpCode::DUP);
      compileExpression(*element);
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
      emit(OpCode::OBJECT_SET, pair.first);
    }
    break;
  }

  case ast::NodeType::LambdaExpression: {
    const auto &lambda = static_cast<const ast::LambdaExpression &>(expression);

    // Create bytecode function for lambda
    BytecodeFunction func("<lambda>",
                          static_cast<uint8_t>(lambda.parameters.size()), 0);

    // Enter function context
    enterFunction(std::move(func));

    // Add parameters as locals
    for (size_t i = 0; i < lambda.parameters.size(); ++i) {
      const auto &param = lambda.parameters[i];
      // Declare local by reserving slot
      reserveLocalSlot(next_local_index++);
    }

    // Compile function body
    if (lambda.body) {
      compileStatement(*lambda.body);
      // If body is expression statement, result is already on stack
      // Otherwise, return null
      if (lambda.body->kind != ast::NodeType::ExpressionStatement) {
        emit(OpCode::LOAD_CONST, addConstant(nullptr));
      }
    } else {
      emit(OpCode::LOAD_CONST, addConstant(nullptr));
    }
    emit(OpCode::RETURN);

    // Leave function context
    leaveFunction();

    // Load closure
    const auto &compiled_func = compiled_functions.back();
    uint32_t function_index = 0;
    for (size_t i = 0; i < compiled_functions.size(); ++i) {
      if (&compiled_functions[i] == &compiled_func) {
        function_index = static_cast<uint32_t>(i);
        break;
      }
    }
    emit(OpCode::LOAD_CONST,
         addConstant(FunctionObject{.function_index = function_index}));
    emit(OpCode::CLOSURE, function_index);
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
    compileExpression(*binary.left);
    compileExpression(*binary.right);
    emit(toBytecodeOperator(binary.operator_));
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

  // Check for method call on primitive (prototype method)
  if (expression.callee->kind == ast::NodeType::MemberExpression) {
    const auto &member =
        static_cast<const ast::MemberExpression &>(*expression.callee);
    auto *property =
        dynamic_cast<const ast::Identifier *>(member.property.get());

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
        methodName = typeName + "." + property->symbol;

        // Check if this is a registered prototype method
        if (host_builtin_names_.find(methodName) != host_builtin_names_.end()) {
          // Compile the object (push as first argument)
          compileExpression(*member.object);

          // Compile remaining arguments
          for (const auto &arg : expression.args) {
            if (!arg) {
              throw std::runtime_error(
                  "Call expression contains null argument");
            }
            compileExpression(*arg);
          }

          // Call the prototype method (object is already first arg)
          emit(OpCode::CALL_HOST,
               std::vector<BytecodeValue>{methodName, arg_count + 1});
          return;
        }
      } else {
        // For variables, emit runtime method dispatch
        // First compile the object to get its value
        compileExpression(*member.object);

        // Compile remaining arguments
        for (const auto &arg : expression.args) {
          if (!arg) {
            throw std::runtime_error("Call expression contains null argument");
          }
          compileExpression(*arg);
        }

        // Emit CALL_HOST with method name and arg count
        // The runtime will determine the type and call the appropriate method
        methodName = "any." + property->symbol;
        emit(OpCode::CALL_HOST,
             std::vector<BytecodeValue>{methodName, arg_count + 1});
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
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{binding->name, arg_count});
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
      emit(OpCode::CALL_HOST,
           std::vector<BytecodeValue>{*callee_name, arg_count});
      return;
    }
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
    uint32_t target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(else_jump, target);
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

void ByteCompiler::resetLocals() { next_local_index = 0; }

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
