#include "ByteCompiler.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/errors/ErrorSystem.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

// Non-member fallbacks (0,0 for errors outside method scope)
static uint32_t _compiler_err_line() { return 0; }
static uint32_t _compiler_err_col() { return 0; }

#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw CompilerError(msg, _compiler_err_line(), _compiler_err_col()); \
  } while (0)

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
  case ast::BinaryOperator::IntDiv:
    return OpCode::DIV; // Integer division uses same opcode, VM handles type
  case ast::BinaryOperator::Equal:
    return OpCode::EQ;
  case ast::BinaryOperator::NotEqual:
    return OpCode::NEQ;
  case ast::BinaryOperator::Is:
    return OpCode::IS;
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
  case ast::BinaryOperator::Matches:
  case ast::BinaryOperator::Tilde:
    // Regex matching - will be compiled as host function call
    return OpCode::NOP; // Placeholder - actual implementation in
                        // compileExpression
  case ast::BinaryOperator::Nullish:
    // Nullish coalescing needs special handling - can't use simple OR
    // Will be compiled inline in compileExpression
    return OpCode::NOP; // Placeholder - actual implementation in
                        // compileExpression
  default:
    COMPILER_THROW(
        "Unsupported binary operator in bytecode compiler");
  }
}

bool isIntegerLiteral(double value) {
  return static_cast<double>(static_cast<int64_t>(value)) == value;
}

} // namespace

std::unique_ptr<BytecodeChunk>
ByteCompiler::compile(const ast::Program &program) {
  if (collect_errors_) {
    try {
      return compileImpl(program);
    } catch (const CompilerError& e) {
      errors_.push_back({e.what(), e.line, e.column});
      has_error_ = true;
      return nullptr;
    } catch (const std::exception& e) {
      errors_.push_back({e.what(), 0, 0});
      has_error_ = true;
      return nullptr;
    }
  }
  return compileImpl(program);
}

std::unique_ptr<BytecodeChunk>
ByteCompiler::compileImpl(const ast::Program &program) {
  chunk = std::make_unique<BytecodeChunk>();
  compiled_functions.clear();
  function_indices_by_node_.clear();
  class_method_indices_by_node_.clear();
  lambda_indices_by_node_.clear();
  top_level_function_indices_by_name_.clear();
  top_level_struct_names_.clear();
  top_level_class_names_.clear();
  errors_.clear();
  has_error_ = false;

  for (size_t i = 0; i < program.body.size(); i++) {
  }

  LexicalResolver resolver;
  resolver.setKnownGlobals(known_globals_);
  lexical_resolution_ = resolver.resolve(program);
  if (!resolver.errors().empty()) {
    // Collect all errors
    std::ostringstream oss;
    oss << "Lexical resolution failed with " << resolver.errors().size()
        << " error(s):\n";
    for (const auto &err : resolver.errors()) {
      oss << "  - " << err << "\n";
    }
    COMPILER_THROW(oss.str());
  }

  // Reserve function indices so forward references and recursion emit stable
  // function objects.
  std::vector<const ast::FunctionDeclaration *> declared_functions;
  std::vector<const ast::LambdaExpression *> declared_lambdas;
  declared_functions.reserve(program.body.size());

  uint32_t next_function_index = 0;
  for (size_t i = 0; i < program.body.size(); i++) {
    const auto &statement = program.body[i];
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
      if (statement && statement->kind == ast::NodeType::StructDeclaration) {
        const auto &decl =
            static_cast<const ast::StructDeclaration &>(*statement);
        top_level_struct_names_.insert(decl.name);
      }
      if (statement && statement->kind == ast::NodeType::ClassDeclaration) {
        const auto &decl =
            static_cast<const ast::ClassDeclaration &>(*statement);
        top_level_class_names_.insert(decl.name);
        for (const auto &method : decl.definition.methods) {
          if (!method) {
            continue;
          }
          class_method_indices_by_node_[method.get()] = next_function_index++;
        }
      }
      continue;
    }
    const auto &decl =
        static_cast<const ast::FunctionDeclaration &>(*statement);
    if (!decl.name) {
      COMPILER_THROW("Function declaration missing name");
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

  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::ClassDeclaration) {
      continue;
    }
    const auto &class_decl =
        static_cast<const ast::ClassDeclaration &>(*statement);
    for (const auto &method : class_decl.definition.methods) {
      if (!method) {
        continue;
      }
      compileClassMethod(class_decl.name, *method, class_decl.definition.fields,
                         class_decl.parentName);
    }
  }

  for (const auto *lambda : declared_lambdas) {
    if (!lambda) {
      continue;
    }
    compileLambda(*lambda);
  }

  // Compute max local slot from resolver's declaration_slots
  uint32_t max_slot = 0;
  for (const auto &[node, slot] : lexical_resolution_.declaration_slots) {
    if (slot >= max_slot) {
      max_slot = slot + 1;
    }
  }

  enterFunction(BytecodeFunction("__main__", 0, max_slot), main_function_index);
  // Ensure next_local_index accounts for resolver-allocated slots
  next_local_index = max_slot;

  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }

    if (statement->kind == ast::NodeType::FunctionDeclaration) {
      // For top-level fn declarations, store function object in globals
      const auto &functionDecl =
          static_cast<const ast::FunctionDeclaration &>(*statement);
      if (!functionDecl.name) {
        continue;
      }

      auto index_it = function_indices_by_node_.find(&functionDecl);
      if (index_it == function_indices_by_node_.end()) {
        continue;
      }

      // Load function object onto stack
      auto upvalues_it =
          lexical_resolution_.function_upvalues.find(&functionDecl);
      if (upvalues_it != lexical_resolution_.function_upvalues.end() &&
          !upvalues_it->second.empty()) {
        emit(OpCode::CLOSURE, index_it->second);
      } else {
        emit(OpCode::LOAD_CONST,
             addConstant(Value::makeFunctionObjId(index_it->second)));
      }

      // Store in global scope so it's callable
      uint32_t fnNameStrId = addStringConstant(functionDecl.name->symbol);
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(fnNameStrId));
      continue;
    }

    compileStatement(*statement);
  }

  emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
  emit(OpCode::RETURN);
  leaveFunction();

  for (auto &function : compiled_functions) {
    if (!function) {
      COMPILER_THROW("Missing compiled function for reserved slot");
    }
    chunk->addFunction(std::move(*function));
  }

  return std::move(chunk);
}

void ByteCompiler::emit(OpCode op) { emit(op, std::vector<Value>{}); }

void ByteCompiler::emit(OpCode op, Value operand) {
  emit(op, std::vector<Value>{std::move(operand)});
}

void ByteCompiler::emit(OpCode op, std::vector<Value> operands) {
  if (!current_function) {
    COMPILER_THROW(
        "Attempted to emit bytecode without active function");
  }
  current_function->instructions.emplace_back(op, std::move(operands));
  current_function->instruction_locations.push_back(
      current_source_location_.value_or(SourceLocation{}));
}

uint32_t ByteCompiler::addConstant(const Value &value) {
  if (!current_function) {
    COMPILER_THROW(
        "Attempted to add constant without active function");
  }

  current_function->constants.push_back(value);
  return static_cast<uint32_t>(current_function->constants.size() - 1);
}

uint32_t ByteCompiler::addStringConstant(const std::string &str) {
  if (!chunk) {
    COMPILER_THROW("Attempted to add string constant without active chunk");
  }
  return chunk->addString(str);
}

uint32_t ByteCompiler::emitJump(OpCode op) {
  if (op != OpCode::JUMP && op != OpCode::JUMP_IF_FALSE &&
      op != OpCode::JUMP_IF_TRUE && op != OpCode::JUMP_IF_NULL) {
    COMPILER_THROW("Invalid jump opcode");
  }

  if (!current_function) {
    COMPILER_THROW("Attempted to emit jump without active function");
  }

  uint32_t index = static_cast<uint32_t>(current_function->instructions.size());
  emit(op, static_cast<uint32_t>(0));
  return index;
}

void ByteCompiler::patchJump(uint32_t jump_instruction_index, uint32_t target) {
  if (!current_function) {
    COMPILER_THROW("Attempted to patch jump without active function");
  }

  if (jump_instruction_index >= current_function->instructions.size()) {
    COMPILER_THROW("Invalid jump patch index");
  }

  auto &instruction = current_function->instructions[jump_instruction_index];
  if (instruction.operands.empty()) {
    COMPILER_THROW("Jump instruction missing operand");
  }

  instruction.operands[0] = target;
}

// Helper to extract parameter name from a FunctionParameter AST node
static std::string extractParamName(const ast::FunctionParameter &param) {
  if (param.pattern && param.pattern->kind == ast::NodeType::Identifier) {
    const auto *id = static_cast<const ast::Identifier *>(param.pattern.get());
    return id->symbol;
  }
  return "_";
}

void ByteCompiler::compileFunction(const ast::FunctionDeclaration &function) {
  if (!function.name) {
    COMPILER_THROW("Function declaration missing name");
  }

  auto index_it = function_indices_by_node_.find(&function);
  if (index_it == function_indices_by_node_.end()) {
    COMPILER_THROW("Missing function index for declaration: " +
                             function.name->symbol);
  }

  // Compute max local slot from resolver's declaration_slots for this function
  // We need to find all declarations that belong to this function
  uint32_t max_slot = static_cast<uint32_t>(function.parameters.size());
  for (const auto &[node, slot] : lexical_resolution_.declaration_slots) {
    if (slot >= max_slot) {
      max_slot = slot + 1;
    }
  }

  // Collect parameter names for metadata
  std::vector<std::string> param_names;
  param_names.reserve(function.parameters.size());
  for (const auto &param : function.parameters) {
    if (param) {
      param_names.push_back(extractParamName(*param));
    } else {
      param_names.push_back("_");
    }
  }

  // Create function with metadata
  BytecodeFunction bf(function.name->symbol,
                       static_cast<uint32_t>(function.parameters.size()), max_slot);
  bf.param_names = std::move(param_names);
  bf.source_line = function.line;
  // source_file will be set by the pipeline/compiler context

  enterFunction(std::move(bf), index_it->second);
  next_local_index = max_slot;
  auto upvalues_it = lexical_resolution_.function_upvalues.find(&function);
  if (upvalues_it != lexical_resolution_.function_upvalues.end()) {
    current_function->upvalues = upvalues_it->second;
  }

  // Collect default parameter values and variadic info
  for (size_t i = 0; i < function.parameters.size(); i++) {
    const auto &param = function.parameters[i];
    if (!param || !param->pattern) {
      COMPILER_THROW("Function parameter missing pattern");
    }
    collectParameterPatternSlots(*param->pattern);

    // Track variadic parameter
    if (param->isVariadic) {
      current_function->variadic_param_index = static_cast<uint32_t>(i);
    }

    if (param->typeAnnotation && param->pattern &&
        param->pattern->kind == ast::NodeType::Identifier) {
      const auto *identifier =
          static_cast<const ast::Identifier *>(param->pattern.get());
      if (auto normalized =
              normalizeTypeAnnotation(param->typeAnnotation->get());
          normalized.has_value()) {
        emitTypeAssertionForLocal(*normalized, declarationSlot(*identifier),
                                  identifier->symbol);
      }
    }

    // Store default value if present (only simple literals for now)
    if (param->defaultValue.has_value()) {
      const auto &defaultExpr = param->defaultValue.value();
      if (defaultExpr->kind == ast::NodeType::NumberLiteral) {
        const auto &num = static_cast<const ast::NumberLiteral &>(*defaultExpr);
        if (isIntegerLiteral(num.value)) {
          current_function->default_values.push_back(
              Value::makeInt(static_cast<int64_t>(num.value)));
        } else {
          current_function->default_values.push_back(
              Value::makeDouble(num.value));
        }
      } else if (defaultExpr->kind == ast::NodeType::StringLiteral) {
        const auto &str = static_cast<const ast::StringLiteral &>(*defaultExpr);
        current_function->default_values.push_back(
            Value::makeNull()); // TODO: string default
      } else if (defaultExpr->kind == ast::NodeType::BooleanLiteral) {
        const auto &boolean =
            static_cast<const ast::BooleanLiteral &>(*defaultExpr);
        current_function->default_values.push_back(
            Value::makeBool(boolean.value));
      } else if (defaultExpr->kind == ast::NodeType::ArrayLiteral) {
        const auto &arr =
            static_cast<const ast::ArrayLiteral &>(*defaultExpr);
        if (arr.elements.empty()) {
          // Empty array default: use boolean true as sentinel.
          // The VM recognizes makeBool(true) as "allocate fresh empty array".
          current_function->default_values.push_back(
              Value::makeBool(true));
        } else {
          // Non-empty array defaults not yet supported as defaults
          current_function->default_values.push_back(std::nullopt);
        }
      } else {
        current_function->default_values.push_back(std::nullopt);
      }
    } else {
      current_function->default_values.push_back(std::nullopt);
    }
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

      // Last statement: if it's an expression statement, return its value
      // (Rust-like implicit return)
      const auto &lastStmt = stmts.back();
      if (lastStmt && lastStmt->kind == ast::NodeType::ExpressionStatement) {
        const auto &exprStmt =
            static_cast<const ast::ExpressionStatement &>(*lastStmt);
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
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          emit(OpCode::RETURN);
        }
      } else if (lastStmt && lastStmt->kind == ast::NodeType::ReturnStatement) {
        // Last statement is an explicit return - compile with tail position
        enterTailPosition();
        compileStatement(*lastStmt);
        exitTailPosition();
      } else if (lastStmt) {
        // Last statement is not an expression or return - compile in tail
        // position and add implicit return
        enterTailPosition();
        compileStatement(*lastStmt);
        exitTailPosition();
        // Only emit RETURN if the statement didn't already return (via tail
        // call branches)
        if (!wasTailCall()) {
          emit(OpCode::RETURN);
        }
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        emit(OpCode::RETURN);
      }
    } else {
      // Empty function body
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      emit(OpCode::RETURN);
    }
  } else {
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    emit(OpCode::RETURN);
  }
  
  // Phase 3B-3: Detect if this function contains yield expressions
  if (function.body) {
    current_function->is_generator = functionContainsYield(*function.body);
  }
  
  leaveFunction();
}

void ByteCompiler::compileLambda(const ast::LambdaExpression &lambda) {
  auto index_it = lambda_indices_by_node_.find(&lambda);
  if (index_it == lambda_indices_by_node_.end()) {
    COMPILER_THROW("Missing function index for lambda expression");
  }

  // Collect parameter names for lambda
  std::vector<std::string> param_names;
  param_names.reserve(lambda.parameters.size());
  for (const auto &param : lambda.parameters) {
    if (param && param->pattern) {
      param_names.push_back(extractParamName(*param));
    } else {
      param_names.push_back("_");
    }
  }

  BytecodeFunction bf("<lambda>",
                       static_cast<uint32_t>(lambda.parameters.size()), 0);
  bf.param_names = std::move(param_names);
  bf.source_line = lambda.line;

  enterFunction(std::move(bf), index_it->second);

  auto upvalues_it = lexical_resolution_.lambda_upvalues.find(&lambda);
  if (upvalues_it != lexical_resolution_.lambda_upvalues.end()) {
    current_function->upvalues = upvalues_it->second;
  }

  // Collect default parameter values and variadic info for lambda
  for (size_t i = 0; i < lambda.parameters.size(); i++) {
    const auto &param = lambda.parameters[i];
    if (!param || !param->pattern) {
      COMPILER_THROW("Lambda parameter missing pattern");
    }
    compileParameterPattern(*param->pattern, static_cast<uint32_t>(i));

    // Track variadic parameter
    if (param->isVariadic) {
      current_function->variadic_param_index = static_cast<uint32_t>(i);
    }

    if (param->typeAnnotation && param->pattern &&
        param->pattern->kind == ast::NodeType::Identifier) {
      const auto *identifier =
          static_cast<const ast::Identifier *>(param->pattern.get());
      if (auto normalized =
              normalizeTypeAnnotation(param->typeAnnotation->get());
          normalized.has_value()) {
        emitTypeAssertionForLocal(*normalized, declarationSlot(*identifier),
                                  identifier->symbol);
      }
    }

    // Store default value if present
    if (param->defaultValue.has_value()) {
      const auto &defaultExpr = param->defaultValue.value();
      if (defaultExpr->kind == ast::NodeType::NumberLiteral) {
        const auto &num = static_cast<const ast::NumberLiteral &>(*defaultExpr);
        if (isIntegerLiteral(num.value)) {
          current_function->default_values.push_back(
              Value::makeInt(static_cast<int64_t>(num.value)));
        } else {
          current_function->default_values.push_back(
              Value::makeDouble(num.value));
        }
      } else if (defaultExpr->kind == ast::NodeType::StringLiteral) {
        const auto &str = static_cast<const ast::StringLiteral &>(*defaultExpr);
        current_function->default_values.push_back(
            Value::makeNull()); // TODO: string default
      } else if (defaultExpr->kind == ast::NodeType::BooleanLiteral) {
        const auto &boolean =
            static_cast<const ast::BooleanLiteral &>(*defaultExpr);
        current_function->default_values.push_back(
            Value::makeBool(boolean.value));
      } else {
        current_function->default_values.push_back(std::nullopt);
      }
    } else {
      current_function->default_values.push_back(std::nullopt);
    }
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
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
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
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    emit(OpCode::RETURN);
  }

  // Phase 3B-3: Detect if this lambda contains yield expressions
  if (lambda.body) {
    // For lambda expressions, we need to check if the body contains yield
    // Note: lambda.body is a Statement, so we use statementContainsYield directly
    current_function->is_generator = statementContainsYield(*lambda.body);
  }

  leaveFunction();
}

void ByteCompiler::compileClassMethod(
    const std::string &class_name, const ast::ClassMethodDef &method,
    const std::vector<ast::ClassFieldDef> &fields,
    const std::string &parent_class_name) {
  auto index_it = class_method_indices_by_node_.find(&method);
  if (index_it == class_method_indices_by_node_.end()) {
    COMPILER_THROW("Missing function index for class method: " + method.name);
  }

  bool is_class_method = method.isClassMethod;
  // For class methods (@@fn), no self param. For instance methods (fn), self at slot 0.
  const uint32_t param_count =
      is_class_method ? static_cast<uint32_t>(method.parameters.size())
                      : static_cast<uint32_t>(method.parameters.size() + 1);

  // Compute max slot
  uint32_t max_slot = param_count;
  if (!is_class_method) {
    for (const auto &[node, slot] : lexical_resolution_.declaration_slots) {
      uint32_t adjusted_slot = slot + 1;
      if (adjusted_slot >= max_slot) {
        max_slot = adjusted_slot + 1;
      }
    }
  } else {
    for (const auto &[node, slot] : lexical_resolution_.declaration_slots) {
      if (slot >= max_slot) {
        max_slot = slot + 1;
      }
    }
  }

  // Collect parameter names
  std::vector<std::string> param_names;
  param_names.reserve(method.parameters.size() + (is_class_method ? 0 : 1));
  if (!is_class_method) param_names.push_back("self");
  for (const auto &param : method.parameters) {
    if (param && param->pattern) {
      param_names.push_back(extractParamName(*param));
    } else {
      param_names.push_back("_");
    }
  }

  BytecodeFunction bf(class_name + "." + method.name, param_count, max_slot);
  bf.param_names = std::move(param_names);
  bf.source_line = method.line;

  enterFunction(std::move(bf), index_it->second);
  next_local_index = max_slot;

  if (!is_class_method) {
    // Instance method: Store self (slot 0) into global "this" for @field access.
    {
      uint32_t this_id = addStringConstant("this");
      emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(this_id));
    }
  }

  // Mirror instance fields into globals for bare field references.
  // For class methods, skip this since there's no self object.
  if (!is_class_method) {
    for (const auto &field : fields) {
      uint32_t field_id = addStringConstant(field.name);
      emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
      emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(field_id)));
      emit(OpCode::OBJECT_GET);
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(field_id));
    }
  }

  // Bind user parameters to slots.
  // For instance methods: slots 1..N (slot 0 is self)
  // For class methods: slots 0..N-1 (no self)
  for (size_t i = 0; i < method.parameters.size(); ++i) {
    const auto &param = method.parameters[i];
    if (!param || !param->pattern) {
      COMPILER_THROW("Class method parameter missing pattern");
    }
    const uint32_t method_param_slot =
        is_class_method ? static_cast<uint32_t>(i) : static_cast<uint32_t>(i + 1);
    compileParameterPattern(*param->pattern, method_param_slot);
    if (param->isVariadic) {
      current_function->variadic_param_index = method_param_slot;
    }
  }

  // Remap declaration slots for local variable access.
  // For instance methods: resolver assigned slots starting at 0, but at
  // runtime slot 0 holds self. We offset all local variable accesses by 1.
  // For class methods: no offset needed (no self).
  local_slot_offset_ = is_class_method ? 0 : 1;

  const std::string prev_class_name = current_class_name_;
  const std::string prev_parent_name = current_parent_class_name_;
  current_class_name_ = class_name;
  current_parent_class_name_ = parent_class_name;

  if (method.body) {
    const auto &stmts = method.body->body;
    if (!stmts.empty()) {
      for (size_t i = 0; i < stmts.size() - 1; i++) {
        if (stmts[i]) {
          compileStatement(*stmts[i]);
        }
      }

      const auto &lastStmt = stmts.back();
      if (lastStmt && lastStmt->kind == ast::NodeType::ExpressionStatement) {
        const auto &exprStmt =
            static_cast<const ast::ExpressionStatement &>(*lastStmt);
        if (exprStmt.expression) {
          enterTailPosition();
          clearTailCallFlag();
          compileExpression(*exprStmt.expression);
          exitTailPosition();
          if (!wasTailCall()) {
            emit(OpCode::RETURN);
          }
        } else {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          emit(OpCode::RETURN);
        }
      } else if (lastStmt && lastStmt->kind == ast::NodeType::ReturnStatement) {
        enterTailPosition();
        compileStatement(*lastStmt);
        exitTailPosition();
      } else if (lastStmt) {
        enterTailPosition();
        compileStatement(*lastStmt);
        exitTailPosition();
        if (!wasTailCall()) {
          emit(OpCode::RETURN);
        }
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        emit(OpCode::RETURN);
      }
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      emit(OpCode::RETURN);
    }
  } else {
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    emit(OpCode::RETURN);
  }

  // Phase 3B-3: Detect if this class method contains yield expressions
  if (method.body) {
    current_function->is_generator = functionContainsYield(*method.body);
  }

  current_class_name_ = prev_class_name;
  current_parent_class_name_ = prev_parent_name;
  local_slot_offset_ = 0;
  leaveFunction();
}

// Compile a parameter pattern - extracts fields from parameter into locals
// paramIndex is the slot where the parameter value is stored
// For patterns, we allocate NEW slots for extracted values (not using
// declarationSlot)
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
      { uint32_t _sid = addStringConstant(key); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
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
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(i))));
      emit(OpCode::ARRAY_GET);

      // Recursively compile the element pattern - result is on stack
      compileParameterPatternValue(*elemPattern);
    }
    break;
  }

  default:
    COMPILER_THROW("Unsupported parameter pattern type");
  }
}

// Compile a parameter pattern value - expects value on stack, stores into local
void ByteCompiler::compileParameterPatternValue(
    const ast::Expression &pattern) {
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
      { uint32_t _sid = addStringConstant(key); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
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
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(i))));
      emit(OpCode::ARRAY_GET);

      compileParameterPatternValue(*elemPattern);
    }
    break;
  }
  default:
    COMPILER_THROW("Unsupported parameter pattern value type");
  }
}

// Collect all declaration slots from a parameter pattern (for function
// declarations)
void ByteCompiler::collectParameterPatternSlots(
    const ast::Expression &pattern) {
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
      // Also don't POP after yield expression - yield value is returned to caller
      bool is_yield = expr_stmt.expression->kind == ast::NodeType::YieldExpression;
      if (!in_tail_position_ && !is_yield) {
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
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      const auto *decl_binding = bindingFor(*identifier);
      // Top-level `let` is emitted as global storage; nested `let` stays local.
      if (decl_binding && decl_binding->kind == ResolvedBindingKind::Global) {
        uint32_t slot = declarationSlot(*identifier);
        reserveLocalSlot(slot);
        // Keep both global and slot state in sync so root-block references
        // resolved as locals still observe initialized values.
        emit(OpCode::DUP);
        emit(OpCode::STORE_GLOBAL,
             std::vector<Value>{Value::makeStringValId(addStringConstant(identifier->symbol))});
        emit(OpCode::STORE_VAR, slot);
        if (let.typeAnnotation) {
          if (auto normalized =
                  normalizeTypeAnnotation(let.typeAnnotation->get());
              normalized.has_value()) {
            emitTypeAssertionForLocal(*normalized, slot, identifier->symbol);
          }
        }
      } else {
        uint32_t slot = declarationSlot(*identifier);
        reserveLocalSlot(slot);
        emit(OpCode::STORE_VAR, slot);
        if (let.typeAnnotation) {
          if (auto normalized =
                  normalizeTypeAnnotation(let.typeAnnotation->get());
              normalized.has_value()) {
            emitTypeAssertionForLocal(*normalized, slot, identifier->symbol);
          }
        }
      }
      break;
    }

    if (let.pattern && (let.pattern->kind == ast::NodeType::ListPattern ||
                        let.pattern->kind == ast::NodeType::ArrayPattern)) {
      const auto &pattern =
          static_cast<const ast::ArrayPattern &>(*let.pattern);
      if (!let.value) {
        COMPILER_THROW("Tuple/array destructuring requires a value");
      }

      bool optimized_tuple_literal =
          let.value->kind == ast::NodeType::TupleExpression;
      const ast::TupleExpression *tuple_value =
          optimized_tuple_literal
              ? static_cast<const ast::TupleExpression *>(let.value.get())
              : nullptr;

      uint32_t temp_slot = next_local_index;
      if (!optimized_tuple_literal) {
        reserveLocalSlot(temp_slot);
        compileExpression(*let.value);
        emit(OpCode::STORE_VAR, temp_slot);
      }

      for (size_t i = 0; i < pattern.elements.size(); ++i) {
        const auto *element_id = pattern.elements[i]
                                     ? dynamic_cast<const ast::Identifier *>(
                                           pattern.elements[i].get())
                                     : nullptr;
        if (!element_id) {
          COMPILER_THROW("Tuple destructuring currently supports "
                                   "identifier elements only");
        }
        const uint32_t slot = declarationSlot(*element_id);
        reserveLocalSlot(slot);
        if (optimized_tuple_literal) {
          if (tuple_value && i < tuple_value->elements.size() &&
              tuple_value->elements[i]) {
            compileExpression(*tuple_value->elements[i]);
          } else {
            emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          }
        } else {
          emit(OpCode::LOAD_VAR, temp_slot);
          emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(i))));
          emit(OpCode::ARRAY_GET);
        }
        emit(OpCode::STORE_VAR, slot);
      }
      break;
    }

    if (let.pattern && let.pattern->kind == ast::NodeType::ObjectPattern) {
      const auto &pattern =
          static_cast<const ast::ObjectPattern &>(*let.pattern);
      if (!let.value) {
        COMPILER_THROW("Object destructuring requires a value");
      }

      // Store the object in a temp slot
      uint32_t temp_slot = next_local_index;
      reserveLocalSlot(temp_slot);
      compileExpression(*let.value);
      emit(OpCode::STORE_VAR, temp_slot);

      // Helper: create a constant that loads a string value
      auto loadStringConst = [this](const std::string &str) {
        uint32_t strId = addStringConstant(str);
        return addConstant(Value::makeStringValId(strId));
      };

      // Extract each field: obj.key -> STORE_VAR/STORE_GLOBAL alias
      for (const auto &prop : pattern.properties) {
        const std::string &key = prop.first;
        const auto *alias = dynamic_cast<const ast::Identifier *>(prop.second.get());
        if (!alias) {
          COMPILER_THROW("Object destructuring supports identifier aliases only");
        }

        // Check if this is a global variable (top-level let)
        if (lexical_resolution_.global_variables.count(alias->symbol) > 0) {
          emit(OpCode::LOAD_VAR, temp_slot);
          emit(OpCode::LOAD_CONST, loadStringConst(key));
          emit(OpCode::OBJECT_GET);
          emit(OpCode::STORE_GLOBAL,
               std::vector<Value>{Value::makeStringValId(addStringConstant(alias->symbol))});
        } else {
          const uint32_t slot = declarationSlot(*alias);
          reserveLocalSlot(slot);
          emit(OpCode::LOAD_VAR, temp_slot);
          emit(OpCode::LOAD_CONST, loadStringConst(key));
          emit(OpCode::OBJECT_GET);
          emit(OpCode::STORE_VAR, slot);
        }
      }
      break;
    }

    COMPILER_THROW(
        "Bytecode compiler supports let patterns: identifier, tuple/array, and object");
    break;
  }

  case ast::NodeType::ReturnStatement: {
    const auto &ret = static_cast<const ast::ReturnStatement &>(statement);
    if (ret.argument) {
      compileExpression(*ret.argument);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
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

  case ast::NodeType::DoWhileStatement:
    compileDoWhileStatement(
        static_cast<const ast::DoWhileStatement &>(statement));
    break;

  case ast::NodeType::ForStatement:
    compileForStatement(static_cast<const ast::ForStatement &>(statement));
    break;

  case ast::NodeType::LoopStatement:
    compileLoopStatement(static_cast<const ast::LoopStatement &>(statement));
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

  case ast::NodeType::WaitStatement:
    compileWaitStatement(static_cast<const ast::WaitStatement &>(statement));
    break;

  case ast::NodeType::BreakStatement:
    emit(OpCode::JUMP, 0); // Will be patched later
    break;

  case ast::NodeType::ContinueStatement:
    emit(OpCode::JUMP, 0); // Will be patched later
    break;

  case ast::NodeType::SleepStatement: {
    const auto &sleep = static_cast<const ast::SleepStatement &>(statement);
    // Prefer numeric constants for raw-number sleeps (e.g. :500).
    bool is_numeric = !sleep.duration.empty();
    bool seen_dot = false;
    for (char ch : sleep.duration) {
      if (ch == '.') {
        if (seen_dot) {
          is_numeric = false;
          break;
        }
        seen_dot = true;
        continue;
      }
      if (ch < '0' || ch > '9') {
        is_numeric = false;
        break;
      }
    }

    if (is_numeric) {
      if (seen_dot) {
        emit(OpCode::LOAD_CONST,
             addConstant(Value::makeDouble(std::stod(sleep.duration))));
      } else {
        emit(OpCode::LOAD_CONST,
             addConstant(Value::makeInt(std::stoll(sleep.duration))));
      }
    } else {
      // Fallback: duration string (e.g. "500ms", "1s", "2.5m", "1h").
      uint32_t strId = addStringConstant(sleep.duration);
      emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));
    }
    // Call sleep() with the duration string
    {
      uint32_t fnId = addStringConstant("sleep");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(fnId),
          Value(static_cast<uint32_t>(1))});
    }
    emit(OpCode::POP); // Discard result
    break;
  }

  case ast::NodeType::FunctionDeclaration:
    // Function declarations evaluate to function objects stored in local slots.
    // Top-level declarations are skipped in __main__ and do not hit this path.
    {
      const auto &function =
          static_cast<const ast::FunctionDeclaration &>(statement);
      if (!function.name) {
        COMPILER_THROW("Function declaration missing name");
      }

      auto index_it = function_indices_by_node_.find(&function);
      if (index_it == function_indices_by_node_.end()) {
        COMPILER_THROW("Missing function index for declaration: " +
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
             addConstant(Value::makeFunctionObjId(index_it->second)));
      }
      emit(OpCode::STORE_VAR, slot);
    }
    break;

  case ast::NodeType::TryExpression:
    compileTryStatement(static_cast<const ast::TryExpression &>(statement));
    break;

  case ast::NodeType::ConfigBlock: {
    // Config block: config { key = value; ... }
    // Compile to: conf.key = value; config.set("key", value);
    const auto &configBlock = static_cast<const ast::ConfigBlock &>(statement);

    for (const auto &pair : configBlock.pairs) {
      const std::string &key = pair.first;
      const auto &valueExpr = pair.second;

      if (!valueExpr) {
        COMPILER_THROW("Config block pair has null value for key: " +
                                 key);
      }

      // Compile value expression
      compileExpression(*valueExpr);

      // Set conf.key = value (conf object is global)
      {
        uint32_t confStrId = addStringConstant("conf");
        emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(confStrId));
      }
      { uint32_t _sid = addStringConstant(key); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      emit(OpCode::OBJECT_SET);

      // Call config.set(key, value) to save to file
      { uint32_t _sid = addStringConstant(key); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      compileExpression(*valueExpr); // Re-compile value for config.set
      {
        uint32_t strId = addStringConstant("config.set");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(2))});
      }
      emit(OpCode::POP); // Discard result
    }
    break;
  }

  case ast::NodeType::WhenBlockStatement:
    compileWhenBlock(static_cast<const ast::WhenBlock &>(statement));
    break;

  case ast::NodeType::ModeBlock: {
    // Simple mode block: mode name { statements }
    // Compile as: when mode == "name" { statements }
    const auto &modeBlock = static_cast<const ast::ModeBlock &>(statement);

    // Compile condition: mode == "modeName"
    // Call mode() host function to get current mode
    { uint32_t _sid = addStringConstant("mode"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    {
      uint32_t modeStrId = addStringConstant("mode");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(modeStrId),
          Value(static_cast<uint32_t>(0))});
    }

    // Load the mode name to compare against
    { uint32_t _sid = addStringConstant(modeBlock.modeName); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };

    // Compare: mode() == "modeName"
    emit(OpCode::EQ);

    // Store condition result
    uint32_t condSlot = next_local_index++;
    reserveLocalSlot(condSlot);
    emit(OpCode::STORE_VAR, condSlot);

    // Jump to end if condition is false
    uint32_t endJump = emitJump(OpCode::JUMP_IF_FALSE);

    // Compile statements in the mode block
    for (const auto &stmt : modeBlock.statements) {
      if (stmt) {
        compileStatement(*stmt);
      }
    }

    // Patch the jump
    patchJump(endJump,
              static_cast<uint32_t>(current_function->instructions.size()));
    break;
  }

  case ast::NodeType::ModesBlock: {
    // Full mode definition: mode name [priority N] { condition = ...; enter {
    // ... }; exit { ... } }
    const auto &modesBlock = static_cast<const ast::ModesBlock &>(statement);

    for (const auto &modeDef : modesBlock.modes) {
      // Register mode with ModeManager at runtime
      // mode.register(name, priority, condition, enter, exit, onEnterFrom,
      // onExitTo, ...)

      // Load mode name
      { uint32_t _sid = addStringConstant(modeDef.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };

      // Load priority
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeInt(static_cast<int64_t>(modeDef.priority))));

      // Compile condition expression (or null if not provided)
      if (modeDef.condition) {
        compileExpression(*modeDef.condition);
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      // Create closures for enter/exit blocks
      // For now, we'll compile them inline and create closures
      // This is a simplified implementation - full implementation needs nested
      // functions

      // Compile enter block as inline code wrapped in closure
      if (modeDef.enterBlock) {
        // Create a closure for enter block
        // For simplicity, we'll emit a placeholder
        emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(0)));
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      // Compile exit block
      if (modeDef.exitBlock) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(0)));
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      // Load onEnterFrom mode name (or null)
      if (!modeDef.onEnterFrom.empty()) {
        { uint32_t _sid = addStringConstant(modeDef.onEnterFrom); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      // Load onExitTo mode name (or null)
      if (!modeDef.onExitTo.empty()) {
        { uint32_t _sid = addStringConstant(modeDef.onExitTo); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      // Load preventRetrigger flag (commented out - field doesn't exist)
      // emit(OpCode::LOAD_CONST, addConstant(modeDef.preventRetrigger));
      emit(OpCode::LOAD_CONST, Value::makeBool(false));

      // Call mode.register
      {
        uint32_t strId = addStringConstant("mode.register");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(8))});
      }
      emit(OpCode::POP); // Discard result
    }
    break;
  }

  case ast::NodeType::ThrowStatement: {
    const auto &throw_stmt =
        static_cast<const ast::ThrowStatement &>(statement);
    if (throw_stmt.value) {
      compileExpression(*throw_stmt.value);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    }
    emit(OpCode::THROW);
    break;
  }

  case ast::NodeType::DelStatement: {
    const auto &del_stmt =
        static_cast<const ast::DelStatement &>(statement);
    if (!del_stmt.target) break;
    compileDelTarget(*del_stmt.target);
    break;
  }

  case ast::NodeType::GoStatement: {
    compileGoStatement(static_cast<const ast::GoStatement &>(statement));
    break;
  }

  case ast::NodeType::OnMessageStatement: {
    // on msg { ... }
    // This is a message handler that binds the message to a variable
    // The actual message is received at runtime when a thread receives a message
    // For now, we compile the body as-is
    const auto &onMsg = static_cast<const ast::OnMessageStatement &>(statement);
    if (onMsg.body) {
      compileStatement(*onMsg.body);
    }
    break;
  }

  // Type system declarations - register types at compile time
  case ast::NodeType::StructDeclaration: {
    const auto &structDecl =
        static_cast<const ast::StructDeclaration &>(statement);
    // Runtime registration: struct.define("Name", ["field1", ...])
    { uint32_t _sid = addStringConstant(structDecl.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::ARRAY_NEW);
    for (const auto &field : structDecl.definition.fields) {
      { uint32_t _sid = addStringConstant(field.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      emit(OpCode::ARRAY_PUSH);
    }
    {
      uint32_t strId = addStringConstant("struct.define");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(static_cast<uint32_t>(2))});
    }
    // Store the type_id in a global variable so constructor calls work
    {
      uint32_t strId = addStringConstant(structDecl.name);
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
    }
    break;
  }

  case ast::NodeType::ClassDeclaration: {
    const auto &classDecl =
        static_cast<const ast::ClassDeclaration &>(statement);
    // Runtime registration: class.define("Name", ["field1", ...], parent, ["@@class_field1", ...])
    { uint32_t _sid = addStringConstant(classDecl.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::ARRAY_NEW);
    // Instance fields (@field)
    for (const auto &field : classDecl.definition.fields) {
      if (!field.isClassField) {
        { uint32_t _sid = addStringConstant(field.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::ARRAY_PUSH);
      }
    }
    // Parent class (null if no parent)
    if (!classDecl.parentName.empty()) {
      uint32_t parent_sid = addStringConstant(classDecl.parentName);
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(parent_sid));
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    }
    // Class fields (@@field) as last argument
    emit(OpCode::ARRAY_NEW);
    for (const auto &field : classDecl.definition.fields) {
      if (field.isClassField) {
        { uint32_t _sid = addStringConstant(field.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::ARRAY_PUSH);
      }
    }
    // Arity: name(1) + instance_fields(1) + parent(1) + class_fields(1) = 4
    uint32_t define_arity = 4;
    {
      uint32_t strId = addStringConstant("class.define");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(define_arity)});
    }
    // Store the type_id in a global variable so constructor calls work
    {
      uint32_t strId = addStringConstant(classDecl.name);
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
    }

    // Initialize class fields (@@field = value) on the class prototype
    for (const auto &field : classDecl.definition.fields) {
      if (field.isClassField && field.defaultValue.has_value()) {
        // Load class object
        { uint32_t _sid = addStringConstant(classDecl.name); emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(_sid)); };
        // Compile default value
        compileExpression(*field.defaultValue.value());
        // Store to class field: OBJECT_SET expects [obj, value, key]
        { uint32_t _sid = addStringConstant(field.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::OBJECT_SET);
        emit(OpCode::POP);  // discard the returned object
      }
    }

    // Register compiled methods on the class type.
    for (const auto &method : classDecl.definition.methods) {
      if (!method) {
        continue;
      }
      auto method_index_it = class_method_indices_by_node_.find(method.get());
      if (method_index_it == class_method_indices_by_node_.end()) {
        COMPILER_THROW("Missing class method index: " + classDecl.name + "." +
                       method->name);
      }
      uint32_t class_name_sid = addStringConstant(classDecl.name);
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(class_name_sid));
      uint32_t method_name_sid = addStringConstant(method->name);
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeStringValId(method_name_sid)));
      emit(OpCode::LOAD_CONST, addConstant(Value::makeFunctionObjId(
                              method_index_it->second)));
      uint32_t register_sid = addStringConstant("class.method");
      emit(OpCode::CALL_HOST,
           std::vector<Value>{Value::makeStringValId(register_sid),
                              Value::makeInt(3)});
      emit(OpCode::POP);
    }
    break;
  }

  case ast::NodeType::EnumDeclaration: {
    const auto &enumDecl = static_cast<const ast::EnumDeclaration &>(statement);
    // Register enum type with its variants
    std::vector<std::string> variantNames;
    for (const auto &variant : enumDecl.definition.variants) {
      variantNames.push_back(variant.name);
    }
    break;
  }

  case ast::NodeType::TraitDeclaration: {
    const auto &traitDecl =
        static_cast<const ast::TraitDeclaration &>(statement);
    break;
  }

  case ast::NodeType::ImplDeclaration: {
    const auto &implDecl = static_cast<const ast::ImplDeclaration &>(statement);
    break;
  }

  case ast::NodeType::UseStatement: {
    compileUseStatement(static_cast<const ast::UseStatement &>(statement));
    break;
  }

  case ast::NodeType::ExportStatement: {
    compileExportStatement(
        static_cast<const ast::ExportStatement &>(statement));
    break;
  }

  case ast::NodeType::ShellCommandStatement: {
    compileShellCommandStatement(
        static_cast<const ast::ShellCommandStatement &>(statement));
    break;
  }

  case ast::NodeType::ConditionalHotkey: {
    // Conditional hotkey: hotkey if condition => { ... }
    const auto &condHk = static_cast<const ast::ConditionalHotkey &>(statement);
    if (condHk.binding) {
      compileHotkeyBinding(*condHk.binding);
    }
    break;
  }

  default:
    COMPILER_THROW("Unsupported statement in bytecode compiler: " +
                             statement.toString());
  }
}

void ByteCompiler::compileTryStatement(const ast::TryExpression &statement) {
  if (!statement.tryBody) {
    COMPILER_THROW("try statement missing body");
  }

  const uint32_t try_enter_index =
      static_cast<uint32_t>(current_function->instructions.size());
  // Emit TRY_ENTER with placeholder operands (catch_ip and finally_ip patched
  // later)
  emit(OpCode::TRY_ENTER,
       std::vector<Value>{
           static_cast<uint32_t>(0), // catch_ip - patched later
           static_cast<uint32_t>(
               0) // finally_ip - patched later (0 if no finally)
       });

  compileStatement(*statement.tryBody);
  emit(OpCode::TRY_EXIT);

  // Finally block location (executed after try body on normal exit)
  uint32_t finally_ip = 0;
  if (statement.finallyBlock) {
    finally_ip = static_cast<uint32_t>(current_function->instructions.size());
    compileStatement(*statement.finallyBlock);
    // After finally on normal exit, jump to end
  }

  const uint32_t jump_after_try = emitJump(OpCode::JUMP);

  // Catch block location
  const uint32_t catch_ip =
      static_cast<uint32_t>(current_function->instructions.size());

  // Patch TRY_ENTER with catch_ip and finally_ip
  if (try_enter_index >= current_function->instructions.size()) {
    COMPILER_THROW("Invalid TRY_ENTER patch index");
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

std::unique_ptr<BytecodeChunk>
ByteCompiler::compileWithModuleLoader(const ast::Program &program,
                                      ModuleLoader &loader,
                                      const std::filesystem::path &basePath) {
  module_loader_ = &loader;
  base_path_ = basePath;
  return compile(program);
}

void ByteCompiler::compileUseStatement(const ast::UseStatement &statement) {
  // Handle host modules first (lazy loading via HostBridge)
  if (host_bridge_ && !statement.isFileImport) {
    for (const auto &moduleName : statement.moduleNames) {
      if (host_bridge_->isModuleAvailable(moduleName)) {
        if (!host_bridge_->loadModule(moduleName)) {
          COMPILER_THROW("Failed to load host module: " + moduleName);
        }

        // If wildcard 'use module.*', flatten exports into global scope
        if (statement.isWildcard) {
          // This requires VM support to copy exported names from module namespace
          // to current scope. For now, we emit a marker.
          { uint32_t _sid = addStringConstant("Flattening host module: " + moduleName); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
          emit(OpCode::POP);
        } else {
          // Regular 'use module' makes the module object available by its name
          { uint32_t _sid = addStringConstant("Host module loaded: " + moduleName); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
          emit(OpCode::POP);
        }
        return;
      }
    }
  }

  // Fall back to file-based module loading
  if (!module_loader_) {
    COMPILER_THROW("Module loader not available for use statement");
  }
  // Load the module (script file)
  if (statement.isFileImport) {
    // Emit the IMPORT opcode with the file path as a constant
    uint32_t path_sid = addStringConstant(statement.filePath);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(path_sid)));
    emit(OpCode::IMPORT);
    // Result is the module object (or null on error/direct run success)
    emit(OpCode::POP);
    return;
  }
}

void ByteCompiler::compileExportStatement(
    const ast::ExportStatement &statement) {
  // Export statements are handled during semantic analysis
  // They mark which declarations should be exported from the module
  // For now, this is a no-op at the bytecode level
  // In a full implementation, we would track exports for the module loader

  if (statement.exported) {
    // The exported value is already compiled as part of the normal compilation
    // We just mark it as exported in the module's export table
  }
}

// Compile a match pattern, emitting code that leaves a boolean on the stack
// indicating whether the pattern matches the value currently on top of stack.
// For simple patterns, this uses EQ. For complex patterns, it emits specialized code.
void ByteCompiler::compilePattern(const ast::Expression &pattern, uint32_t discSlot) {
  switch (pattern.kind) {
  case ast::NodeType::OrPattern: {
    const auto &orPat = static_cast<const ast::OrPattern &>(pattern);
    // OrPattern: alt1.matches(disc) || alt2.matches(disc) || ...
    // Compile first alternative
    compilePattern(*orPat.alternatives[0], discSlot);
    
    for (size_t i = 1; i < orPat.alternatives.size(); i++) {
      compilePattern(*orPat.alternatives[i], discSlot);
      emit(OpCode::OR);
    }
    break;
  }
  
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    // ArrayPattern: check length AND bind element variables
    // Start with true
    emit(OpCode::LOAD_CONST, addConstant(Value::makeBool(true)));
    
    // Check length first
    size_t expectedLen = arrPat.elements.size();
    emit(OpCode::LOAD_VAR, discSlot);
    emit(OpCode::ARRAY_LEN);
    if (arrPat.rest) {
      // Rest pattern: length must be >= element count
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(expectedLen))));
      emit(OpCode::GTE);
    } else {
      // Exact match: length must equal
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(expectedLen))));
      emit(OpCode::EQ);
    }
    emit(OpCode::AND);
    
    // Extract and bind elements
    for (size_t i = 0; i < arrPat.elements.size(); i++) {
      const auto &elem = arrPat.elements[i];
      if (elem && elem->kind == ast::NodeType::Identifier) {
        const auto &ident = static_cast<const ast::Identifier &>(*elem);
        // Get array element
        emit(OpCode::LOAD_VAR, discSlot);
        emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(i))));
        emit(OpCode::ARRAY_GET);
        // Bind to variable
        auto binding = bindingFor(ident);
        if (binding && binding->kind == ResolvedBindingKind::Local) {
          emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
        }
      }
    }
    
    // Handle rest pattern
    if (arrPat.rest) {
      const auto &rest = arrPat.rest;
      if (rest->kind == ast::NodeType::Identifier) {
        const auto &ident = static_cast<const ast::Identifier &>(*rest);
        // Create array of remaining elements
        // For simplicity, just bind to null for now
        // TODO: create slice array
        auto binding = bindingFor(ident);
        if (binding && binding->kind == ResolvedBindingKind::Local) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
        }
      }
    }
    break;
  }
  
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    // ObjectPattern: check each field matches AND bind variables
    // Start with true
    emit(OpCode::LOAD_CONST, addConstant(Value::makeBool(true)));
    
    for (const auto &prop : objPat.properties) {
      // Get field value
      emit(OpCode::LOAD_VAR, discSlot);
      { uint32_t strId = addStringConstant(prop.first); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(strId))); };
      emit(OpCode::OBJECT_GET);
      
      // Compare with expected pattern AND bind variables
      if (prop.second) {
        if (prop.second->kind == ast::NodeType::Identifier) {
          // Identifier pattern: check existence and bind
          const auto &ident = static_cast<const ast::Identifier &>(*prop.second);
          // Find slot by looking up in resolution results
          auto binding = bindingFor(ident);
          if (binding && binding->kind == ResolvedBindingKind::Local) {
            // DUP the value, store to pattern slot, then check existence
            emit(OpCode::DUP);
            emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
          }
          // Check existence (non-null)
          emit(OpCode::IS_NULL);
          emit(OpCode::NOT);
          emit(OpCode::AND);
        } else {
          // Nested pattern - compare
          compileExpression(*prop.second);
          emit(OpCode::EQ);
          emit(OpCode::AND);
        }
      } else {
        // Null pattern - just check existence
        emit(OpCode::IS_NULL);
        emit(OpCode::NOT);
        emit(OpCode::AND);
      }
    }
    break;
  }
  
  case ast::NodeType::RangePattern: {
    const auto &rangePat = static_cast<const ast::RangePattern &>(pattern);
    // Range pattern: check discSlot >= start && discSlot <= end
    // Load discriminant
    emit(OpCode::LOAD_VAR, discSlot);
    // Compile start and check >=
    compileExpression(*rangePat.start);
    emit(OpCode::GTE);
    
    // Load discriminant again and check <= end
    emit(OpCode::LOAD_VAR, discSlot);
    compileExpression(*rangePat.end);
    emit(OpCode::LTE);
    
    // Combine: (disc >= start) && (disc <= end)
    emit(OpCode::AND);
    break;
  }

  case ast::NodeType::WildcardPattern: {
    // Wildcard pattern: always matches
    emit(OpCode::LOAD_CONST, addConstant(Value::makeBool(true)));
    break;
  }

  default: {
    // Simple pattern (literal, identifier): use EQ comparison
    compileExpression(pattern);
    emit(OpCode::LOAD_VAR, discSlot);
    emit(OpCode::EQ);
    break;
  }
  }
}

void ByteCompiler::compileExpression(const ast::Expression &expression) {
  auto source_scope = atNode(expression);
  switch (expression.kind) {
  case ast::NodeType::NumberLiteral: {
    const auto &num = static_cast<const ast::NumberLiteral &>(expression);
    if (isIntegerLiteral(num.value)) {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(num.value))));
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeDouble(num.value)));
    }
    break;
  }

  case ast::NodeType::StringLiteral: {
    const auto &str = static_cast<const ast::StringLiteral &>(expression);
    { uint32_t _sid = addStringConstant(str.value); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    break;
  }

  case ast::NodeType::CharLiteral: {
    const auto &ch = static_cast<const ast::CharLiteral &>(expression);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(ch.value))));
    break;
  }

  case ast::NodeType::HotkeyLiteral: {
    const auto &hotkey = static_cast<const ast::HotkeyLiteral &>(expression);
    { uint32_t _sid = addStringConstant(hotkey.combination); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    break;
  }

  case ast::NodeType::HotkeyExpression: {
    // Hotkey binding as expression (assignment RHS)
    const auto &hkExpr = static_cast<const ast::HotkeyExpression &>(expression);
    compileHotkeyBindingExpr(*hkExpr.binding);
    break;
  }

  case ast::NodeType::InterpolatedStringExpression: {
    const auto &interp =
        static_cast<const ast::InterpolatedStringExpression &>(expression);

    // Build interpolated string by concatenating segments
    // Start with empty string
    { uint32_t _sid = addStringConstant(""); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };

    for (const auto &segment : interp.segments) {
      if (segment.isString) {
        // Push string segment
        { uint32_t _sid = addStringConstant(segment.stringValue); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
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
    emit(OpCode::LOAD_CONST, addConstant(Value::makeBool(boolean.value)));
    break;
  }

  case ast::NodeType::NullLiteral: {
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
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
          COMPILER_THROW("Array literal contains null element");
        }
        emit(OpCode::DUP);
        compileExpression(*element);
        emit(OpCode::ARRAY_PUSH);
        emit(OpCode::POP); // Remove duplicate array from stack (ARRAY_PUSH
                           // pushes container back)
      }
    } else {
      // Complex case: has spread, build array element by element
      emit(OpCode::ARRAY_NEW);
      uint32_t resultSlot = next_local_index++;
      reserveLocalSlot(resultSlot);
      emit(OpCode::STORE_VAR, resultSlot);

      for (const auto &element : array.elements) {
        if (!element) {
          COMPILER_THROW("Array literal contains null element");
        }
        if (element->kind == ast::NodeType::SpreadExpression) {
          const auto &spread =
              static_cast<const ast::SpreadExpression &>(*element);
          if (!spread.target) {
            COMPILER_THROW("Spread expression missing target");
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
          emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(0)));
          emit(OpCode::STORE_VAR, idxSlot);

          uint32_t loopStart =
              static_cast<uint32_t>(current_function->instructions.size());

          // Check if idx < len (continue loop if true)
          emit(OpCode::LOAD_VAR, idxSlot);
          emit(OpCode::LOAD_VAR, lenSlot);
          emit(OpCode::LT);
          uint32_t endJump = emitJump(OpCode::JUMP_IF_FALSE);

          // Get array element and push to result array
          emit(OpCode::LOAD_VAR, resultSlot); // Load result array
          emit(OpCode::LOAD_VAR, spreadArrSlot);
          emit(OpCode::LOAD_VAR, idxSlot);
          emit(OpCode::ARRAY_GET);
          // Stack: result_array, element
          emit(OpCode::ARRAY_PUSH);
          // Stack: result_array
          emit(OpCode::STORE_VAR, resultSlot); // Save result array back

          // Increment index
          emit(OpCode::LOAD_VAR, idxSlot);
          emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(1)));
          emit(OpCode::ADD);
          emit(OpCode::STORE_VAR, idxSlot);

          emit(OpCode::JUMP, loopStart);

          uint32_t loopEnd =
              static_cast<uint32_t>(current_function->instructions.size());
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
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*element);
      }
      emit(OpCode::ARRAY_PUSH);
    }
    emit(OpCode::ARRAY_FREEZE);
    break;
  }

  case ast::NodeType::SetExpression: {
    const auto &set = static_cast<const ast::SetExpression &>(expression);
    emit(OpCode::SET_NEW);
    for (const auto &element : set.elements) {
      if (!element) {
        COMPILER_THROW("Set literal contains null element");
      }
      emit(OpCode::DUP);
      emit(OpCode::LOAD_CONST, Value::makeBool(true));
      compileExpression(*element);
      emit(OpCode::SET_SET);
    }
    break;
  }

  case ast::NodeType::ObjectLiteral: {
    const auto &object = static_cast<const ast::ObjectLiteral &>(expression);
    // Emit sorted or unsorted object creation based on AST flag
    if (object.unsorted) {
      emit(OpCode::OBJECT_NEW_UNSORTED);
    } else {
      emit(OpCode::OBJECT_NEW);
    }
    uint32_t positionalIndex = 0;
    for (const auto &entry : object.pairs) {
      if (!entry.value) {
        COMPILER_THROW("Collection literal contains null value");
      }
      emit(OpCode::DUP);

      // Check for spread
      if (entry.key == "__spread__") {
        compileExpression(*entry.value);
        // Call any.extend(obj, source) to spread
        uint32_t strId = addStringConstant("any.extend");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(2))});
      } else if (entry.isComputedKey) {
        // Computed key: {[expr]: value}
        compileExpression(*entry.value);
        compileExpression(*entry.keyExpr);
        emit(OpCode::OBJECT_SET);
      } else if (entry.key.empty()) {
        // Positional element: store at numeric index
        compileExpression(*entry.value);
        emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(positionalIndex))));
        positionalIndex++;
        emit(OpCode::OBJECT_SET);
      } else {
        // Regular key:value pair
        compileExpression(*entry.value);
        uint32_t strId = addStringConstant(entry.key);
        emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(strId)));
        emit(OpCode::OBJECT_SET);
      }
    }
      break;
    }

    case ast::NodeType::IfExpression: {
      const auto &ifExpr = static_cast<const ast::IfExpression &>(expression);

      // Compile condition
      compileExpression(*ifExpr.condition);

      // Jump to else branch if false
      uint32_t elseJump = emitJump(OpCode::JUMP_IF_FALSE);

      // Compile then branch
      compileExpression(*ifExpr.thenBranch);

      // Jump over else branch
      uint32_t endJump = emitJump(OpCode::JUMP);

      // Patch else jump
      uint32_t elseTarget = static_cast<uint32_t>(current_function->instructions.size());
      patchJump(elseJump, elseTarget);

      // Compile else branch (or push null if no else)
      if (ifExpr.elseBranch) {
        compileExpression(*ifExpr.elseBranch);
      } else {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      }

      // Patch end jump
      uint32_t endTarget = static_cast<uint32_t>(current_function->instructions.size());
      patchJump(endJump, endTarget);

      break;
    }

    case ast::NodeType::MatchExpression: {
    const auto &match = static_cast<const ast::MatchExpression &>(expression);

    // Compile all discriminants and store them in temp variables
    std::vector<uint32_t> discriminantSlots;
    for (const auto &discriminant : match.discriminants) {
      if (!discriminant) {
        COMPILER_THROW("Match expression has null discriminant");
      }
      compileExpression(*discriminant);
      uint32_t slot = next_local_index++;
      reserveLocalSlot(slot);
      emit(OpCode::STORE_VAR, slot);
      discriminantSlots.push_back(slot);
    }

    std::vector<uint32_t> caseJumps;
    
    // Allocate a temp slot to track whether all patterns match
    uint32_t matchSlot = next_local_index++;
    reserveLocalSlot(matchSlot);

    // Compile each case
    for (const auto &arm : match.cases) {
      const auto &patterns = arm.patterns;
      const auto &result = arm.result;

      // Initialize match slot to true (all patterns match so far)
      emit(OpCode::LOAD_CONST, addConstant(Value::makeBool(true)));
      emit(OpCode::STORE_VAR, matchSlot);

      // For multi-discriminant matching, we need all patterns to match
      for (size_t i = 0; i < patterns.size() && i < discriminantSlots.size(); i++) {
        if (patterns[i]) {
          // Load current match status first
          emit(OpCode::LOAD_VAR, matchSlot);
          
          // Compile pattern match for this discriminant
          compilePattern(*patterns[i], discriminantSlots[i]);
          
          // AND the match status with the comparison result
          // Stack: [match_status, match_result] -> [result]
          emit(OpCode::AND);
          emit(OpCode::STORE_VAR, matchSlot);
        }
        // If pattern[i] is null (underscore wildcard), it always matches
      }

      // Check if all patterns matched
      emit(OpCode::LOAD_VAR, matchSlot);
      
      // Jump to next case if not matched
      uint32_t failJump = emitJump(OpCode::JUMP_IF_FALSE);

      // If there's a guard condition, evaluate it
      if (arm.guard) {
        compileExpression(*arm.guard);
        // Jump to next case if guard is false
        uint32_t guardFailJump = emitJump(OpCode::JUMP_IF_FALSE);
        
        // Guard passed, compile result expression
        compileExpression(*result);
        caseJumps.push_back(emitJump(OpCode::JUMP));
        
        // Patch guard failure jump to next case
        uint32_t nextCaseTarget = static_cast<uint32_t>(current_function->instructions.size());
        patchJump(guardFailJump, nextCaseTarget);
      } else {
        // All patterns matched, compile result expression
        compileExpression(*result);

        // Jump to end of match
        caseJumps.push_back(emitJump(OpCode::JUMP));
      }

      // Patch pattern failure jump to here (next case)
      uint32_t nextCaseTarget = static_cast<uint32_t>(current_function->instructions.size());
      patchJump(failJump, nextCaseTarget);
    }

    // Compile default case - TCO: inherit tail position
    uint32_t defaultTarget =
        static_cast<uint32_t>(current_function->instructions.size());
    if (match.defaultCase) {
      compileExpression(*match.defaultCase);
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
    }

    uint32_t endTarget =
        static_cast<uint32_t>(current_function->instructions.size());

    // Patch all result jumps to end
    for (uint32_t resultJump : caseJumps) {
      patchJump(resultJump, endTarget);
    }

    break;
  }

  case ast::NodeType::LambdaExpression: {
    const auto &lambda = static_cast<const ast::LambdaExpression &>(expression);
    auto it = lambda_indices_by_node_.find(&lambda);
    if (it == lambda_indices_by_node_.end()) {
      COMPILER_THROW("Missing function index for lambda expression");
    }
    auto upvalues_it = lexical_resolution_.lambda_upvalues.find(&lambda);
    if (upvalues_it != lexical_resolution_.lambda_upvalues.end() &&
        !upvalues_it->second.empty()) {
      emit(OpCode::CLOSURE, it->second);
    } else {
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeFunctionObjId(it->second)));
    }
    break;
  }

  case ast::NodeType::Identifier: {
    const auto &id = static_cast<const ast::Identifier &>(expression);

    // Explicit global-scope identifier (::x).
    if (id.isGlobalScope) {
      uint32_t strId = addStringConstant(id.symbol);
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
      break;
    }

    const auto *binding = bindingFor(id);
    if (!binding) {
      COMPILER_THROW("Missing lexical binding for identifier: " +
                               id.symbol);
    }

    switch (binding->kind) {
    case ResolvedBindingKind::Local:
      emit(OpCode::LOAD_VAR, effectiveSlot(binding->slot));
      break;
    case ResolvedBindingKind::Upvalue:
      emit(OpCode::LOAD_UPVALUE, binding->slot);
      break;
    case ResolvedBindingKind::Function:
      // User-defined function - load as FunctionObject
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeFunctionObjId(
               top_level_function_indices_by_name_[binding->name])));
      break;
    case ResolvedBindingKind::HostFunction:
      // Host function - load as global, runtime will dispatch
      {
        uint32_t strId = addStringConstant(binding->name);
        emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
      }
      break;
    case ResolvedBindingKind::Global:
      // Global variable - runtime will decide
      {
        uint32_t strId = addStringConstant(binding->name);
        emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
      }
      break;
    }
    break;
  }

  case ast::NodeType::ThisExpression: {
    // `this` keyword - load current object reference
    // In class instance methods, 'this' is in local slot 0
    if (!current_class_name_.empty()) {
      emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
    } else {
      // Non-class context: fall back to global
      uint32_t strId = addStringConstant("this");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
    }
    break;
  }

  case ast::NodeType::AtExpression: {
    // @field - compile as loading 'this' (slot 0) and getting the field
    const auto &atExpr = static_cast<const ast::AtExpression &>(expression);
    if (!atExpr.field) {
      COMPILER_THROW("@ expression missing field");
    }
    auto *fieldId = dynamic_cast<const ast::Identifier *>(atExpr.field.get());
    if (!fieldId) {
      COMPILER_THROW("@ expression field must be an identifier");
    }

    // Check if this is a hotkey directive method (disable/enable/remove/toggle)
    // These should be called as methods, not just accessed as fields
    bool isDirective = (fieldId->symbol == "disable" ||
                        fieldId->symbol == "enable" ||
                        fieldId->symbol == "remove" ||
                        fieldId->symbol == "toggle");

    // Load 'this' (slot 0 for instance methods, or LOAD_GLOBAL for non-class methods)
    if (!current_class_name_.empty()) {
      // Class instance method: 'this' is in local slot 0
      emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
    } else {
      // Non-class context: fall back to global (for hotkey directives)
      uint32_t strId = addStringConstant("this");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
    }
    // Get the field/method from this
    { uint32_t _sid = addStringConstant(fieldId->symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);

    // If it's a directive method, call it
    if (isDirective) {
      emit(OpCode::CALL, static_cast<uint32_t>(0));
    }
    break;
  }

  case ast::NodeType::AtAtExpression: {
    // @@field - compile as loading class object and getting the field
    const auto &atExpr = static_cast<const ast::AtAtExpression &>(expression);
    if (!atExpr.field) {
      COMPILER_THROW("@@ expression missing field");
    }
    auto *fieldId = dynamic_cast<const ast::Identifier *>(atExpr.field.get());
    if (!fieldId) {
      COMPILER_THROW("@@ expression field must be an identifier");
    }
    // Load class object (stored as global with class name)
    { uint32_t strId = addStringConstant(current_class_name_); emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId)); };
    // Get the field from class object
    { uint32_t _sid = addStringConstant(fieldId->symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    break;
  }

  case ast::NodeType::BinaryExpression: {
    const auto &binary = static_cast<const ast::BinaryExpression &>(expression);
    if (!binary.left || !binary.right) {
      COMPILER_THROW("Malformed binary expression");
    }

    // Special handling for 'in' and 'not in' - compile as host function call
    if (binary.operator_ == ast::BinaryOperator::In ||
        binary.operator_ == ast::BinaryOperator::NotIn) {
      compileExpression(*binary.left);  // value to check
      compileExpression(*binary.right); // container
      std::string fnName =
          binary.operator_ == ast::BinaryOperator::In ? "any.in" : "any.not_in";
      emit(OpCode::CALL_HOST,
           std::vector<Value>{Value::makeStringValId(addStringConstant(fnName)), Value::makeInt(static_cast<int64_t>(2))});
    } else if (binary.operator_ == ast::BinaryOperator::Matches ||
               binary.operator_ == ast::BinaryOperator::Tilde) {
      // Regex/string matching - compile as regex_search host function call
      compileExpression(*binary.left);  // string to match
      compileExpression(*binary.right); // pattern
      {
        uint32_t strId = addStringConstant("regex_search");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(2))});
      }
    } else if (binary.operator_ == ast::BinaryOperator::Nullish) {
      // Nullish coalescing: left ?? right
      // Evaluate left side
      compileExpression(*binary.left);

      // Duplicate the value (we need it for both the null check and potential
      // result)
      emit(OpCode::DUP);

      // Jump to right side if left is null/undefined (this pops the duplicate)
      uint32_t jumpToRight = emitJump(OpCode::JUMP_IF_NULL);

      // Left is not null - the original value is still on stack, just skip the
      // right side
      uint32_t done = emitJump(OpCode::JUMP);

      // Left was null - evaluate right side (the null was already popped by
      // JUMP_IF_NULL)
      patchJump(jumpToRight,
                static_cast<uint32_t>(current_function->instructions.size()));
      compileExpression(*binary.right);

      // Done
      patchJump(done,
                static_cast<uint32_t>(current_function->instructions.size()));
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
      COMPILER_THROW("Malformed range expression");
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
      COMPILER_THROW("Pipeline expression has no stages");
    }

    // Compile first stage normally
    if (pipeline.stages[0]->kind == ast::NodeType::CallExpression) {
      const auto &call =
          static_cast<const ast::CallExpression &>(*pipeline.stages[0]);
      if (call.callee) {
        compileExpression(*call.callee);
      }
      for (const auto &arg : call.args) {
        if (arg)
          compileExpression(*arg);
      }
      emit(OpCode::CALL, static_cast<uint32_t>(call.args.size()));
    } else {
      compileExpression(*pipeline.stages[0]);
    }

    // For each subsequent stage, apply enhanced pipeline rules
    for (size_t i = 1; i < pipeline.stages.size(); ++i) {
      auto &stage = pipeline.stages[i];
      if (!stage) {
        COMPILER_THROW("Pipeline stage is null");
      }

      // Reserve temp slot for previous pipe value
      uint32_t pipe_temp = next_local_index;
      reserveLocalSlot(pipe_temp);
      emit(OpCode::STORE_VAR, pipe_temp); // Store previous result

      // Lambda expression: | x => x.isFile  (filter/map)
      if (stage->kind == ast::NodeType::LambdaExpression) {
        // Load the piped value as argument
        emit(OpCode::LOAD_VAR, pipe_temp);
        // Compile and call the lambda
        compileExpression(*stage);
        emit(OpCode::CALL, 1); // Call lambda with 1 arg (piped value)
      }
      // Call expression with explicit args: | find "ERROR"
      // Auto-curry: prepend piped value as first arg
      else if (stage->kind == ast::NodeType::CallExpression) {
        const auto &call = static_cast<const ast::CallExpression &>(*stage);
        // Load piped value first (as first argument - auto-curry)
        emit(OpCode::LOAD_VAR, pipe_temp);
        // Compile the callee
        if (call.callee) {
          compileExpression(*call.callee);
        }
        // Add explicit arguments after piped value
        for (const auto &arg : call.args) {
          if (arg)
            compileExpression(*arg);
        }
        // Call with (1 + explicit_args) count
        emit(OpCode::CALL, static_cast<uint32_t>(1 + call.args.size()));
      }
      // Identifier: | print, | upper, etc.
      // Tap behavior: function receives value, we ensure pass-through
      else if (stage->kind == ast::NodeType::Identifier) {
        const auto &ident = static_cast<const ast::Identifier &>(*stage);
        emit(OpCode::LOAD_VAR, pipe_temp); // Load piped value as arg

        // Route through any.* dispatch for consistency
        {
          uint32_t strId = addStringConstant("any." + ident.symbol);
          emit(OpCode::CALL_HOST, std::vector<Value>{
              Value::makeStringValId(strId),
              Value(static_cast<uint32_t>(1))});
        }

        // For tap functions (print), ensure value passes through
        // If result is nil, restore the previous pipe value
        if (ident.symbol == "print" || ident.symbol == "log" ||
            ident.symbol == "debug" || ident.symbol == "tap") {
          // Check if result is nil, if so restore pipe value
          uint32_t result_temp = next_local_index;
          reserveLocalSlot(result_temp);
          emit(OpCode::STORE_VAR, result_temp);
          emit(OpCode::LOAD_VAR, result_temp);
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull())); // nil
          // If nil, use pipe value, else use result
          // Simplified: just dup and check
          emit(OpCode::LOAD_VAR, pipe_temp); // Default to pipe value
        }
      }
      // Member expression: | text.upper, | array.filter
      else if (stage->kind == ast::NodeType::MemberExpression) {
        const auto &member = static_cast<const ast::MemberExpression &>(*stage);
        emit(OpCode::LOAD_VAR, pipe_temp); // Load piped value
        // Compile member access - the object is the piped value
        compileExpression(*stage);
        // Call with piped value as first arg
        emit(OpCode::CALL, 1);
      }
      // Default: compile expression and call with piped value
      else {
        emit(OpCode::LOAD_VAR, pipe_temp);
        compileExpression(*stage);
        emit(OpCode::CALL, 1);
      }

      // Void pass-through: if result is nil/none, restore previous pipe value
      // This is handled at runtime - we emit code to check and restore
      uint32_t stage_result = next_local_index;
      reserveLocalSlot(stage_result);
      emit(OpCode::STORE_VAR, stage_result);

      // Load result and check if nil
      emit(OpCode::LOAD_VAR, stage_result);
      emit(OpCode::DUP);
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull())); // nil

      // If equal (both nil), restore pipe value
      // For now, simplified: always have result, void functions
      // should be marked in HostBridge to return previous value
      emit(OpCode::LOAD_VAR, stage_result); // Use stage result
    }
    break;
  }

  case ast::NodeType::AssignmentExpression: {
    const auto &assignment =
        static_cast<const ast::AssignmentExpression &>(expression);
    const ast::Expression *rhs_expr = assignment.value.get();
    bool rhs_is_missing = (rhs_expr == nullptr);

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
    const auto *target_array =
        assignment.target
            ? dynamic_cast<const ast::ArrayLiteral *>(assignment.target.get())
            : nullptr;
    const auto *target_object =
        assignment.target
            ? dynamic_cast<const ast::ObjectLiteral *>(assignment.target.get())
            : nullptr;
    const auto *target_at =
        assignment.target
            ? dynamic_cast<const ast::AtExpression *>(assignment.target.get())
            : nullptr;
    const auto *target_atat =
        assignment.target
            ? dynamic_cast<const ast::AtAtExpression *>(assignment.target.get())
            : nullptr;

    auto emitStoreIdentifierWithResult = [&](const ResolvedBinding &binding) {
      if (binding.is_const) {
        COMPILER_THROW("Cannot assign to const binding: " +
                                 binding.name);
      }
      emit(OpCode::DUP);
      if (binding.kind == ResolvedBindingKind::Local) {
        emit(OpCode::STORE_VAR, binding.slot);
      } else if (binding.kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::STORE_UPVALUE, binding.slot);
      } else if (binding.kind == ResolvedBindingKind::Global) {
        {
          uint32_t strId = addStringConstant(binding.name);
          emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
        }
      } else {
        COMPILER_THROW("Assignment target is not mutable");
      }
    };

    auto emitLoadIdentifier = [&](const ResolvedBinding &binding) {
      if (binding.is_const && assignment.operator_ != "=") {
        COMPILER_THROW("Cannot mutate const binding: " +
                                 binding.name);
      }
      if (binding.kind == ResolvedBindingKind::Local) {
        emit(OpCode::LOAD_VAR, binding.slot);
      } else if (binding.kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::LOAD_UPVALUE, binding.slot);
      } else if (binding.kind == ResolvedBindingKind::Global) {
        {
          uint32_t strId = addStringConstant(binding.name);
          emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
        }
      } else {
        COMPILER_THROW("Assignment target is not mutable");
      }
    };

    auto emitStoreMemberWithResult = [&](const ast::MemberExpression &member) {
      auto *property =
          dynamic_cast<const ast::Identifier *>(member.property.get());
      if (!member.object || !property) {
        COMPILER_THROW(
            "Member assignment expects identifier property target");
      }
      uint32_t temp_slot = next_local_index;
      reserveLocalSlot(temp_slot);
      emit(OpCode::STORE_VAR, temp_slot);
      compileExpression(*member.object);
      emit(OpCode::LOAD_VAR, temp_slot);
      { uint32_t _sid = addStringConstant(property->symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      emit(OpCode::OBJECT_SET);
      emit(OpCode::LOAD_VAR, temp_slot);
    };

    auto emitStoreIndexWithResult =
        [&](const ast::IndexExpression &index_expr) {
          if (!index_expr.object || !index_expr.index) {
            COMPILER_THROW(
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
      if (rhs_is_missing) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*rhs_expr);
      }
      if (target_id) {
        // Check for global scope assignment (::x = value)
        if (assignment.isGlobalScope) {
          emit(OpCode::DUP);
          emit(OpCode::STORE_GLOBAL,
               std::vector<Value>{Value::makeStringValId(addStringConstant(target_id->symbol))});
          break;
        }

        const auto *binding = bindingFor(*target_id);
        if (!binding) {
          COMPILER_THROW(
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
      if (target_array) {
        // Array destructuring assignment: [a, b, c] = value
        const auto &arrayLit =
            static_cast<const ast::ArrayLiteral &>(*target_array);

        // Compile the value first
      if (rhs_is_missing) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*rhs_expr);
      }
        uint32_t temp_slot = next_local_index;
        reserveLocalSlot(temp_slot);
        emit(OpCode::STORE_VAR, temp_slot);

        // Extract each element
        for (size_t i = 0; i < arrayLit.elements.size(); ++i) {
          const auto &element = arrayLit.elements[i];
          if (element && element->kind == ast::NodeType::Identifier) {
            const auto &ident = static_cast<const ast::Identifier &>(*element);
            const auto *binding = bindingFor(ident);
            if (!binding) {
              COMPILER_THROW(
                  "Missing lexical binding for destructuring element: " +
                  ident.symbol);
            }
            // Load array, get element at index i
            emit(OpCode::LOAD_VAR, temp_slot);
            emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(i))));
            emit(OpCode::ARRAY_GET);
            // Store in the binding
            if (binding->kind == ResolvedBindingKind::Local) {
              emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
            } else if (binding->kind == ResolvedBindingKind::Global) {
              {
                uint32_t strId = addStringConstant(binding->name);
                emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
              }
            } else {
              COMPILER_THROW(
                  "Unsupported binding kind for destructuring");
            }
          }
        }
        break;
      }
      if (target_object) {
        // Object destructuring assignment: {key: val} = obj
        const auto &objLit =
            static_cast<const ast::ObjectLiteral &>(*target_object);

        // Compile the value first
      if (rhs_is_missing) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*rhs_expr);
      }
        uint32_t temp_slot = next_local_index;
        reserveLocalSlot(temp_slot);
        emit(OpCode::STORE_VAR, temp_slot);

        // Extract each property
        for (const auto &entry : objLit.pairs) {
          if (entry.value && entry.value->kind == ast::NodeType::Identifier) {
            const auto &ident =
                static_cast<const ast::Identifier &>(*entry.value);
            const auto *binding = bindingFor(ident);
            if (!binding) {
              COMPILER_THROW(
                  "Missing lexical binding for destructuring property: " +
                  ident.symbol);
            }
            // Load object, get property by key
            emit(OpCode::LOAD_VAR, temp_slot);
            { uint32_t _sid = addStringConstant(entry.key); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
            emit(OpCode::OBJECT_GET);
            // Store in the binding
            if (binding->kind == ResolvedBindingKind::Local) {
              emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
            } else if (binding->kind == ResolvedBindingKind::Global) {
              {
                uint32_t strId = addStringConstant(binding->name);
                emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
              }
            } else {
              COMPILER_THROW(
                  "Unsupported binding kind for destructuring");
            }
          }
        }
        break;
      }
      if (target_at) {
        // @field assignment - store to self.field
        // Get field name from AtExpression
        std::string field_name;
        if (target_at->field && target_at->field->kind == ast::NodeType::Identifier) {
          const auto *field_id = static_cast<const ast::Identifier *>(target_at->field.get());
          field_name = field_id->symbol;
        } else {
          COMPILER_THROW("@field assignment requires identifier");
        }
        // Compile value first
        if (rhs_is_missing) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        } else {
          compileExpression(*rhs_expr);
        }
        // Store to self.field: OBJECT_SET expects [obj, value, key]
        // Use temp slots to arrange the stack correctly
        uint32_t temp_val = next_local_index;
        reserveLocalSlot(temp_val);
        emit(OpCode::STORE_VAR, temp_val);  // pop value
        // Stack is now empty
        { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        uint32_t temp_key = next_local_index;
        reserveLocalSlot(temp_key);
        emit(OpCode::STORE_VAR, temp_key);  // pop key
        emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0)); // [self]
        emit(OpCode::LOAD_VAR, temp_val);  // [self, value]
        emit(OpCode::LOAD_VAR, temp_key);  // [self, value, key]
        emit(OpCode::OBJECT_SET);
        break;
      }
      if (target_atat) {
        // @@field assignment - store to class.field
        // Get field name from AtAtExpression
        std::string field_name;
        if (target_atat->field && target_atat->field->kind == ast::NodeType::Identifier) {
          const auto *field_id = static_cast<const ast::Identifier *>(target_atat->field.get());
          field_name = field_id->symbol;
        } else {
          COMPILER_THROW("@@field assignment requires identifier");
        }
        // Compile value first
        if (rhs_is_missing) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        } else {
          compileExpression(*rhs_expr);
        }
        // Store to class.field: OBJECT_SET expects [obj, value, key]
        // Load class object (stored as global with class name)
        { uint32_t _sid = addStringConstant(current_class_name_); emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(_sid)); };
        // Now stack has [class_obj], need [class_obj, value, key]
        // Use temp slots to arrange the stack correctly
        uint32_t temp_val = next_local_index;
        reserveLocalSlot(temp_val);
        emit(OpCode::STORE_VAR, temp_val);  // pop value
        // Stack is now [class_obj]
        // Save class obj to temp
        uint32_t temp_obj = next_local_index;
        reserveLocalSlot(temp_obj);
        emit(OpCode::STORE_VAR, temp_obj);  // pop class_obj
        // Stack is empty
        { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        uint32_t temp_key = next_local_index;
        reserveLocalSlot(temp_key);
        emit(OpCode::STORE_VAR, temp_key);  // pop key
        // Stack is empty, rebuild: [class_obj, value, key]
        emit(OpCode::LOAD_VAR, temp_obj);   // [class_obj]
        emit(OpCode::LOAD_VAR, temp_val);   // [class_obj, value]
        emit(OpCode::LOAD_VAR, temp_key);   // [class_obj, value, key]
        emit(OpCode::OBJECT_SET);
        break;
      }
      COMPILER_THROW("Unsupported assignment target");
    }

    auto emitCompound = [&](OpCode math_op) {
      if (target_id) {
        const auto *binding = bindingFor(*target_id);
        if (!binding) {
          COMPILER_THROW(
              "Missing lexical binding for assignment target: " +
              target_id->symbol);
        }
        emitLoadIdentifier(*binding);
      if (rhs_is_missing) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*rhs_expr);
      }
        emit(math_op);
        emitStoreIdentifierWithResult(*binding);
        return;
      }
      if (target_member) {
        auto *property = dynamic_cast<const ast::Identifier *>(
            target_member->property.get());
        if (!target_member->object || !property) {
          COMPILER_THROW(
              "Member assignment expects identifier property target");
        }
        uint32_t temp_object = next_local_index;
        reserveLocalSlot(temp_object);
        compileExpression(*target_member->object);
        emit(OpCode::STORE_VAR, temp_object);
        emit(OpCode::LOAD_VAR, temp_object);
        { uint32_t _sid = addStringConstant(property->symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::OBJECT_GET);
      if (rhs_is_missing) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*rhs_expr);
      }
        emit(math_op);
        uint32_t temp_result = next_local_index;
        reserveLocalSlot(temp_result);
        emit(OpCode::DUP);
        emit(OpCode::STORE_VAR, temp_result);
        emit(OpCode::LOAD_VAR, temp_object);
        { uint32_t _sid = addStringConstant(property->symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::LOAD_VAR, temp_result);
        emit(OpCode::OBJECT_SET);
        emit(OpCode::LOAD_VAR, temp_result);
        return;
      }
      if (target_index) {
        if (!target_index->object || !target_index->index) {
          COMPILER_THROW("Index assignment expects object and index");
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
      if (rhs_is_missing) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      } else {
        compileExpression(*rhs_expr);
      }
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
      if (target_at) {
        // @field compound assignment
        // Get field name
        std::string field_name;
        if (target_at->field && target_at->field->kind == ast::NodeType::Identifier) {
          const auto *field_id = static_cast<const ast::Identifier *>(target_at->field.get());
          field_name = field_id->symbol;
        } else {
          COMPILER_THROW("@field compound assignment requires identifier");
        }
        uint32_t temp_result = next_local_index;
        reserveLocalSlot(temp_result);
        // Load self.field
        emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
        { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::OBJECT_GET);
        if (rhs_is_missing) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        } else {
          compileExpression(*rhs_expr);
        }
        emit(math_op);
        emit(OpCode::DUP);
        emit(OpCode::STORE_VAR, temp_result);
        // Store back to self.field
        emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
        { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::LOAD_VAR, temp_result);
        emit(OpCode::OBJECT_SET);
        emit(OpCode::LOAD_VAR, temp_result);
        return;
      }
      if (target_atat) {
        // @@field compound assignment
        std::string field_name;
        if (target_atat->field && target_atat->field->kind == ast::NodeType::Identifier) {
          const auto *field_id = static_cast<const ast::Identifier *>(target_atat->field.get());
          field_name = field_id->symbol;
        } else {
          COMPILER_THROW("@@field compound assignment requires identifier");
        }
        uint32_t temp_result = next_local_index;
        reserveLocalSlot(temp_result);
        // Load class.field
        { uint32_t _sid = addStringConstant(current_class_name_); emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(_sid)); };
        { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::OBJECT_GET);
        if (rhs_is_missing) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        } else {
          compileExpression(*rhs_expr);
        }
        emit(math_op);
        emit(OpCode::DUP);
        emit(OpCode::STORE_VAR, temp_result);
        // Store back to class.field
        // Stack: [result]
        uint32_t temp_obj = next_local_index;
        reserveLocalSlot(temp_obj);
        emit(OpCode::STORE_VAR, temp_obj);
        // Rebuild: [class_obj, result, field_key]
        { uint32_t _sid = addStringConstant(current_class_name_); emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(_sid)); };
        { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        emit(OpCode::LOAD_VAR, temp_obj);
        emit(OpCode::OBJECT_SET);
        emit(OpCode::LOAD_VAR, temp_result);
        return;
      }
      COMPILER_THROW("Unsupported compound assignment target");
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
    if (assignment.operator_ == "%=") {
      emitCompound(OpCode::MOD);
      break;
    }
    if (assignment.operator_ == "**=") {
      emitCompound(OpCode::POW);
      break;
    }

    COMPILER_THROW("Unsupported assignment operator: " +
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
      COMPILER_THROW("Unsupported member expression");
    }

    // Helper: create a constant that loads a string value
    auto loadStringConst = [this](const std::string &str) {
      uint32_t strId = addStringConstant(str);
      return addConstant(Value::makeStringValId(strId));
    };

    // Check if object is an identifier (variable) - use OBJECT_GET directly
    compileExpression(*member.object);
    emit(OpCode::LOAD_CONST, loadStringConst(property->symbol));
    emit(OpCode::OBJECT_GET);
    break;
  }

  case ast::NodeType::IndexExpression: {
    const auto &index = static_cast<const ast::IndexExpression &>(expression);
    if (!index.object || !index.index) {
      COMPILER_THROW("Malformed index expression");
    }
    compileExpression(*index.object);
    compileExpression(*index.index);
    emit(OpCode::ARRAY_GET);
    break;
  }

  case ast::NodeType::SpreadExpression: {
    const auto &spread = static_cast<const ast::SpreadExpression &>(expression);
    if (!spread.target) {
      COMPILER_THROW("Spread expression missing target");
    }
    compileExpression(*spread.target);
    emit(OpCode::SPREAD);
    break;
  }

  case ast::NodeType::AwaitExpression: {
    const auto &await_expr =
        static_cast<const ast::AwaitExpression &>(expression);
    if (!await_expr.argument) {
      COMPILER_THROW("Await expression missing argument");
    }
    compileExpression(*await_expr.argument);
    {
      uint32_t strId = addStringConstant("async.await");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(static_cast<uint32_t>(1))});
    }
    break;
  }

  case ast::NodeType::UpdateExpression: {
    const auto &update_expr =
        static_cast<const ast::UpdateExpression &>(expression);
    if (!update_expr.argument) {
      COMPILER_THROW("Update expression missing argument");
    }

    bool isIncrement =
        (update_expr.operator_ == ast::UpdateExpression::Operator::Increment);

    // Check for @field++ or @@field++
    const auto *target_at =
        dynamic_cast<const ast::AtExpression *>(update_expr.argument.get());
    const auto *target_atat =
        dynamic_cast<const ast::AtAtExpression *>(update_expr.argument.get());

    if (target_at) {
      // @field++ or @field--
      std::string field_name;
      if (target_at->field && target_at->field->kind == ast::NodeType::Identifier) {
        const auto *field_id = static_cast<const ast::Identifier *>(target_at->field.get());
        field_name = field_id->symbol;
      } else {
        COMPILER_THROW("@field update requires identifier");
      }
      // Load self.field
      if (!current_class_name_.empty()) {
        emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
      } else {
        emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(addStringConstant("this")));
      }
      emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(addStringConstant(field_name))));
      emit(OpCode::OBJECT_GET);
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(1))));
      emit(isIncrement ? OpCode::ADD : OpCode::SUB);
      emit(OpCode::DUP);
      // Store back to self.field: OBJECT_SET expects [obj, value, key]
      // Stack currently has: [result]
      uint32_t temp_val = next_local_index;
      reserveLocalSlot(temp_val);
      emit(OpCode::STORE_VAR, temp_val);  // pop result, stack: []
      // Build [self, result, field_key]
      if (!current_class_name_.empty()) {
        emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));
      } else {
        emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(addStringConstant("this")));
      }
      emit(OpCode::LOAD_VAR, temp_val);   // [self, result]
      { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };  // [self, result, key]
      emit(OpCode::OBJECT_SET);
      emit(OpCode::LOAD_VAR, temp_val);
      break;
    }

    if (target_atat) {
      // @@field++ or @@field--
      std::string field_name;
      if (target_atat->field && target_atat->field->kind == ast::NodeType::Identifier) {
        const auto *field_id = static_cast<const ast::Identifier *>(target_atat->field.get());
        field_name = field_id->symbol;
      } else {
        COMPILER_THROW("@@field update requires identifier");
      }
      // Load class.field
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(addStringConstant(current_class_name_)));
      emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(addStringConstant(field_name))));
      emit(OpCode::OBJECT_GET);
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(1))));
      emit(isIncrement ? OpCode::ADD : OpCode::SUB);
      emit(OpCode::DUP);
      // Store back to class.field: OBJECT_SET expects [obj, value, key]
      // Stack currently has: [result]
      uint32_t temp_val = next_local_index;
      reserveLocalSlot(temp_val);
      emit(OpCode::STORE_VAR, temp_val);  // pop result, stack: []
      // Build [class_obj, result, field_key]
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(addStringConstant(current_class_name_)));   // [class_obj]
      emit(OpCode::LOAD_VAR, temp_val);   // [class_obj, result]
      { uint32_t _sid = addStringConstant(field_name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };  // [class_obj, result, key]
      emit(OpCode::OBJECT_SET);
      emit(OpCode::LOAD_VAR, temp_val);
      break;
    }

    // The argument must be an identifier
    const auto *target_id =
        dynamic_cast<const ast::Identifier *>(update_expr.argument.get());
    if (!target_id) {
      COMPILER_THROW(
          "Update expression argument must be an identifier");
    }

    const auto *binding = bindingFor(*target_id);
    if (!binding) {
      COMPILER_THROW(
          "Missing lexical binding for update expression: " +
          target_id->symbol);
    }

    if (update_expr.isPrefix) {
      // Prefix: ++x or --x
      // Load, modify, store, return new value
      if (binding->kind == ResolvedBindingKind::Local) {
        emit(OpCode::LOAD_VAR, effectiveSlot(binding->slot));
      } else if (binding->kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::LOAD_UPVALUE, binding->slot);
      } else if (binding->kind == ResolvedBindingKind::Global) {
        {
          uint32_t strId = addStringConstant(binding->name);
          emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
        }
      } else {
        COMPILER_THROW(
            "Cannot update variable with binding kind: " +
            std::to_string(static_cast<int>(binding->kind)));
      }
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(1))));
      emit(isIncrement ? OpCode::ADD : OpCode::SUB);
      emit(OpCode::DUP); // Save result for return value
      if (binding->kind == ResolvedBindingKind::Local) {
        emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
      } else if (binding->kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::STORE_UPVALUE, binding->slot);
      } else if (binding->kind == ResolvedBindingKind::Global) {
        {
          uint32_t strId = addStringConstant(binding->name);
          emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
        }
      }
    } else {
      // Postfix: x++ or x--
      // Load, dup, modify, store, pop new value, return old value
      if (binding->kind == ResolvedBindingKind::Local) {
        emit(OpCode::LOAD_VAR, effectiveSlot(binding->slot));
      } else if (binding->kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::LOAD_UPVALUE, binding->slot);
      } else if (binding->kind == ResolvedBindingKind::Global) {
        {
          uint32_t strId = addStringConstant(binding->name);
          emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
        }
      } else {
        COMPILER_THROW("Cannot update variable with binding kind: " +
                                 std::to_string(static_cast<int>(binding->kind)));
      }
      emit(OpCode::DUP); // Save old value
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(1))));
      emit(isIncrement ? OpCode::ADD : OpCode::SUB);
      emit(OpCode::DUP); // Save new value for storage
      if (binding->kind == ResolvedBindingKind::Local) {
        emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
      } else if (binding->kind == ResolvedBindingKind::Upvalue) {
        emit(OpCode::STORE_UPVALUE, binding->slot);
      } else if (binding->kind == ResolvedBindingKind::Global) {
        {
          uint32_t strId = addStringConstant(binding->name);
          emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
        }
      }
      emit(OpCode::POP); // Remove new value, leave old value on stack as result
    }
    break;
  }

  case ast::NodeType::UnaryExpression: {
    const auto &unary = static_cast<const ast::UnaryExpression &>(expression);
    if (!unary.operand) {
      COMPILER_THROW("Unary expression missing operand");
    }

    // Compile operand first
    compileExpression(*unary.operand);

    // Apply unary operator
    switch (unary.operator_) {
    case ast::UnaryExpression::UnaryOperator::Not:
      emit(OpCode::NOT);
      break;
    case ast::UnaryExpression::UnaryOperator::Minus:
      emit(OpCode::NEGATE);
      break;
    case ast::UnaryExpression::UnaryOperator::Plus:
      // No-op for unary plus
      break;
    case ast::UnaryExpression::UnaryOperator::Length:
      // Length operator: call any.len on the operand
      {
        uint32_t strId = addStringConstant("any.len");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(1))});
      }
      break;
    default:
      COMPILER_THROW("Unsupported unary operator");
    }
    break;
  }

  case ast::NodeType::TernaryExpression: {
    const auto &ternary =
        static_cast<const ast::TernaryExpression &>(expression);
    if (!ternary.condition || !ternary.trueValue || !ternary.falseValue) {
      COMPILER_THROW("Ternary expression missing components");
    }

    // Compile condition
    compileExpression(*ternary.condition);

    // Jump to false branch if condition is false
    uint32_t false_jump = emitJump(OpCode::JUMP_IF_FALSE);

    // Compile true branch
    compileExpression(*ternary.trueValue);
    uint32_t end_jump = emitJump(OpCode::JUMP);

    // Patch false jump and compile false branch
    patchJump(false_jump,
              static_cast<uint32_t>(current_function->instructions.size()));
    compileExpression(*ternary.falseValue);

    // Patch end jump
    patchJump(end_jump,
              static_cast<uint32_t>(current_function->instructions.size()));
    break;
  }

  case ast::NodeType::GetInputExpression: {
    const auto &getInput = static_cast<const ast::GetInputExpression &>(expression);
    compileGetInputExpression(getInput);
    break;
  }

  // Pattern types - should be compiled via compilePattern, not directly
  case ast::NodeType::OrPattern:
  case ast::NodeType::ArrayPattern:
  case ast::NodeType::ObjectPattern:
  case ast::NodeType::SpreadPattern:
  case ast::NodeType::WildcardPattern:
    COMPILER_THROW("Pattern type used outside of match context: " +
                             expression.toString());

  // Concurrency Primitives
  case ast::NodeType::ThreadExpression:
    compileThreadExpression(static_cast<const ast::ThreadExpression &>(expression));
    break;

  case ast::NodeType::IntervalExpression:
    compileIntervalExpression(static_cast<const ast::IntervalExpression &>(expression));
    break;

  case ast::NodeType::TimeoutExpression:
    compileTimeoutExpression(static_cast<const ast::TimeoutExpression &>(expression));
    break;

  // Coroutines
  case ast::NodeType::YieldExpression:
    compileYieldExpression(static_cast<const ast::YieldExpression &>(expression));
    break;

  case ast::NodeType::GoExpression:
    compileGoExpression(static_cast<const ast::GoExpression &>(expression));
    break;

  case ast::NodeType::ChannelExpression:
    compileChannelExpression(static_cast<const ast::ChannelExpression &>(expression));
    break;

  default:
    COMPILER_THROW("Unsupported expression in bytecode compiler: " +
                             expression.toString());
  }
}

void ByteCompiler::compileCallExpression(
    const ast::CallExpression &expression) {
  if (!expression.callee) {
    COMPILER_THROW("Call expression missing callee");
  }

  uint32_t arg_count = static_cast<uint32_t>(expression.args.size());
  bool hasKwargs = !expression.kwargs.empty();

  // Handle super calls for prototype inheritance (@->method())
  if (expression.isSuperCall) {
    if (current_parent_class_name_.empty()) {
      COMPILER_THROW("Super call used outside a derived class method");
    }

    // Load parent class prototype from globals
    uint32_t parent_class_sid = addStringConstant(current_parent_class_name_);
    emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(parent_class_sid));
    
    // Get the method from parent prototype using OBJECT_GET_RAW (no binding)
    uint32_t method_sid = addStringConstant(expression.superMethodName);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(method_sid)));
    emit(OpCode::OBJECT_GET_RAW);
    
    // Stack: [raw_function]
    // Now push self (current instance at slot 0) and explicit args
    emit(OpCode::LOAD_VAR, static_cast<uint32_t>(0));  // self
    for (const auto &arg : expression.args) {
      if (!arg) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        continue;
      }
      compileExpression(*arg);
    }
    // CALL with self + explicit args
    emit(OpCode::CALL, static_cast<uint32_t>(arg_count + 1));
    return;
  }

  if (expression.callee->kind == ast::NodeType::Identifier) {
    const auto &callee_id =
        static_cast<const ast::Identifier &>(*expression.callee);
    const auto *binding = bindingFor(callee_id);
    // Check if this is a known host function (even if resolver didn't mark it as such)
    bool isHostFunc = (binding && binding->kind == ResolvedBindingKind::HostFunction) ||
                      host_global_names_.count(callee_id.symbol) > 0;
    if (binding && binding->kind == ResolvedBindingKind::Global &&
        top_level_struct_names_.find(callee_id.symbol) !=
            top_level_struct_names_.end()) {
      { uint32_t _sid = addStringConstant(callee_id.symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      uint32_t totalArgs = 1; // type name
      for (const auto &arg : expression.args) {
        if (!arg) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          totalArgs++;
          continue;
        }
        compileExpression(*arg);
        totalArgs++;
      }
      {
        uint32_t strId = addStringConstant("struct.new");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(totalArgs)});
      }
      return;
    }
    if (binding && binding->kind == ResolvedBindingKind::Global &&
        top_level_class_names_.find(callee_id.symbol) !=
            top_level_class_names_.end()) {
      { uint32_t _sid = addStringConstant(callee_id.symbol); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      uint32_t totalArgs = 1; // type name
      for (const auto &arg : expression.args) {
        if (!arg) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          totalArgs++;
          continue;
        }
        compileExpression(*arg);
        totalArgs++;
      }
      {
        uint32_t strId = addStringConstant("class.new");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(totalArgs)});
      }
      return;
    }
    // Check if calling a host function
    if (isHostFunc) {
      for (const auto &arg : expression.args) {
        if (!arg) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          continue;
        }
        compileExpression(*arg);
      }
      uint32_t totalArgs = arg_count;
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          compileExpression(*kwarg.value);
          { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
          emit(OpCode::OBJECT_SET);
        }
        totalArgs++;
      }
      {
        uint32_t strId = addStringConstant(binding ? binding->name : callee_id.symbol);
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(totalArgs)});
      }
      return;
    }
  }
  // Check for member call
  if (expression.callee->kind == ast::NodeType::MemberExpression) {
    const auto &member =
        static_cast<const ast::MemberExpression &>(*expression.callee);
    auto *property =
        dynamic_cast<const ast::Identifier *>(member.property.get());
    if (!member.object || !property) {
      COMPILER_THROW("Unsupported member call expression");
    }

    // Namespace/module call: system.hardware(), display.getMonitors(), etc.
    // Do not rewrite these to any.*; call the qualified host function directly.
    if (auto *objIdent =
            dynamic_cast<const ast::Identifier *>(member.object.get())) {
      if (host_global_names_.count(objIdent->symbol) > 0) {
        for (const auto &arg : expression.args) {
          if (!arg) {
            emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
            continue;
          }
          compileExpression(*arg);
        }
        uint32_t totalArgs = arg_count;
        if (hasKwargs) {
          emit(OpCode::OBJECT_NEW);
          for (const auto &kwarg : expression.kwargs) {
            { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
            compileExpression(*kwarg.value);
            emit(OpCode::OBJECT_SET);
          }
          totalArgs++;
        }
        uint32_t strId =
            addStringConstant(objIdent->symbol + "." + property->symbol);
        emit(OpCode::CALL_HOST, std::vector<Value>{
                                   Value::makeStringValId(strId),
                                   Value(totalArgs)});
        return;
      }
    }

    // Instance-style method call on runtime value (e.g., nums.map(double), m.moveTo(...), "hello".len(), arr.len()).
    // Always emit CALL_METHOD - the VM will dispatch based on runtime type.
    // For primitives: direct dispatch via prototype tables (no boxing).
    // For objects/classes: prototype chain lookup via bound method objects.
    compileExpression(*member.object);
    // Compile args, expanding spread
    uint32_t totalArgs = 0;
    for (const auto &arg : expression.args) {
      if (!arg) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        totalArgs++;
        continue;
      }
      if (arg->kind == ast::NodeType::SpreadExpression) {
        const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
        if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
          const auto &arrLit = static_cast<const ast::ArrayLiteral &>(*spread.target);
          for (const auto &elem : arrLit.elements) {
            if (elem) {
              compileExpression(*elem);
              totalArgs++;
            }
          }
        } else {
          compileExpression(*arg);
          totalArgs++;
        }
      } else {
        compileExpression(*arg);
        totalArgs++;
      }
    }
    if (hasKwargs) {
      emit(OpCode::OBJECT_NEW);
      for (const auto &kwarg : expression.kwargs) {
        { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
        compileExpression(*kwarg.value);
        emit(OpCode::OBJECT_SET);
      }
      totalArgs++;
    }
    uint32_t method_sid = addStringConstant(property->symbol);
    emit(OpCode::CALL_METHOD, std::vector<Value>{
        Value::makeStringValId(method_sid),
        Value(totalArgs)});
    return;
  }

  if (expression.callee->kind == ast::NodeType::Identifier) {
    const auto &callee_id =
        static_cast<const ast::Identifier &>(*expression.callee);
    const auto *binding = bindingFor(callee_id);
    if (!binding) {
      // Check if this is a known host global (e.g., print, sleep)
      if (host_global_names_.count(callee_id.symbol) > 0) {
        // Compile as host function call, expanding spread args
        uint32_t totalArgs = 0;
        for (const auto &arg : expression.args) {
          if (!arg) {
            emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
            totalArgs++;
            continue;
          }
          if (arg->kind == ast::NodeType::SpreadExpression) {
            const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
            if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
              const auto &arrLit = static_cast<const ast::ArrayLiteral &>(*spread.target);
              for (const auto &elem : arrLit.elements) {
                if (elem) {
                  compileExpression(*elem);
                  totalArgs++;
                }
              }
            } else {
              compileExpression(*arg);
              totalArgs++;
            }
          } else {
            compileExpression(*arg);
            totalArgs++;
          }
        }
        if (hasKwargs) {
          emit(OpCode::OBJECT_NEW);
          for (const auto &kwarg : expression.kwargs) {
            emit(OpCode::DUP);
            { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
            compileExpression(*kwarg.value);
            emit(OpCode::OBJECT_SET);
          }
          totalArgs++;
        }
        {
          uint32_t strId = addStringConstant(callee_id.symbol);
          emit(OpCode::CALL_HOST, std::vector<Value>{
              Value::makeStringValId(strId),
              Value(totalArgs)});
        }
        return;
      }
      COMPILER_THROW("Missing lexical binding for callee: " +
                               callee_id.symbol);
    }

    if (binding->kind == ResolvedBindingKind::HostFunction) {
      // Host function - call via CALL_HOST, expanding spread args
      uint32_t totalArgs = 0;
      for (const auto &arg : expression.args) {
        if (!arg) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          totalArgs++;
          continue;
        }
        if (arg->kind == ast::NodeType::SpreadExpression) {
          const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
          if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
            const auto &arrLit = static_cast<const ast::ArrayLiteral &>(*spread.target);
            for (const auto &elem : arrLit.elements) {
              if (elem) {
                compileExpression(*elem);
                totalArgs++;
              }
            }
          } else {
            compileExpression(*arg);
            totalArgs++;
          }
        } else {
          compileExpression(*arg);
          totalArgs++;
        }
      }
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          compileExpression(*kwarg.value);
          { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
          emit(OpCode::OBJECT_SET);
        }
        totalArgs++;
      }

      {
        uint32_t strId = addStringConstant(binding->name);
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(totalArgs)});
      }
      return;
    }

    if (binding->kind == ResolvedBindingKind::Function) {
      // User-defined function - load as FunctionObject and call
      uint32_t fn_index = top_level_function_indices_by_name_[binding->name];
      uint32_t const_idx = addConstant(Value::makeFunctionObjId(fn_index));
      emit(OpCode::LOAD_CONST, const_idx);

      // Compile args, expanding spread
      uint32_t totalArgs = 0;
      for (const auto &arg : expression.args) {
        if (!arg) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          totalArgs++;
          continue;
        }
        if (arg->kind == ast::NodeType::SpreadExpression) {
          const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
          if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
            const auto &arrLit = static_cast<const ast::ArrayLiteral &>(*spread.target);
            for (const auto &elem : arrLit.elements) {
              if (elem) {
                compileExpression(*elem);
                totalArgs++;
              }
            }
          } else {
            compileExpression(*arg);
            totalArgs++;
          }
        } else {
          compileExpression(*arg);
          totalArgs++;
        }
      }
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          compileExpression(*kwarg.value);
          { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
          emit(OpCode::OBJECT_SET);
        }
        totalArgs++;
      }

      emit(OpCode::CALL, totalArgs);
      return;
    }

    if (binding->kind == ResolvedBindingKind::Global) {
      // Global variable that might contain a function - load and call
      {
        uint32_t strId = addStringConstant(binding->name);
        emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
      }

      // Compile args, expanding spread
      uint32_t totalArgs = 0;
      for (const auto &arg : expression.args) {
        if (!arg) {
          emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
          totalArgs++;
          continue;
        }
        if (arg->kind == ast::NodeType::SpreadExpression) {
          const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
          if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
            const auto &arrLit = static_cast<const ast::ArrayLiteral &>(*spread.target);
            for (const auto &elem : arrLit.elements) {
              if (elem) {
                compileExpression(*elem);
                totalArgs++;
              }
            }
          } else {
            compileExpression(*arg);
            totalArgs++;
          }
        } else {
          compileExpression(*arg);
          totalArgs++;
        }
      }
      if (hasKwargs) {
        emit(OpCode::OBJECT_NEW);
        for (const auto &kwarg : expression.kwargs) {
          compileExpression(*kwarg.value);
          { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
          emit(OpCode::OBJECT_SET);
        }
        totalArgs++;
      }

      emit(OpCode::CALL, totalArgs);
      return;
    }
  }

  // Dynamic language: compile callee expression and let runtime resolve
  compileExpression(*expression.callee);

  // Compile arguments, handling spread expressions
  uint32_t actualArgCount = 0;
  for (const auto &arg : expression.args) {
    if (!arg) {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
      actualArgCount++;
      continue;
    }
    if (arg->kind == ast::NodeType::SpreadExpression) {
      const auto &spread = static_cast<const ast::SpreadExpression &>(*arg);
      // Handle spread over array literal: expand at compile time
      if (spread.target && spread.target->kind == ast::NodeType::ArrayLiteral) {
        const auto &arrLit =
            static_cast<const ast::ArrayLiteral &>(*spread.target);
        for (const auto &elem : arrLit.elements) {
          if (elem) {
            compileExpression(*elem);
            actualArgCount++;
          }
        }
      } else {
        // Dynamic spread not yet supported - skip for now
        // TODO: Implement runtime spread expansion
        COMPILER_THROW("Spread operator in function calls only "
                                 "supports array literals for now");
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
      compileExpression(*kwarg.value);
      { uint32_t _sid = addStringConstant(kwarg.name); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      emit(OpCode::OBJECT_SET);
    }
    actualArgCount++;
  }

  // TCO: Emit TAIL_CALL if in tail position and callee is a user-defined
  // function
  if (in_tail_position_ &&
      expression.callee->kind == ast::NodeType::Identifier) {
    const auto &callee_id =
        static_cast<const ast::Identifier &>(*expression.callee);
    const auto *binding = bindingFor(callee_id);
    if (binding && (binding->kind == ResolvedBindingKind::Local ||
                    binding->kind == ResolvedBindingKind::Upvalue ||
                    binding->kind == ResolvedBindingKind::Global)) {
      emit(OpCode::TAIL_CALL, actualArgCount);
      emitted_tail_call_ = true;
      return;
    }
  }

  emit(OpCode::CALL, actualArgCount);
}

void ByteCompiler::compileIfStatement(const ast::IfStatement &statement) {
  if (!statement.condition || !statement.consequence) {
    COMPILER_THROW("Malformed if statement");
  }

  compileExpression(*statement.condition);
  uint32_t else_jump = emitJump(OpCode::JUMP_IF_FALSE);

  bool was_tail = in_tail_position_;
  bool consequence_was_tail = false;

  // Only propagate tail position when this if-statement itself is in tail
  // position.
  if (was_tail) {
    clearTailCallFlag();
    enterTailPosition();
    compileStatement(*statement.consequence);
    consequence_was_tail = wasTailCall();
    exitTailPosition();
  } else {
    compileStatement(*statement.consequence);
  }

  // If consequence didn't emit tail call, emit RETURN
  if (was_tail && !consequence_was_tail) {
    emit(OpCode::RETURN);
  }

  if (statement.alternative) {
    uint32_t end_jump = emitJump(OpCode::JUMP);
    uint32_t else_target =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(else_jump, else_target);

    bool alternative_was_tail = false;
    if (was_tail) {
      clearTailCallFlag();
      enterTailPosition();
      compileStatement(*statement.alternative);
      alternative_was_tail = wasTailCall();
      exitTailPosition();
    } else {
      compileStatement(*statement.alternative);
    }

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
}

void ByteCompiler::compileWhileStatement(const ast::WhileStatement &statement) {
  if (!statement.condition || !statement.body) {
    COMPILER_THROW("Malformed while statement");
  }

  uint32_t loop_start =
      static_cast<uint32_t>(current_function->instructions.size());

  compileExpression(*statement.condition);
  uint32_t end_jump = emitJump(OpCode::JUMP_IF_FALSE);

  // Disable tail position so body's last expression result gets POPped
  bool saved_tail = in_tail_position_;
  in_tail_position_ = false;
  compileStatement(*statement.body);
  in_tail_position_ = saved_tail;
  emit(OpCode::JUMP, loop_start);

  uint32_t loop_end =
      static_cast<uint32_t>(current_function->instructions.size());
  patchJump(end_jump, loop_end);
}

void ByteCompiler::compileDoWhileStatement(
    const ast::DoWhileStatement &statement) {
  if (!statement.condition || !statement.body) {
    COMPILER_THROW("Malformed do-while statement");
  }

  uint32_t loop_start =
      static_cast<uint32_t>(current_function->instructions.size());

  // Execute body first (do-while always executes at least once)
  // Disable tail position so body's last expression result gets POPped
  bool saved_tail = in_tail_position_;
  in_tail_position_ = false;
  compileStatement(*statement.body);
  in_tail_position_ = saved_tail;

  // Then check condition
  compileExpression(*statement.condition);
  uint32_t end_jump = emitJump(OpCode::JUMP_IF_FALSE);

  // Jump back to loop start
  emit(OpCode::JUMP, loop_start);

  // Patch end jump
  uint32_t loop_end =
      static_cast<uint32_t>(current_function->instructions.size());
  patchJump(end_jump, loop_end);
}

void ByteCompiler::compileForStatement(const ast::ForStatement &statement) {
  if (statement.iterators.empty() || !statement.iterable || !statement.body) {
    COMPILER_THROW("Malformed for statement");
  }

  bool multiVar = statement.iterators.size() > 1;

  // Get iterator variable slots
  std::vector<uint32_t> iterSlots;
  for (const auto &iter : statement.iterators) {
    uint32_t slot = declarationSlot(*iter);
    reserveLocalSlot(slot);
    iterSlots.push_back(slot);
  }

  // Compile iterable and store in temp variable: [iterable]
  compileExpression(*statement.iterable);
  emit(OpCode::STRING_PROMOTE);
  
  uint32_t iterableSlot = next_local_index++;
  reserveLocalSlot(iterableSlot);
  emit(OpCode::STORE_VAR, iterableSlot);

  // Create iterator: [iter(iterable)]
  emit(OpCode::LOAD_VAR, iterableSlot);
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
  { uint32_t _sid = addStringConstant("done"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
  emit(OpCode::OBJECT_GET);
  uint32_t end_jump = emitJump(OpCode::JUMP_IF_TRUE);

  // Iterator returns {first, second, done}:
  // - Arrays: first=index, second=value
  // - Objects: first=key, second=value
  // - Strings: first=index, second=char
  
  if (multiVar && iterSlots.size() >= 2) {
    // Multi-variable: store first and second directly
    emit(OpCode::LOAD_VAR, resultSlot);
    { uint32_t _sid = addStringConstant("first"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    emit(OpCode::STORE_VAR, iterSlots[0]);
    
    emit(OpCode::LOAD_VAR, resultSlot);
    { uint32_t _sid = addStringConstant("second"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    emit(OpCode::STORE_VAR, iterSlots[1]);
  } else {
    // Single variable: 
    // - Arrays/strings/ranges: store second (value/char/element)
    // - Objects/sets: store first (key/element)
    // Detection order:
    // 1. Check "push" (arrays have it) -> use second
    // 2. Check "upper" (strings have it) -> use second
    // 3. Check "step" (ranges have it) -> use second
    // 4. Otherwise (object/set) -> use first
    
    // First check if it's an array (has "push")
    emit(OpCode::LOAD_VAR, iterableSlot);
    { uint32_t _sid = addStringConstant("push"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    
    // If push is not null, it's an array - use second
    uint32_t isArrayJump = emitJump(OpCode::JUMP_IF_NULL);
    
    // Has push (array) - get second (value)
    emit(OpCode::LOAD_VAR, resultSlot);
    { uint32_t _sid = addStringConstant("second"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    emit(OpCode::STORE_VAR, iterSlots[0]);
    
    // Jump to end
    uint32_t endJump = emitJump(OpCode::JUMP);
    
    // Check if it's a string (has "upper")
    patchJump(isArrayJump, static_cast<uint32_t>(current_function->instructions.size()));
    emit(OpCode::LOAD_VAR, iterableSlot);
    { uint32_t _sid = addStringConstant("upper"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    
    // If upper is not null, it's a string - use second
    uint32_t isStringJump = emitJump(OpCode::JUMP_IF_NULL);
    
    // Has upper (string) - get second (character)
    emit(OpCode::LOAD_VAR, resultSlot);
    { uint32_t _sid = addStringConstant("second"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    emit(OpCode::STORE_VAR, iterSlots[0]);
    
    // Jump to end
    uint32_t stringEndJump = emitJump(OpCode::JUMP);
    
    // Check if it's a range (has "step")
    patchJump(isStringJump, static_cast<uint32_t>(current_function->instructions.size()));
    emit(OpCode::LOAD_VAR, iterableSlot);
    { uint32_t _sid = addStringConstant("step"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    
    // If step is not null, it's a range - use second
    uint32_t isRangeJump = emitJump(OpCode::JUMP_IF_NULL);
    
    // Has step (range) - get second (value)
    emit(OpCode::LOAD_VAR, resultSlot);
    { uint32_t _sid = addStringConstant("second"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    emit(OpCode::STORE_VAR, iterSlots[0]);
    
    // Jump to end
    uint32_t rangeEndJump = emitJump(OpCode::JUMP);
    
    // Otherwise (object/set) - use first (key/element)
    patchJump(isRangeJump, static_cast<uint32_t>(current_function->instructions.size()));
    emit(OpCode::LOAD_VAR, resultSlot);
    { uint32_t _sid = addStringConstant("first"); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
    emit(OpCode::OBJECT_GET);
    emit(OpCode::STORE_VAR, iterSlots[0]);
    
    patchJump(endJump, static_cast<uint32_t>(current_function->instructions.size()));
    patchJump(stringEndJump, static_cast<uint32_t>(current_function->instructions.size()));
    patchJump(rangeEndJump, static_cast<uint32_t>(current_function->instructions.size()));
  }

  // Execute body
  // Disable tail position so body's last expression result gets POPped
  {
    bool saved_tail = in_tail_position_;
    in_tail_position_ = false;
    compileStatement(*statement.body);
    in_tail_position_ = saved_tail;
  }

  // Jump back to loop start
  emit(OpCode::JUMP, loop_start);

  // Patch end jump
  uint32_t loop_end =
      static_cast<uint32_t>(current_function->instructions.size());
  patchJump(end_jump, loop_end);
}

void ByteCompiler::compileLoopStatement(const ast::LoopStatement &statement) {
  if (!statement.body) {
    COMPILER_THROW("Malformed loop statement: body is null");
  }

  // Check if this is a count-based loop: loop 5 { ... }
  if (statement.countExpr) {
    // Compile count expression
    compileExpression(*statement.countExpr);

    // Store count in temp variable
    uint32_t countSlot = next_local_index++;
    reserveLocalSlot(countSlot);
    emit(OpCode::STORE_VAR, countSlot);

    // Create counter variable starting at 0
    uint32_t counterSlot = next_local_index++;
    reserveLocalSlot(counterSlot);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(0))));
    emit(OpCode::STORE_VAR, counterSlot);

    uint32_t loop_start =
        static_cast<uint32_t>(current_function->instructions.size());

    // Check if counter < count
    emit(OpCode::LOAD_VAR, counterSlot);
    emit(OpCode::LOAD_VAR, countSlot);
    emit(OpCode::LT);
    uint32_t end_jump = emitJump(OpCode::JUMP_IF_FALSE);

    // Execute body
    compileStatement(*statement.body);

    // Increment counter
    emit(OpCode::LOAD_VAR, counterSlot);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(1))));
    emit(OpCode::ADD);
    emit(OpCode::STORE_VAR, counterSlot);

    // Jump back to loop start
    emit(OpCode::JUMP, loop_start);

    // Patch end jump
    uint32_t loop_end =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(end_jump, loop_end);
    return;
  }

  // Check if this is a condition-based loop: loop while condition { ... }
  if (statement.condition) {
    uint32_t loop_start =
        static_cast<uint32_t>(current_function->instructions.size());

    compileExpression(*statement.condition);
    uint32_t end_jump = emitJump(OpCode::JUMP_IF_FALSE);

    compileStatement(*statement.body);
    emit(OpCode::JUMP, loop_start);

    uint32_t loop_end =
        static_cast<uint32_t>(current_function->instructions.size());
    patchJump(end_jump, loop_end);
    return;
  }

  // Infinite loop: loop { ... }
  uint32_t loop_start =
      static_cast<uint32_t>(current_function->instructions.size());

  compileStatement(*statement.body);
  emit(OpCode::JUMP, loop_start);
}

void ByteCompiler::compileBlockStatement(const ast::BlockStatement &block) {
  const auto &stmts = block.body;
  if (stmts.empty()) {
    return;
  }

  // Save and clear tail position for non-last statements
  bool saved_tail = in_tail_position_;

  // Compile all but last statement (never in tail position)
  in_tail_position_ = false;
  for (size_t i = 0; i < stmts.size() - 1; i++) {
    if (!stmts[i]) {
      continue;
    }
    compileStatement(*stmts[i]);
  }

  // Last statement: inherit original tail position
  in_tail_position_ = saved_tail;
  if (stmts.back()) {
    compileStatement(*stmts.back());
  }
  in_tail_position_ = saved_tail;
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

  case ast::NodeType::ClassDeclaration: {
    const auto &classDecl =
        static_cast<const ast::ClassDeclaration &>(statement);
    // Collect class methods - these need to be compiled as functions
    // but registered with the class type at runtime
    for (const auto &method : classDecl.definition.methods) {
      if (method && method->body) {
        for (const auto &nested : method->body->body) {
          if (nested) {
            collectFunctionDeclarations(*nested, out);
          }
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
  case ast::NodeType::LoopStatement: {
    const auto &loop_statement =
        static_cast<const ast::LoopStatement &>(statement);
    if (loop_statement.countExpr) {
      collectLambdaExpressions(*loop_statement.countExpr, out);
    }
    if (loop_statement.condition) {
      collectLambdaExpressions(*loop_statement.condition, out);
    }
    if (loop_statement.body) {
      collectLambdaExpressions(*loop_statement.body, out);
    }
    break;
  }
  case ast::NodeType::DoWhileStatement: {
    const auto &doWhile_statement =
        static_cast<const ast::DoWhileStatement &>(statement);
    if (doWhile_statement.body) {
      collectLambdaExpressions(*doWhile_statement.body, out);
    }
    if (doWhile_statement.condition) {
      collectLambdaExpressions(*doWhile_statement.condition, out);
    }
    break;
  }
  case ast::NodeType::ForStatement: {
    const auto &for_statement =
        static_cast<const ast::ForStatement &>(statement);
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
  case ast::NodeType::GoStatement: {
    const auto &go_stmt =
        static_cast<const ast::GoStatement &>(statement);
    if (go_stmt.call) {
      collectLambdaExpressions(*go_stmt.call, out);
    }
    break;
  }
  case ast::NodeType::ClassDeclaration: {
    const auto &class_decl =
        static_cast<const ast::ClassDeclaration &>(statement);
    for (const auto &method : class_decl.definition.methods) {
      if (method && method->body) {
        for (const auto &nested : method->body->body) {
          if (nested) {
            collectLambdaExpressions(*nested, out);
          }
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
    for (const auto &entry : object.pairs) {
      if (entry.value) {
        collectLambdaExpressions(*entry.value, out);
      }
      if (entry.isComputedKey && entry.keyExpr) {
        collectLambdaExpressions(*entry.keyExpr, out);
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
  case ast::NodeType::MatchExpression: {
    const auto &match_expr =
        static_cast<const ast::MatchExpression &>(expression);
    // Collect from all discriminants
    for (const auto &discriminant : match_expr.discriminants) {
      if (discriminant) {
        collectLambdaExpressions(*discriminant, out);
      }
    }
    // Collect from all patterns in all cases
    for (const auto &arm : match_expr.cases) {
      for (const auto &pattern : arm.patterns) {
        if (pattern) {
          collectLambdaExpressions(*pattern, out);
        }
      }
      if (arm.guard) {
        collectLambdaExpressions(*arm.guard, out);
      }
      if (arm.result) {
        collectLambdaExpressions(*arm.result, out);
      }
    }
    if (match_expr.defaultCase) {
      collectLambdaExpressions(*match_expr.defaultCase, out);
    }
    break;
  }
  
  // Pattern types
  case ast::NodeType::OrPattern: {
    const auto &orPat = static_cast<const ast::OrPattern &>(expression);
    for (const auto &alt : orPat.alternatives) {
      if (alt) collectLambdaExpressions(*alt, out);
    }
    break;
  }
  case ast::NodeType::WildcardPattern: {
    // Wildcard pattern has no sub-expressions
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(expression);
    for (const auto &elem : arrPat.elements) {
      if (elem) collectLambdaExpressions(*elem, out);
    }
    if (arrPat.rest) collectLambdaExpressions(*arrPat.rest, out);
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(expression);
    for (const auto &prop : objPat.properties) {
      if (prop.second) collectLambdaExpressions(*prop.second, out);
    }
    break;
  }
  case ast::NodeType::SpreadPattern: {
    const auto &spreadPat = static_cast<const ast::SpreadPattern &>(expression);
    if (spreadPat.target) collectLambdaExpressions(*spreadPat.target, out);
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

std::optional<std::string> ByteCompiler::normalizeTypeAnnotation(
    const ast::TypeAnnotation *annotation) const {
  if (!annotation || !annotation->type) {
    return std::nullopt;
  }
  const auto *reference =
      dynamic_cast<const ast::TypeReference *>(annotation->type.get());
  if (!reference) {
    return std::nullopt;
  }

  std::string type_name = reference->name;
  for (char &ch : type_name) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  if (type_name.empty() || type_name == "any" || type_name == "auto" ||
      type_name == "unknown") {
    return std::nullopt;
  }
  if (type_name.size() >= 2 &&
      type_name.compare(type_name.size() - 2, 2, "[]") == 0) {
    return std::string("array");
  }
  if (type_name == "int" || type_name == "integer") {
    return std::string("int");
  }
  if (type_name == "num" || type_name == "number" || type_name == "float" ||
      type_name == "double" || type_name == "decimal") {
    return std::string("number");
  }
  if (type_name == "str" || type_name == "string") {
    return std::string("string");
  }
  if (type_name == "bool" || type_name == "boolean") {
    return std::string("bool");
  }
  if (type_name == "list" || type_name == "array" || type_name == "vector") {
    return std::string("array");
  }
  if (type_name == "obj" || type_name == "object" || type_name == "map") {
    return std::string("object");
  }
  if (type_name == "fn" || type_name == "function" || type_name == "closure") {
    return std::string("function");
  }
  if (type_name == "class") {
    return std::string("class");
  }
  if (type_name == "struct") {
    return std::string("struct");
  }

  // Custom nominal types are not enforceable yet in bytecode VM.
  return std::nullopt;
}

void ByteCompiler::emitTypeAssertionForLocal(
    const std::string &normalized_expected, uint32_t slot,
    const std::string &label) {
  const auto emitTypeEq = [&](const char *runtime_type_name) {
    emit(OpCode::LOAD_VAR, slot);
    {
      const uint32_t type_name = addStringConstant("type");
      emit(OpCode::CALL_HOST,
           std::vector<Value>{Value::makeStringValId(type_name), Value(1)});
    }
    {
      const uint32_t expected_id = addStringConstant(runtime_type_name);
      emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(expected_id)));
    }
    emit(OpCode::EQ);
  };

  if (normalized_expected == "number") {
    emitTypeEq("int");
    emitTypeEq("float");
    emit(OpCode::OR);
  } else if (normalized_expected == "function") {
    emitTypeEq("function");
    emitTypeEq("closure");
    emit(OpCode::OR);
  } else {
    emitTypeEq(normalized_expected.c_str());
  }

  const std::string message = "Type annotation mismatch for '" + label +
                              "': expected " + normalized_expected;
  {
    const uint32_t msg_id = addStringConstant(message);
    emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(msg_id)));
  }
  {
    const uint32_t assert_name = addStringConstant("assert");
    emit(OpCode::CALL_HOST,
         std::vector<Value>{Value::makeStringValId(assert_name), Value(2)});
  }
  emit(OpCode::POP);
}

const ResolvedBinding *ByteCompiler::bindingFor(const ast::Identifier &id) const {
  auto it = lexical_resolution_.identifier_bindings.find(&id);
  if (it == lexical_resolution_.identifier_bindings.end()) {
    return nullptr;
  }
  return &it->second;
}

uint32_t ByteCompiler::effectiveSlot(uint32_t slot) const {
  return slot + local_slot_offset_;
}

uint32_t ByteCompiler::declarationSlot(const ast::Identifier &id) const {
  auto it = lexical_resolution_.declaration_slots.find(&id);
  if (it == lexical_resolution_.declaration_slots.end()) {
    COMPILER_THROW("Missing declaration slot for: " + id.symbol);
  }
  return effectiveSlot(it->second);
}

void ByteCompiler::reserveLocalSlot(uint32_t slot) {
  if (slot >= next_local_index) {
    next_local_index = slot + 1;
  }
}

void ByteCompiler::enterFunction(BytecodeFunction &&function,
                                 std::optional<uint32_t> slot) {
  // Support nested functions by saving current function context
  if (current_function) {
    // Save current function state for nesting
    saved_functions_.push_back(
        std::make_pair(std::move(current_function), current_function_slot_));
  }

  current_function = std::make_unique<BytecodeFunction>(std::move(function));
  current_function_slot_ = slot;
  resetLocals();
}

void ByteCompiler::leaveFunction() {
  if (!current_function) {
    COMPILER_THROW("No active function to close");
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

  // Restore previous function context if nested
  if (!saved_functions_.empty()) {
    auto saved = std::move(saved_functions_.back());
    saved_functions_.pop_back();
    current_function = std::move(saved.first);
    current_function_slot_ = saved.second;
  } else {
    resetLocals();
  }
}

void ByteCompiler::resetLocals() { next_local_index = 0; }

// Tail call optimization - track tail position context
void ByteCompiler::enterTailPosition() { in_tail_position_ = true; }

void ByteCompiler::exitTailPosition() { in_tail_position_ = false; }

bool ByteCompiler::isInTailPosition() const { return in_tail_position_; }

bool ByteCompiler::wasTailCall() const { return emitted_tail_call_; }

void ByteCompiler::clearTailCallFlag() { emitted_tail_call_ = false; }

// Compile when block: when condition { statements }
void ByteCompiler::compileWhenBlock(const ast::WhenBlock &whenBlock) {
  // Compile the condition expression
  if (whenBlock.condition) {
    compileExpression(*whenBlock.condition);
  } else {
    emit(OpCode::LOAD_CONST,
         Value::makeBool(true)); // Default to true if no condition
  }

  // Store condition result
  uint32_t condSlot = next_local_index++;
  reserveLocalSlot(condSlot);
  emit(OpCode::STORE_VAR, condSlot);

  // Jump to end if condition is false
  uint32_t endJump = emitJump(OpCode::JUMP_IF_FALSE);

  // Compile statements in the when block
  for (const auto &stmt : whenBlock.statements) {
    if (stmt) {
      compileStatement(*stmt);
    }
  }

  // Patch the jump
  patchJump(endJump,
            static_cast<uint32_t>(current_function->instructions.size()));
}

// Compile hotkey binding: hotkey => action
void ByteCompiler::compileHotkeyBinding(const ast::HotkeyBinding &binding) {
  // For each hotkey in the binding
  for (const auto &hotkeyExpr : binding.hotkeys) {
    if (!hotkeyExpr)
      continue;

    // Create a function that wraps the action with @ context injection
    // The action will receive @ as its first parameter
    BytecodeFunction hotkeyActionFn("hotkey_action");
    enterFunction(std::move(hotkeyActionFn));

    // Compile the action statement/expression
    if (binding.action) {
      if (auto *blockStmt =
              dynamic_cast<const ast::BlockStatement *>(binding.action.get())) {
        // Block statement - compile all statements
        for (const auto &stmt : blockStmt->body) {
          compileStatement(*stmt);
        }
      } else if (auto *exprStmt =
                     dynamic_cast<const ast::ExpressionStatement *>(
                         binding.action.get())) {
        // Single expression - compile and return
        compileExpression(*exprStmt->expression);
        emit(OpCode::RETURN);
      } else {
        // Other statement types
        compileStatement(*binding.action);
        emit(OpCode::RETURN);
      }
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(0))));
    }

    leaveFunction();
    
    // Store hotkey_action to globals so it can be loaded at runtime
    {
      uint32_t strId = addStringConstant("hotkey_action");
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeFunctionObjId(
               static_cast<uint32_t>(compiled_functions.size() - 1))));
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
    }

    // Create a wrapper that injects the @ context
    // This wrapper receives the hotkey context as first parameter and passes it
    // to the action
    BytecodeFunction hotkeyWrapperFn("hotkey_wrapper");
    enterFunction(std::move(hotkeyWrapperFn));

    // If there's a condition expression, compile and check it first
    uint32_t skipActionJump = 0;
    if (binding.conditionExpr) {
      // Compile the condition expression
      compileExpression(*binding.conditionExpr);

      // If condition is false, skip the action
      skipActionJump = emitJump(OpCode::JUMP_IF_FALSE);

      // Pop the condition result (we don't need it anymore)
      emit(OpCode::POP);
    }

    // Load the hotkey action function
    {
      uint32_t strId = addStringConstant("hotkey_action");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
    }

    // Create hotkey context object
    {
      uint32_t strId = addStringConstant("Hotkey");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(static_cast<uint32_t>(1))});
    }

    // Store hotkey context as 'this' so @field/@directive works in hotkey blocks
    {
      uint32_t strId = addStringConstant("this");
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
      // Reload 'this' for the CALL (STORE_GLOBAL consumes the value)
      uint32_t strId2 = addStringConstant("this");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId2));
    }

    // Call the action with @ context as parameter
    emit(OpCode::CALL, static_cast<uint32_t>(1)); // Call with 1 arg (@ context)

    // Pop the result (action result)
    emit(OpCode::POP);

    // If we had a condition, patch the skip jump to here
    if (binding.conditionExpr && skipActionJump > 0) {
      patchJump(skipActionJump,
                static_cast<uint32_t>(current_function->instructions.size()));
    }

    leaveFunction();
    
    // Store hotkey_wrapper to globals so it can be loaded at runtime
    {
      uint32_t strId = addStringConstant("hotkey_wrapper");
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeFunctionObjId(
               static_cast<uint32_t>(compiled_functions.size() - 1))));
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
    }

    // Register the hotkey with the wrapper function
    // First, compile the hotkey string
    compileExpression(*hotkeyExpr);
    
    // Load the hotkey_wrapper function from globals
    {
      uint32_t strId = addStringConstant("hotkey_wrapper");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
    }

    // Call hotkey.register with (hotkey_string, wrapper_function)
    {
      uint32_t strId = addStringConstant("hotkey.register");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(static_cast<uint32_t>(2))});
    }
    emit(OpCode::POP); // Discard result

    // Handle conditions if present (for when blocks)
    if (!binding.conditions.empty()) {
      // Conditions are handled by ConditionalHotkeyManager
      // For now, just register the hotkey and let the conditional manager
      // handle it
    }
  }
}

// Compile hotkey binding as expression (returns context object on stack)
// This is a variant of compileHotkeyBinding that leaves the result on the stack
// instead of popping it, so it can be used as an assignment RHS.
void ByteCompiler::compileHotkeyBindingExpr(const ast::HotkeyBinding &binding) {
  // For each hotkey in the binding
  for (const auto &hotkeyExpr : binding.hotkeys) {
    if (!hotkeyExpr)
      continue;

    // Create a function that wraps the action with @ context injection
    BytecodeFunction hotkeyActionFn("hotkey_action");
    enterFunction(std::move(hotkeyActionFn));

    if (binding.action) {
      if (auto *blockStmt =
              dynamic_cast<const ast::BlockStatement *>(binding.action.get())) {
        for (const auto &stmt : blockStmt->body) {
          compileStatement(*stmt);
        }
      } else if (auto *exprStmt =
                     dynamic_cast<const ast::ExpressionStatement *>(
                         binding.action.get())) {
        compileExpression(*exprStmt->expression);
        emit(OpCode::RETURN);
      } else {
        compileStatement(*binding.action);
        emit(OpCode::RETURN);
      }
    } else {
      emit(OpCode::LOAD_CONST, addConstant(Value::makeInt(static_cast<int64_t>(0))));
    }

    leaveFunction();

    // Store hotkey_action to globals
    {
      uint32_t strId = addStringConstant("hotkey_action");
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeFunctionObjId(
               static_cast<uint32_t>(compiled_functions.size() - 1))));
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
    }

    // Create wrapper
    BytecodeFunction hotkeyWrapperFn("hotkey_wrapper");
    enterFunction(std::move(hotkeyWrapperFn));

    uint32_t skipActionJump = 0;
    if (binding.conditionExpr) {
      compileExpression(*binding.conditionExpr);
      skipActionJump = emitJump(OpCode::JUMP_IF_FALSE);
      emit(OpCode::POP);
    }

    // Load hotkey_action
    {
      uint32_t strId = addStringConstant("hotkey_action");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
    }

    // Create hotkey context object
    {
      uint32_t strId = addStringConstant("Hotkey");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(static_cast<uint32_t>(1))});
    }

    // Store as 'this' for @field/@directive access
    {
      uint32_t strId = addStringConstant("this");
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
      uint32_t strId2 = addStringConstant("this");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId2));
    }

    // Call action with @ context
    emit(OpCode::CALL, static_cast<uint32_t>(1));
    emit(OpCode::POP);

    if (binding.conditionExpr && skipActionJump > 0) {
      patchJump(skipActionJump,
                static_cast<uint32_t>(current_function->instructions.size()));
    }

    leaveFunction();

    // Store wrapper to globals
    {
      uint32_t strId = addStringConstant("hotkey_wrapper");
      emit(OpCode::LOAD_CONST,
           addConstant(Value::makeFunctionObjId(
               static_cast<uint32_t>(compiled_functions.size() - 1))));
      emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
    }

    // Register the hotkey - result (context object) stays on stack
    compileExpression(*hotkeyExpr);
    {
      uint32_t strId = addStringConstant("hotkey_wrapper");
      emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
    }
    {
      uint32_t strId = addStringConstant("hotkey.register");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId),
          Value(static_cast<uint32_t>(2))});
    }
    // DON'T POP - leave context object on stack for assignment
  }
}

// Compile input statement: > "text" or > {Enter}
void ByteCompiler::compileInputStatement(const ast::InputStatement &statement) {
  // Each input command becomes a host function call to io.send or similar
  for (const auto &cmd : statement.commands) {
    switch (cmd.type) {
    case ast::InputCommand::SendText:
      // io.send(cmd.text)
      { uint32_t _sid = addStringConstant(cmd.text); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      {
        uint32_t strId = addStringConstant("io.send");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(1))});
      }
      break;
    case ast::InputCommand::SendKey:
      // io.sendKey(cmd.key)
      { uint32_t _sid = addStringConstant(cmd.key); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      {
        uint32_t strId = addStringConstant("io.sendKey");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(1))});
      }
      break;
    case ast::InputCommand::MouseClick:
      // io.mouseClick(cmd.text) - text contains button name
      { uint32_t _sid = addStringConstant(cmd.text); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      {
        uint32_t strId = addStringConstant("io.mouseClick");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(1))});
      }
      break;
    case ast::InputCommand::MouseMove:
      // io.mouseMove(x, y) - xExprStr and yExprStr contain coordinates
      // For now, treat as strings and evaluate them
      // TODO: Properly evaluate expressions
      { uint32_t _sid = addStringConstant(cmd.xExprStr); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      { uint32_t _sid = addStringConstant(cmd.yExprStr); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      {
        uint32_t strId = addStringConstant("io.mouseMove");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(2))});
      }
      break;
    case ast::InputCommand::MouseRelative:
      // Similar to MouseMove but relative
      { uint32_t _sid = addStringConstant(cmd.xExprStr); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      { uint32_t _sid = addStringConstant(cmd.yExprStr); emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid))); };
      {
        uint32_t strId = addStringConstant("io.mouseMoveRel");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(2))});
      }
      break;
    case ast::InputCommand::Sleep:
      // sleep_ms(cmd.duration)
      emit(OpCode::LOAD_CONST, Value::makeInt(std::stoi(cmd.duration)));
      {
        uint32_t strId = addStringConstant("sleep_ms");
        emit(OpCode::CALL_HOST, std::vector<Value>{
            Value::makeStringValId(strId),
            Value(static_cast<uint32_t>(1))});
      }
      break;
    default:
      // Other commands not yet implemented
      break;
    }
  }
}

// Compile shell command: $ cmd or $! cmd
void ByteCompiler::compileShellCommandStatement(const ast::ShellCommandStatement &statement) {
  if (!statement.commandExpr) {
    COMPILER_THROW("Shell command missing expression");
  }

  // Compile the command expression
  compileExpression(*statement.commandExpr);

  // Call run() or runCapture() host function
  const char *funcName = statement.captureOutput ? "runCapture" : "run";
  uint32_t strId = addStringConstant(funcName);
  emit(OpCode::CALL_HOST, std::vector<Value>{
      Value::makeStringValId(strId),
      Value(static_cast<uint32_t>(1))});
  emit(OpCode::POP);

  // Handle pipe chain: $! cmd1 | cmd2 | cmd3
  const ast::ShellCommandStatement *next = statement.next.get();
  while (next) {
    if (next->commandExpr) {
      compileExpression(*next->commandExpr);
      uint32_t strId2 = addStringConstant("run");
      emit(OpCode::CALL_HOST, std::vector<Value>{
          Value::makeStringValId(strId2),
          Value(static_cast<uint32_t>(1))});
      emit(OpCode::POP);
    }
    next = next->next.get();
  }
}

void ByteCompiler::compileWaitStatement(const ast::WaitStatement &statement) {
  if (statement.condition) {
    // Wait statement: w condition
    // Compiled as a loop that checks condition and sleeps a bit
    uint32_t startLabel =
        static_cast<uint32_t>(current_function->instructions.size());
    compileExpression(*statement.condition);
    uint32_t jumpToEnd = emitJump(OpCode::JUMP_IF_TRUE);

    // Sleep a bit (10ms) to avoid high CPU usage
    {
      uint32_t _sid = addStringConstant("10ms");
      emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(_sid)));
    };
    {
      uint32_t strId = addStringConstant("sleep");
      emit(OpCode::CALL_HOST, std::vector<Value>{Value::makeStringValId(strId),
                                                 Value(static_cast<uint32_t>(1))});
    }
    emit(OpCode::POP); // discard sleep result

    emit(OpCode::JUMP, startLabel);

    patchJump(jumpToEnd,
              static_cast<uint32_t>(current_function->instructions.size()));
  }
}

void ByteCompiler::compileGetInputExpression(
    const ast::GetInputExpression &expression) {
  // < source (e.g., < clipboard)
  // Compiled as call to io.getClipboard() or similar
  std::string fnName = "io.get" + expression.source;
  // Capitalize first letter of source if needed for camelCase
  if (!expression.source.empty()) {
    std::string source = expression.source;
    source[0] = std::toupper(source[0]);
    fnName = "io.get" + source;
  }

  // Special case for prompt
  uint32_t argCount = 0;
  if (expression.prompt) {
    compileExpression(*expression.prompt);
    argCount = 1;
  }

  uint32_t strId = addStringConstant(fnName);
  emit(OpCode::CALL_HOST, std::vector<Value>{Value::makeStringValId(strId),
                                             Value(argCount)});
}

// ============================================================================
// CONCURRENCY & COROUTINE COMPILATION
// ============================================================================

void ByteCompiler::compileThreadExpression(const ast::ThreadExpression &expression) {
  // thread { ... } -> spawn a new thread with the body as a function

  if (!expression.body) {
    COMPILER_THROW("Thread expression missing body");
  }

  compileClosureBody(*expression.body, "<thread>");

  // Emit CALL_HOST to thread.spawn
  uint32_t strId = addStringConstant("thread.spawn");
  emit(OpCode::CALL_HOST, std::vector<Value>{
      Value::makeStringValId(strId),
      Value(static_cast<uint32_t>(1))});
}

void ByteCompiler::compileIntervalExpression(const ast::IntervalExpression &expression) {
  // interval <ms> { ... } -> start a repeating timer

  if (!expression.intervalMs) {
    COMPILER_THROW("Interval expression missing duration");
  }

  if (!expression.body) {
    COMPILER_THROW("Interval expression missing body");
  }

  // Compile the interval duration
  compileExpression(*expression.intervalMs);

  compileClosureBody(*expression.body, "<interval>");

  // Emit CALL_HOST to interval.start
  uint32_t strId = addStringConstant("interval.start");
  emit(OpCode::CALL_HOST, std::vector<Value>{
      Value::makeStringValId(strId),
      Value(static_cast<uint32_t>(2))});
}

void ByteCompiler::compileTimeoutExpression(const ast::TimeoutExpression &expression) {
  // timeout <ms> { ... } -> start a one-shot timer

  if (!expression.delayMs) {
    COMPILER_THROW("Timeout expression missing duration");
  }

  if (!expression.body) {
    COMPILER_THROW("Timeout expression missing body");
  }

  // Compile the delay duration
  compileExpression(*expression.delayMs);

  compileClosureBody(*expression.body, "<timeout>");

  // Emit CALL_HOST to timeout.start
  uint32_t strId = addStringConstant("timeout.start");
  emit(OpCode::CALL_HOST, std::vector<Value>{
      Value::makeStringValId(strId),
      Value(static_cast<uint32_t>(2))});
}

// Compile a closure body, resolving identifiers against the enclosing function's scope
void ByteCompiler::compileClosureBody(const ast::Statement &body, const std::string &name) {
  // Collect all identifiers in the body that reference enclosing scope variables
  std::vector<UpvalueDescriptor> upvalues;
  collectUpvaluesFromBody(body, upvalues);

  uint32_t funcIndex = compiled_functions.size();
  BytecodeFunction bf(name, 0, 0);
  bf.upvalues = std::move(upvalues);

  enterFunction(std::move(bf), funcIndex);

  compileStatement(body);

  emit(OpCode::RETURN);
  leaveFunction();

  // Load the function object
  emit(OpCode::LOAD_CONST, addConstant(Value::makeFunctionObjId(funcIndex)));
}

void ByteCompiler::collectUpvaluesFromBody(const ast::Statement &stmt, std::vector<UpvalueDescriptor> &upvalues) {
  switch (stmt.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &es = static_cast<const ast::ExpressionStatement &>(stmt);
    if (es.expression) collectUpvaluesFromExpr(*es.expression, upvalues);
    break;
  }
  case ast::NodeType::LetDeclaration: {
    const auto &let = static_cast<const ast::LetDeclaration &>(stmt);
    if (let.value) collectUpvaluesFromExpr(*let.value, upvalues);
    break;
  }
  case ast::NodeType::IfStatement: {
    const auto &ifStmt = static_cast<const ast::IfStatement &>(stmt);
    if (ifStmt.condition) collectUpvaluesFromExpr(*ifStmt.condition, upvalues);
    if (ifStmt.consequence) collectUpvaluesFromBody(*ifStmt.consequence, upvalues);
    if (ifStmt.alternative) collectUpvaluesFromBody(*ifStmt.alternative, upvalues);
    break;
  }
  case ast::NodeType::BlockStatement: {
    const auto &block = static_cast<const ast::BlockStatement &>(stmt);
    for (const auto &s : block.body) {
      if (s) collectUpvaluesFromBody(*s, upvalues);
    }
    break;
  }
  case ast::NodeType::ReturnStatement: {
    const auto &ret = static_cast<const ast::ReturnStatement &>(stmt);
    if (ret.argument) collectUpvaluesFromExpr(*ret.argument, upvalues);
    break;
  }
  default:
    break;
  }
}

void ByteCompiler::collectUpvaluesFromExpr(const ast::Expression &expr, std::vector<UpvalueDescriptor> &upvalues) {
  if (expr.kind == ast::NodeType::Identifier) {
    const auto &id = static_cast<const ast::Identifier &>(expr);
    // First try direct binding lookup
    const auto *binding = bindingFor(id);
    if (binding && (binding->kind == ResolvedBindingKind::Local ||
                     binding->kind == ResolvedBindingKind::Upvalue)) {
      bool found = false;
      for (const auto &uv : upvalues) {
        if (uv.index == binding->slot && uv.captures_local == (binding->kind == ResolvedBindingKind::Local)) {
          found = true;
          break;
        }
      }
      if (!found) {
        upvalues.push_back({binding->slot, binding->kind == ResolvedBindingKind::Local});
      }
      return;
    }
    // No direct binding — search by name in enclosing scope
    for (const auto &[idNode, bnd] : lexical_resolution_.identifier_bindings) {
      if (idNode && idNode->symbol == id.symbol) {
        if (bnd.kind == ResolvedBindingKind::Local || bnd.kind == ResolvedBindingKind::Upvalue) {
          bool found = false;
          for (const auto &uv : upvalues) {
            if (uv.index == bnd.slot && uv.captures_local == (bnd.kind == ResolvedBindingKind::Local)) {
              found = true;
              break;
            }
          }
          if (!found) {
            upvalues.push_back({bnd.slot, bnd.kind == ResolvedBindingKind::Local});
          }
          return;
        }
      }
    }
    return;
  }
  // Recurse into sub-expressions
  switch (expr.kind) {
  case ast::NodeType::BinaryExpression: {
    const auto &be = static_cast<const ast::BinaryExpression &>(expr);
    if (be.left) collectUpvaluesFromExpr(*be.left, upvalues);
    if (be.right) collectUpvaluesFromExpr(*be.right, upvalues);
    break;
  }
  case ast::NodeType::CallExpression: {
    const auto &ce = static_cast<const ast::CallExpression &>(expr);
    if (ce.callee) collectUpvaluesFromExpr(*ce.callee, upvalues);
    for (const auto &arg : ce.args) {
      if (arg) collectUpvaluesFromExpr(*arg, upvalues);
    }
    for (const auto &kw : ce.kwargs) {
      if (kw.value) collectUpvaluesFromExpr(*kw.value, upvalues);
    }
    break;
  }
  case ast::NodeType::LambdaExpression: {
    // Nested lambdas handle their own upvalues
    break;
  }
  case ast::NodeType::ThreadExpression:
  case ast::NodeType::IntervalExpression:
  case ast::NodeType::TimeoutExpression: {
    // These handle their own upvalues recursively
    break;
  }
  default:
    break;
  }
}

void ByteCompiler::compileYieldExpression(const ast::YieldExpression &expression) {
  // yield or yield(value) or yield(ms)
  // For now, just push the value (coroutine yield is a VM-level operation)
  
  if (expression.value) {
    compileExpression(*expression.value);
  } else {
    // No value - push null
    emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
  }
  
  // Yield is a VM-level coroutine operation, not a host function
  // Keep the YIELD opcode for now
  emit(OpCode::YIELD);
}

void ByteCompiler::compileGoStatement(const ast::GoStatement &statement) {
  // go func() -> async function call
  // For now, just compile the call expression (go is a statement modifier)
  
  if (!statement.call) {
    COMPILER_THROW("Go statement missing call expression");
  }
  
  // Compile the call expression
  compileExpression(*statement.call);
  
  // Pop the result (go statements don't use the return value)
  emit(OpCode::POP);
  
  // Note: go is a statement modifier that makes the call asynchronous
  // The actual async execution is handled by the VM or runtime
  // For now, this is a placeholder - a full implementation would
  // require integration with the thread pool
}

void ByteCompiler::compileGoExpression(const ast::GoExpression &expression) {
  // go expr -> call thread_spawn(expr)
  // Returns a thread object with .join(), .send() methods
  
  if (!expression.call) {
    COMPILER_THROW("Go expression missing call expression");
  }
  
  // Compile the expression/function being spawned
  compileExpression(*expression.call);
  
  // Call the host function thread_spawn with the function as argument
  uint32_t strId = addStringConstant("thread_spawn");
  emit(OpCode::CALL_HOST, std::vector<Value>{
      Value::makeStringValId(strId),
      Value(static_cast<uint32_t>(1))  // 1 argument
  });
}

// Compile del target: del variable, del obj.field, del arr[index], del set[key]
void ByteCompiler::compileDelTarget(const ast::Expression &target) {
  switch (target.kind) {
    case ast::NodeType::Identifier: {
      const auto &id = static_cast<const ast::Identifier &>(target);
      const auto *binding = bindingFor(id);
      if (binding && binding->kind == ResolvedBindingKind::Local) {
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        emit(OpCode::STORE_VAR, effectiveSlot(binding->slot));
      } else if (binding && binding->kind == ResolvedBindingKind::Upvalue) {
        COMPILER_THROW("Cannot del upvalue binding");
      } else {
        // No binding or global - delete from globals by setting to null
        uint32_t strId = addStringConstant(id.symbol);
        emit(OpCode::LOAD_CONST, addConstant(Value::makeNull()));
        emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
      }
      break;
    }
    case ast::NodeType::MemberExpression: {
      const auto &member = static_cast<const ast::MemberExpression &>(target);
      compileExpression(*member.object);
      if (member.property && member.property->kind == ast::NodeType::Identifier) {
        const auto &prop = static_cast<const ast::Identifier &>(*member.property);
        uint32_t strId = addStringConstant(prop.symbol);
        emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(strId)));
        emit(OpCode::OBJECT_DELETE);
      }
      break;
    }
    case ast::NodeType::IndexExpression: {
      const auto &idx = static_cast<const ast::IndexExpression &>(target);
      compileExpression(*idx.object);
      compileExpression(*idx.index);
      // Dispatch to ARRAY_DEL or SET_DEL at runtime based on container type
      emit(OpCode::ARRAY_DEL);
      break;
    }
    default:
      COMPILER_THROW("del target must be identifier, member, or index expression");
  }
}

void ByteCompiler::compileChannelExpression(const ast::ChannelExpression &expression) {
  // channel() -> create a new channel
  // Emit CALL_HOST to channel.new
  
  (void)expression; // Unused parameter
  
  // Emit CALL_HOST to channel.new
  uint32_t strId = addStringConstant("channel.new");
  emit(OpCode::CALL_HOST, std::vector<Value>{
      Value::makeStringValId(strId),
      Value(static_cast<uint32_t>(0))});
}

// Phase 3B-3: Generator Detection Implementation
bool ByteCompiler::functionContainsYield(const ast::BlockStatement &body) const {
  // BlockStatement stores statements in `body` field (vector of unique_ptr<Statement>)
  for (const auto &stmt : body.body) {
    if (stmt && statementContainsYield(*stmt)) {
      return true;
    }
  }
  return false;
}

bool ByteCompiler::statementContainsYield(const ast::Statement &stmt) const {
  switch (stmt.kind) {
    case ast::NodeType::ExpressionStatement: {
      const auto &expr_stmt = static_cast<const ast::ExpressionStatement &>(stmt);
      return expr_stmt.expression && expressionContainsYield(*expr_stmt.expression);
    }
    
    case ast::NodeType::BlockStatement: {
      const auto &block = static_cast<const ast::BlockStatement &>(stmt);
      return functionContainsYield(block);
    }
    
    case ast::NodeType::IfStatement: {
      const auto &if_stmt = static_cast<const ast::IfStatement &>(stmt);
      if (if_stmt.condition && expressionContainsYield(*if_stmt.condition)) {
        return true;
      }
      if (if_stmt.consequence && statementContainsYield(*if_stmt.consequence)) {
        return true;
      }
      if (if_stmt.alternative && statementContainsYield(*if_stmt.alternative)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::WhileStatement: {
      const auto &while_stmt = static_cast<const ast::WhileStatement &>(stmt);
      if (while_stmt.condition && expressionContainsYield(*while_stmt.condition)) {
        return true;
      }
      if (while_stmt.body && statementContainsYield(*while_stmt.body)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::DoWhileStatement: {
      const auto &do_while = static_cast<const ast::DoWhileStatement &>(stmt);
      if (do_while.body && statementContainsYield(*do_while.body)) {
        return true;
      }
      if (do_while.condition && expressionContainsYield(*do_while.condition)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::ForStatement: {
      const auto &for_stmt = static_cast<const ast::ForStatement &>(stmt);
      if (for_stmt.iterable && expressionContainsYield(*for_stmt.iterable)) {
        return true;
      }
      if (for_stmt.body && statementContainsYield(*for_stmt.body)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::LoopStatement: {
      const auto &loop = static_cast<const ast::LoopStatement &>(stmt);
      if (loop.condition && expressionContainsYield(*loop.condition)) {
        return true;
      }
      if (loop.countExpr && expressionContainsYield(*loop.countExpr)) {
        return true;
      }
      if (loop.body && statementContainsYield(*loop.body)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::TryExpression: {
      const auto &try_expr = static_cast<const ast::TryExpression &>(stmt);
      if (try_expr.tryBody && statementContainsYield(*try_expr.tryBody)) {
        return true;
      }
      if (try_expr.catchBody && statementContainsYield(*try_expr.catchBody)) {
        return true;
      }
      if (try_expr.finallyBlock && statementContainsYield(*try_expr.finallyBlock)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::HotkeyBinding: {
      const auto &hotkey = static_cast<const ast::HotkeyBinding &>(stmt);
      if (hotkey.action && statementContainsYield(*hotkey.action)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::WaitStatement: {
      const auto &wait = static_cast<const ast::WaitStatement &>(stmt);
      return wait.condition && expressionContainsYield(*wait.condition);
    }
    
    // Phase 3B-3: FunctionDeclaration inside generators should recursively check if nested function contains yield
    // Note: We check if the IMMEDIATE parent function contains yield, not nested ones
    // Nested functions are compiled separately and get their own is_generator flag
    case ast::NodeType::FunctionDeclaration:
    case ast::NodeType::ClassDeclaration:
    case ast::NodeType::EnumDeclaration:
      // These don't make the parent a generator - each function/class has its own compilation
      return false;
    
    default:
      return false;
  }
}

bool ByteCompiler::expressionContainsYield(const ast::Expression &expr) const {
  // First check if this is a yield expression directly
  if (expr.kind == ast::NodeType::YieldExpression) {
    return true;
  }
  
  // Then check nested expressions recursively
  switch (expr.kind) {
    case ast::NodeType::BinaryExpression: {
      const auto &binary = static_cast<const ast::BinaryExpression &>(expr);
      return (binary.left && expressionContainsYield(*binary.left)) ||
             (binary.right && expressionContainsYield(*binary.right));
    }
    
    case ast::NodeType::UnaryExpression: {
      const auto &unary = static_cast<const ast::UnaryExpression &>(expr);
      return unary.operand && expressionContainsYield(*unary.operand);
    }
    
    case ast::NodeType::CallExpression: {
      const auto &call = static_cast<const ast::CallExpression &>(expr);
      if (call.callee && expressionContainsYield(*call.callee)) {
        return true;
      }
      // Check positional arguments
      for (const auto &arg : call.args) {
        if (arg && expressionContainsYield(*arg)) {
          return true;
        }
      }
      // Check keyword arguments
      for (const auto &[key, kwarg] : call.kwargs) {
        if (kwarg && expressionContainsYield(*kwarg)) {
          return true;
        }
      }
      return false;
    }
    
    case ast::NodeType::IfExpression: {
      const auto &if_expr = static_cast<const ast::IfExpression &>(expr);
      if (if_expr.condition && expressionContainsYield(*if_expr.condition)) {
        return true;
      }
      if (if_expr.thenBranch && expressionContainsYield(*if_expr.thenBranch)) {
        return true;
      }
      if (if_expr.elseBranch && expressionContainsYield(*if_expr.elseBranch)) {
        return true;
      }
      return false;
    }
    
    case ast::NodeType::ArrayLiteral: {
      const auto &array = static_cast<const ast::ArrayLiteral &>(expr);
      for (const auto &elem : array.elements) {
        if (elem && expressionContainsYield(*elem)) {
          return true;
        }
      }
      return false;
    }
    
    case ast::NodeType::ObjectLiteral: {
      const auto &obj = static_cast<const ast::ObjectLiteral &>(expr);
      for (const auto &entry : obj.pairs) {
        if (entry.value && expressionContainsYield(*entry.value)) {
          return true;
        }
        if (entry.isComputedKey && entry.keyExpr && expressionContainsYield(*entry.keyExpr)) {
          return true;
        }
      }
      return false;
    }
    
    case ast::NodeType::MemberExpression: {
      const auto &member = static_cast<const ast::MemberExpression &>(expr);
      return member.object && expressionContainsYield(*member.object);
    }
    
    case ast::NodeType::IndexExpression: {
      const auto &index = static_cast<const ast::IndexExpression &>(expr);
      return (index.object && expressionContainsYield(*index.object)) ||
             (index.index && expressionContainsYield(*index.index));
    }
    
    case ast::NodeType::LambdaExpression: {
      // Lambdas are separate functions, don't inherit outer function's yield detection
      return false;
    }
    
    case ast::NodeType::TryExpression: {
      // Note: TryExpression is an expression, not a statement
      // It may contain statements, but we need to be careful with the structure
      // For now, conservatively assume it might contain yield
      // (the actual structure should be checked in AST.h)
      return true; // TODO: Implement proper TryExpression yield detection
    }
    
    default:
      return false;
  }
}

} // namespace havel::compiler
