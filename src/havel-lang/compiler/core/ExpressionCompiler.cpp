#include "havel-lang/errors/ErrorSystem.h"
#include "ExpressionCompiler.hpp"
#include "havel-lang/ast/AST.h"
#include <stdexcept>

// Macro for throwing errors with source location info
#define COMPILER_THROW(msg) \
  do { \
    ::havel::errors::ErrorReporter::instance().report( \
        HAVEL_ERROR(::havel::errors::ErrorStage::Compiler, msg)); \
    throw std::runtime_error(std::string(msg) + " [" __FILE__ ":" + std::to_string(__LINE__) + "]"); \
  } while (0)

namespace havel::compiler {

ExpressionCompiler::ExpressionCompiler(
    CodeEmitter& emitter,
    BindingResolver& bindingResolver,
    const LexicalResolutionResult& lexicalResult)
    : emitter_(emitter),
      bindingResolver_(bindingResolver),
      lexicalResult_(lexicalResult) {}

void ExpressionCompiler::compile(const ast::Expression& expression) {
  switch (expression.kind) {
    case ast::NodeType::Identifier:
      compileIdentifier(static_cast<const ast::Identifier&>(expression));
      break;
    case ast::NodeType::BinaryExpression:
      compileBinaryExpression(static_cast<const ast::BinaryExpression&>(expression));
      break;
    case ast::NodeType::UnaryExpression:
      compileUnaryExpression(static_cast<const ast::UnaryExpression&>(expression));
      break;
    case ast::NodeType::CallExpression:
      compileCallExpression(static_cast<const ast::CallExpression&>(expression));
      break;
    case ast::NodeType::AssignmentExpression:
      compileAssignmentExpression(static_cast<const ast::AssignmentExpression&>(expression));
      break;
    case ast::NodeType::MemberExpression:
      compileMemberExpression(static_cast<const ast::MemberExpression&>(expression));
      break;
    case ast::NodeType::IndexExpression:
      compileIndexExpression(static_cast<const ast::IndexExpression&>(expression));
      break;
    case ast::NodeType::LambdaExpression:
      compileLambdaExpression(static_cast<const ast::LambdaExpression&>(expression));
      break;
    case ast::NodeType::ArrayLiteral:
      compileArrayLiteral(static_cast<const ast::ArrayLiteral&>(expression));
      break;
    case ast::NodeType::ObjectLiteral:
      compileObjectLiteral(static_cast<const ast::ObjectLiteral&>(expression));
      break;
    case ast::NodeType::TernaryExpression:
      compileTernaryExpression(static_cast<const ast::TernaryExpression&>(expression));
      break;
    case ast::NodeType::UpdateExpression:
      compileUpdateExpression(static_cast<const ast::UpdateExpression&>(expression));
      break;
    case ast::NodeType::AwaitExpression:
      compileAwaitExpression(static_cast<const ast::AwaitExpression&>(expression));
      break;
    case ast::NodeType::SpreadExpression:
      compileSpreadExpression(static_cast<const ast::SpreadExpression&>(expression));
      break;
    case ast::NodeType::RangeExpression:
      compileRangeExpression(static_cast<const ast::RangeExpression&>(expression));
      break;
    case ast::NodeType::PipelineExpression:
      compilePipelineExpression(static_cast<const ast::PipelineExpression&>(expression));
      break;
    case ast::NodeType::InterpolatedStringExpression:
      compileInterpolatedString(static_cast<const ast::InterpolatedStringExpression&>(expression));
      break;
    default:
      COMPILER_THROW("Unsupported expression type: " + expression.toString());
  }
}

void ExpressionCompiler::compileIdentifier(const ast::Identifier& id) {
  if (id.isGlobalScope) {
    emitLoadGlobal(id.symbol);
    return;
  }

  auto binding = lexicalResult_.identifier_bindings.find(&id);
  if (binding == lexicalResult_.identifier_bindings.end()) {
    COMPILER_THROW("Missing binding for identifier: " + id.symbol);
  }

  const auto& resolved = binding->second;
  switch (resolved.kind) {
    case ResolvedBindingKind::Local:
      emitLoadVar(resolved.slot);
      break;
    case ResolvedBindingKind::Upvalue:
      emitLoadUpvalue(resolved.slot);
      break;
    case ResolvedBindingKind::Function:
      emitter_.emit(OpCode::LOAD_CONST,
        Value::makeFunctionObjId(topLevelFunctionIndices_[resolved.name]));
      break;
    case ResolvedBindingKind::HostFunction:
      // TODO: Register host function and use makeHostFuncId
      emitter_.emit(OpCode::LOAD_CONST, Value::makeNull());
      break;
    case ResolvedBindingKind::Global:
      emitLoadGlobal(resolved.name);
      break;
  }
}

void ExpressionCompiler::compileBinaryExpression(const ast::BinaryExpression& binary) {
  if (binary.left) {
    compile(*binary.left);
  }
  if (binary.right) {
    compile(*binary.right);
  }
  emitter_.emit(binaryOpToBytecode(binary.operator_));
}

void ExpressionCompiler::compileUnaryExpression(const ast::UnaryExpression& unary) {
  if (unary.operand) {
    compile(*unary.operand);
  }

  switch (unary.operator_) {
    case ast::UnaryExpression::UnaryOperator::Not:
      emitter_.emit(OpCode::NOT);
      break;
    case ast::UnaryExpression::UnaryOperator::Minus:
      emitter_.emit(OpCode::NEGATE);
      break;
    case ast::UnaryExpression::UnaryOperator::Plus:
      // No-op
      break;
    default:
      COMPILER_THROW("Unsupported unary operator");
  }
}

void ExpressionCompiler::compileCallExpression(const ast::CallExpression& call) {
  if (!call.callee) {
    COMPILER_THROW("Call expression missing callee");
  }

  // Handle member method calls: obj.method(args)
  if (call.callee && call.callee->kind == ast::NodeType::MemberExpression) {
    const auto& member = static_cast<const ast::MemberExpression&>(*call.callee);
    // Load object
    compile(*member.object);
    // Keep object for 'this'
    emitter_.emit(OpCode::DUP);
    // Load method name
    if (member.property && member.property->kind == ast::NodeType::Identifier) {
      const auto& prop = static_cast<const ast::Identifier&>(*member.property);
      uint32_t strId = emitter_.addStringConstant(prop.symbol);
      emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));
    } else {
      COMPILER_THROW("Method call requires identifier method name");
    }
    // Look up method on object
    emitter_.emit(OpCode::OBJECT_GET);
    // Reorder to put method before object
    emitter_.emit(OpCode::SWAP);
    // Compile arguments
    for (const auto& arg : call.args) {
      if (arg) compile(*arg);
    }
    // Call method with object as 'this'
    emitter_.emit(OpCode::CALL, static_cast<uint32_t>(call.args.size() + 1));
    return;
  }

  // Regular function calls: compile args then callee
  for (const auto& arg : call.args) {
    if (arg) compile(*arg);
  }
  compile(*call.callee);
  emitter_.emit(OpCode::CALL, static_cast<uint32_t>(call.args.size()));
}

void ExpressionCompiler::compileAssignmentExpression(
    const ast::AssignmentExpression& assignment) {
  if (!assignment.value) {
    COMPILER_THROW("Assignment missing value");
  }

  // Compile value first (RHS)
  compile(*assignment.value);

  // Store to target
  if (assignment.target && assignment.target->kind == ast::NodeType::Identifier) {
    const auto& id = static_cast<const ast::Identifier&>(*assignment.target);

    auto binding = lexicalResult_.identifier_bindings.find(&id);
    if (binding == lexicalResult_.identifier_bindings.end()) {
      COMPILER_THROW("Missing binding for assignment target: " + id.symbol);
    }

    const auto& resolved = binding->second;
    emitter_.emit(OpCode::DUP); // Keep value on stack for result

    switch (resolved.kind) {
      case ResolvedBindingKind::Local:
        emitStoreVar(resolved.slot);
        break;
      case ResolvedBindingKind::Upvalue:
        emitStoreUpvalue(resolved.slot);
        break;
      case ResolvedBindingKind::Global:
        emitStoreGlobal(resolved.name);
        break;
      default:
        COMPILER_THROW("Cannot assign to this binding type");
    }
  } else if (assignment.target) {
    // For complex assignments (member, index, @field), we need to compile in a specific order:
    // 1. Object/key (if needed), 2. Value, 3. DUP value for result, 4. Reload key, 5. Store

    if (assignment.target->kind == ast::NodeType::MemberExpression) {
      // obj.field = value
      const auto& member = static_cast<const ast::MemberExpression&>(*assignment.target);
      // 1. Compile object
      compile(*member.object);
      // 2. Compile value (this pushes value on top of stack)
      compile(*assignment.value);
      // Stack: [obj, value]
      // 3. Keep value on stack for result
      emitter_.emit(OpCode::DUP); // [obj, value, value]
      // 4. Load field name
      if (member.property && member.property->kind == ast::NodeType::Identifier) {
        const auto& prop = static_cast<const ast::Identifier&>(*member.property);
        uint32_t strId = emitter_.addStringConstant(prop.symbol);
        emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));
        // Stack: [obj, value, value, key]
        // OBJECT_SET pops key, value, obj and stores, leaving [obj, value]
        emitter_.emit(OpCode::OBJECT_SET);
      } else {
        COMPILER_THROW("Member assignment requires identifier property");
      }
    } else if (assignment.target->kind == ast::NodeType::AtExpression) {
      // @field = value (equivalent to this.field = value)
      const auto& atExpr = static_cast<const ast::AtExpression&>(*assignment.target);
      // 1. Load 'this'
      uint32_t thisStrId = emitter_.addStringConstant("this");
      emitter_.emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(thisStrId));
      // 2. Compile value
      compile(*assignment.value);
      // Stack: [this, value]
      // 3. Keep value on stack for result
      emitter_.emit(OpCode::DUP); // [this, value, value]
      // 4. Load field name
      if (!atExpr.field) {
        COMPILER_THROW("@ expression missing field");
      }
      auto* fieldId = dynamic_cast<const ast::Identifier*>(atExpr.field.get());
      if (!fieldId) {
        COMPILER_THROW("@ expression field must be an identifier");
      }
      uint32_t fieldStrId = emitter_.addStringConstant(fieldId->symbol);
      emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(fieldStrId));
      // Stack: [this, value, value, key]
      // OBJECT_SET pops key, value, this and stores, leaving [this, value]
      emitter_.emit(OpCode::OBJECT_SET);
    } else if (assignment.target->kind == ast::NodeType::IndexExpression) {
      // obj[index] = value
      // Stack: [value]
      emitter_.emit(OpCode::DUP); // Keep value on stack for result
      const auto& indexExpr = static_cast<const ast::IndexExpression&>(*assignment.target);
      // Load object and index
      compile(*indexExpr.object);
      compile(*indexExpr.index);
      // Stack: [value, obj, index] -> ARRAY_SET will pop to store
      emitter_.emit(OpCode::ARRAY_SET);
    } else {
      compile(*assignment.target); // Fallback: compile as read (may throw at runtime)
    }
  }
}

void ExpressionCompiler::compileMemberExpression(const ast::MemberExpression& member) {
  if (!member.object) {
    COMPILER_THROW("Member expression missing object");
  }

  compile(*member.object);

  if (member.property && member.property->kind == ast::NodeType::Identifier) {
    const auto& prop = static_cast<const ast::Identifier&>(*member.property);
    uint32_t strId = emitter_.addStringConstant(prop.symbol);
    emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));
    emitter_.emit(OpCode::OBJECT_GET);
  }
}

void ExpressionCompiler::compileIndexExpression(const ast::IndexExpression& index) {
  if (!index.object || !index.index) {
    COMPILER_THROW("Index expression missing components");
  }

  compile(*index.object);
  compile(*index.index);
  emitter_.emit(OpCode::ARRAY_GET);
}

void ExpressionCompiler::compileLambdaExpression(const ast::LambdaExpression& lambda) {
  // Lambda compilation is handled by FunctionCompiler
  // This just emits a placeholder that gets patched later
  emitter_.emit(OpCode::CLOSURE, static_cast<uint32_t>(0)); // Placeholder
}

void ExpressionCompiler::compileArrayLiteral(const ast::ArrayLiteral& array) {
  // Create array first, then push elements
  emitter_.emit(OpCode::ARRAY_NEW);
  // Compile elements in reverse order (stack order for ARRAY_PUSH)
  for (auto it = array.elements.rbegin(); it != array.elements.rend(); ++it) {
    if (*it) {
      compile(**it);
      emitter_.emit(OpCode::ARRAY_PUSH);
    }
  }
}

void ExpressionCompiler::compileObjectLiteral(const ast::ObjectLiteral& object) {
  emitter_.emit(OpCode::OBJECT_NEW);
  for (const auto& [key, value] : object.pairs) {
    if (value) {
      compile(*value);
      uint32_t strId = emitter_.addStringConstant(key);
      emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));
      emitter_.emit(OpCode::OBJECT_SET);
    }
  }
}

void ExpressionCompiler::compileTernaryExpression(const ast::TernaryExpression& ternary) {
  if (!ternary.condition || !ternary.trueValue || !ternary.falseValue) {
    COMPILER_THROW("Ternary expression missing components");
  }

  compile(*ternary.condition);
  uint32_t falseJump = emitter_.emitJump(OpCode::JUMP_IF_FALSE);

  compile(*ternary.trueValue);
  uint32_t endJump = emitter_.emitJump(OpCode::JUMP);

  emitter_.patchJump(falseJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  compile(*ternary.falseValue);

  emitter_.patchJump(endJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
}

void ExpressionCompiler::compileUpdateExpression(const ast::UpdateExpression& update) {
  if (!update.argument || update.argument->kind != ast::NodeType::Identifier) {
    COMPILER_THROW("Update expression must have identifier argument");
  }

  const auto& id = static_cast<const ast::Identifier&>(*update.argument);
  auto binding = lexicalResult_.identifier_bindings.find(&id);
  if (binding == lexicalResult_.identifier_bindings.end()) {
    COMPILER_THROW("Missing binding for update expression");
  }

  const auto& resolved = binding->second;
  bool isIncrement = update.operator_ == ast::UpdateExpression::Operator::Increment;

  // Load current value
  if (resolved.kind == ResolvedBindingKind::Local) {
    emitLoadVar(resolved.slot);
  } else if (resolved.kind == ResolvedBindingKind::Upvalue) {
    emitLoadUpvalue(resolved.slot);
  } else {
    COMPILER_THROW("Cannot update non-local variable");
  }

  if (update.isPrefix) {
    // ++x: increment then return new value
    emitter_.emit(OpCode::LOAD_CONST, emitter_.addConstant(static_cast<int64_t>(1)));
    emitter_.emit(isIncrement ? OpCode::ADD : OpCode::SUB);
    emitter_.emit(OpCode::DUP);
    if (resolved.kind == ResolvedBindingKind::Local) {
      emitStoreVar(resolved.slot);
    } else {
      emitStoreUpvalue(resolved.slot);
    }
  } else {
    // x++: return old value, then increment
    emitter_.emit(OpCode::DUP); // Save old value
    emitter_.emit(OpCode::LOAD_CONST, emitter_.addConstant(static_cast<int64_t>(1)));
    emitter_.emit(isIncrement ? OpCode::ADD : OpCode::SUB);
    if (resolved.kind == ResolvedBindingKind::Local) {
      emitStoreVar(resolved.slot);
    } else {
      emitStoreUpvalue(resolved.slot);
    }
    emitter_.emit(OpCode::POP); // Pop new value, leave old
  }
}

void ExpressionCompiler::compileAwaitExpression(const ast::AwaitExpression& await) {
  if (await.argument) {
    compile(*await.argument);
  }
  uint32_t strId = emitter_.addStringConstant(std::string("async.await"));
  emitter_.emit(OpCode::CALL_HOST, std::vector<Value>{
    Value::makeStringValId(strId),
    Value::makeInt(static_cast<int64_t>(1))
  });
}

void ExpressionCompiler::compileSpreadExpression(const ast::SpreadExpression& spread) {
  if (spread.target) {
    compile(*spread.target);
  }
  emitter_.emit(OpCode::SPREAD);
}

void ExpressionCompiler::compileRangeExpression(const ast::RangeExpression& range) {
  if (!range.start || !range.end) {
    COMPILER_THROW("Range expression missing bounds");
  }

  compile(*range.start);
  compile(*range.end);

  if (range.step) {
    compile(*range.step);
    emitter_.emit(OpCode::RANGE_STEP_NEW);
  } else {
    emitter_.emit(OpCode::RANGE_NEW);
  }
}

void ExpressionCompiler::compilePipelineExpression(const ast::PipelineExpression& pipeline) {
  if (pipeline.stages.empty()) {
    COMPILER_THROW("Pipeline expression has no stages");
  }

  // Compile first stage
  compile(*pipeline.stages[0]);

  // Compile subsequent stages with piping
  for (size_t i = 1; i < pipeline.stages.size(); ++i) {
    uint32_t tempSlot = emitter_.reserveLocalSlot();
    emitter_.emit(OpCode::STORE_VAR, tempSlot);

    // Load piped value as first argument
    emitter_.emit(OpCode::LOAD_VAR, tempSlot);

    // Compile stage
    compile(*pipeline.stages[i]);

    // Call with piped value
    emitter_.emit(OpCode::CALL, static_cast<uint32_t>(1));
  }
}

void ExpressionCompiler::compileInterpolatedString(
    const ast::InterpolatedStringExpression& interp) {
  // Start with empty string
  uint32_t emptyStrId = emitter_.addStringConstant(std::string(""));
  emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(emptyStrId));

  for (const auto& segment : interp.segments) {
    if (segment.isString) {
      uint32_t segStrId = emitter_.addStringConstant(segment.stringValue);
      emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(segStrId));
    } else if (segment.expression) {
      compile(*segment.expression);
    }
    emitter_.emit(OpCode::ADD); // Concatenate
  }
}

// Helper methods
void ExpressionCompiler::emitLoadVar(uint32_t slot) {
  emitter_.emit(OpCode::LOAD_VAR, slot);
}

void ExpressionCompiler::emitStoreVar(uint32_t slot) {
  emitter_.emit(OpCode::STORE_VAR, slot);
}

void ExpressionCompiler::emitLoadUpvalue(uint32_t slot) {
  emitter_.emit(OpCode::LOAD_UPVALUE, slot);
}

void ExpressionCompiler::emitStoreUpvalue(uint32_t slot) {
  emitter_.emit(OpCode::STORE_UPVALUE, slot);
}

void ExpressionCompiler::emitLoadGlobal(const std::string& name) {
  uint32_t strId = emitter_.addStringConstant(name);
  emitter_.emit(OpCode::LOAD_GLOBAL, Value::makeStringValId(strId));
}

void ExpressionCompiler::emitStoreGlobal(const std::string& name) {
  uint32_t strId = emitter_.addStringConstant(name);
  emitter_.emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
}

void ExpressionCompiler::emitLoadConst(const Value& value) {
  emitter_.emit(OpCode::LOAD_CONST, emitter_.addConstant(value));
}

OpCode ExpressionCompiler::binaryOpToBytecode(ast::BinaryOperator op) const {
  switch (op) {
    case ast::BinaryOperator::Add: return OpCode::ADD;
    case ast::BinaryOperator::Sub: return OpCode::SUB;
    case ast::BinaryOperator::Mul: return OpCode::MUL;
    case ast::BinaryOperator::Div: return OpCode::DIV;
    case ast::BinaryOperator::Mod: return OpCode::MOD;
    case ast::BinaryOperator::Pow: return OpCode::POW;
    case ast::BinaryOperator::Equal: return OpCode::EQ;
    case ast::BinaryOperator::NotEqual: return OpCode::NEQ;
    case ast::BinaryOperator::Less: return OpCode::LT;
    case ast::BinaryOperator::LessEqual: return OpCode::LTE;
    case ast::BinaryOperator::Greater: return OpCode::GT;
    case ast::BinaryOperator::GreaterEqual: return OpCode::GTE;
    case ast::BinaryOperator::And: return OpCode::AND;
    case ast::BinaryOperator::Or: return OpCode::OR;
    default: COMPILER_THROW("Unsupported binary operator");
  }
}

bool ExpressionCompiler::isAssignmentOp(ast::BinaryOperator op) const {
  return op == ast::BinaryOperator::AddAssign ||
         op == ast::BinaryOperator::SubAssign ||
         op == ast::BinaryOperator::MulAssign ||
         op == ast::BinaryOperator::DivAssign;
}

void ExpressionCompiler::emitCompoundAssignment(const ast::Identifier& target,
                                                  const ast::Expression& value,
                                                  OpCode mathOp) {
  // Not fully implemented - would need binding lookup
  (void)target;
  (void)value;
  (void)mathOp;
}

// ============================================================================
// StatementCompiler Implementation
// ============================================================================

StatementCompiler::StatementCompiler(CodeEmitter& emitter,
                                       ExpressionCompiler& exprCompiler,
                                       BindingResolver& bindingResolver)
    : emitter_(emitter),
      exprCompiler_(exprCompiler),
      bindingResolver_(bindingResolver) {}

void StatementCompiler::compile(const ast::Statement& statement) {
  switch (statement.kind) {
    case ast::NodeType::ExpressionStatement:
      compileExpressionStatement(static_cast<const ast::ExpressionStatement&>(statement));
      break;
    case ast::NodeType::LetDeclaration:
      compileLetDeclaration(static_cast<const ast::LetDeclaration&>(statement));
      break;
    case ast::NodeType::IfStatement:
      compileIfStatement(static_cast<const ast::IfStatement&>(statement));
      break;
    case ast::NodeType::WhileStatement:
      compileWhileStatement(static_cast<const ast::WhileStatement&>(statement));
      break;
    case ast::NodeType::DoWhileStatement:
      compileDoWhileStatement(static_cast<const ast::DoWhileStatement&>(statement));
      break;
    case ast::NodeType::ForStatement:
      compileForStatement(static_cast<const ast::ForStatement&>(statement));
      break;
    case ast::NodeType::LoopStatement:
      compileLoopStatement(static_cast<const ast::LoopStatement&>(statement));
      break;
    case ast::NodeType::ReturnStatement:
      compileReturnStatement(static_cast<const ast::ReturnStatement&>(statement));
      break;
    case ast::NodeType::BlockStatement:
      compileBlockStatement(static_cast<const ast::BlockStatement&>(statement));
      break;
    case ast::NodeType::TryExpression:
      compileTryStatement(static_cast<const ast::TryExpression&>(statement));
      break;
    case ast::NodeType::ThrowStatement:
      compileThrowStatement(static_cast<const ast::ThrowStatement&>(statement));
      break;
    case ast::NodeType::WhenBlockStatement:
      compileWhenBlock(static_cast<const ast::WhenBlock&>(statement));
      break;
    case ast::NodeType::ModeBlock:
      compileModeBlock(static_cast<const ast::ModeBlock&>(statement));
      break;
    case ast::NodeType::HotkeyBinding:
      compileHotkeyBinding(static_cast<const ast::HotkeyBinding&>(statement));
      break;
    case ast::NodeType::ConditionalHotkey:
      compileConditionalHotkey(static_cast<const ast::ConditionalHotkey&>(statement));
      break;
    case ast::NodeType::InputStatement:
      compileInputStatement(static_cast<const ast::InputStatement&>(statement));
      break;
    case ast::NodeType::UseStatement:
      compileUseStatement(static_cast<const ast::UseStatement&>(statement));
      break;
    case ast::NodeType::ExportStatement:
      compileExportStatement(static_cast<const ast::ExportStatement&>(statement));
      break;
    default:
      COMPILER_THROW("Unsupported statement type: " + statement.toString());
  }
}

void StatementCompiler::compileExpressionStatement(const ast::ExpressionStatement& stmt) {
  if (stmt.expression) {
    exprCompiler_.compile(*stmt.expression);
    emitter_.emit(OpCode::POP); // Discard result
  }
}

void StatementCompiler::compileLetDeclaration(const ast::LetDeclaration& let) {
  if (let.value) {
    exprCompiler_.compile(*let.value);
  } else {
    emitter_.emit(OpCode::LOAD_CONST, Value::makeNull()); // undefined
  }

  // Store to pattern
  if (let.pattern) {
    if (let.pattern->kind == ast::NodeType::Identifier) {
      const auto& id = static_cast<const ast::Identifier&>(*let.pattern);
      auto binding = bindingResolver_.resolveIdentifier(id.symbol);
      if (binding) {
        if (binding->kind == ResolvedBindingKind::Local) {
          emitter_.emit(OpCode::STORE_VAR, binding->slot);
        } else if (binding->kind == ResolvedBindingKind::Global) {
          uint32_t strId = emitter_.addStringConstant(id.symbol);
          emitter_.emit(OpCode::STORE_GLOBAL, Value::makeStringValId(strId));
        }
        emitter_.emit(OpCode::POP); // Discard stored value (statement context)
      }
    }
    // TODO: Handle complex patterns
  }
}

void StatementCompiler::compileIfStatement(const ast::IfStatement& ifStmt) {
  if (!ifStmt.condition) {
    COMPILER_THROW("If statement missing condition");
  }

  exprCompiler_.compile(*ifStmt.condition);
  uint32_t elseJump = emitter_.emitJump(OpCode::JUMP_IF_FALSE);

  if (ifStmt.consequence) {
    compile(*ifStmt.consequence);
  }

  if (ifStmt.alternative) {
    uint32_t endJump = emitter_.emitJump(OpCode::JUMP);
    emitter_.patchJump(elseJump,
      static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
    compile(*ifStmt.alternative);
    emitter_.patchJump(endJump,
      static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  } else {
    emitter_.patchJump(elseJump,
      static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  }
}

void StatementCompiler::compileWhileStatement(const ast::WhileStatement& whileStmt) {
  if (!whileStmt.condition || !whileStmt.body) {
    COMPILER_THROW("While statement missing components");
  }

  uint32_t loopStart = static_cast<uint32_t>(emitter_.currentFunction().instructions.size());
  pushLoopContext(loopStart);

  exprCompiler_.compile(*whileStmt.condition);
  uint32_t exitJump = emitter_.emitJump(OpCode::JUMP_IF_FALSE);

  compile(*whileStmt.body);
  emitter_.emit(OpCode::JUMP, loopStart);

  emitter_.patchJump(exitJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));

  patchContinues(loopStart);
  patchBreaks(static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  popLoopContext();
}

void StatementCompiler::compileDoWhileStatement(const ast::DoWhileStatement& doWhile) {
  if (!doWhile.body || !doWhile.condition) {
    COMPILER_THROW("Do-while statement missing components");
  }

  uint32_t loopStart = static_cast<uint32_t>(emitter_.currentFunction().instructions.size());
  pushLoopContext(loopStart);

  compile(*doWhile.body);

  exprCompiler_.compile(*doWhile.condition);
  emitter_.emit(OpCode::JUMP_IF_TRUE, loopStart);

  patchBreaks(static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  popLoopContext();
}

void StatementCompiler::compileForStatement(const ast::ForStatement& forStmt) {
  if (!forStmt.iterable || !forStmt.body) {
    COMPILER_THROW("For statement missing components");
  }

  bindingResolver_.beginScope();

  // Compile iterable
  exprCompiler_.compile(*forStmt.iterable);
  uint32_t iteratorSlot = emitter_.reserveLocalSlot();
  emitter_.emit(OpCode::STORE_VAR, iteratorSlot);

  uint32_t loopStart = static_cast<uint32_t>(emitter_.currentFunction().instructions.size());
  pushLoopContext(loopStart);

  // Get next value
  emitter_.emit(OpCode::LOAD_VAR, iteratorSlot);
  uint32_t iterNextStrId = emitter_.addStringConstant(std::string("iterator.next"));
  emitter_.emit(OpCode::CALL_HOST, std::vector<Value>{
    Value::makeStringValId(iterNextStrId),
    Value(static_cast<uint32_t>(1))
  });

  // Check if done
  emitter_.emit(OpCode::DUP);
  emitter_.emit(OpCode::LOAD_CONST, Value::makeNull());
  emitter_.emit(OpCode::EQ);
  uint32_t exitJump = emitter_.emitJump(OpCode::JUMP_IF_TRUE);
  emitter_.emit(OpCode::POP); // Pop the null check result

  // Store to iterator variables
  for (const auto& iter : forStmt.iterators) {
    if (iter) {
      uint32_t slot = bindingResolver_.declareLocal(iter->symbol, iter.get(), false);
      emitter_.emit(OpCode::DUP);
      emitter_.emit(OpCode::STORE_VAR, slot);
    }
  }
  emitter_.emit(OpCode::POP); // Pop iterator value

  compile(*forStmt.body);
  emitter_.emit(OpCode::JUMP, loopStart);

  emitter_.patchJump(exitJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  emitter_.emit(OpCode::POP); // Pop final null

  patchBreaks(static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  popLoopContext();

  bindingResolver_.endScope();
}

void StatementCompiler::compileLoopStatement(const ast::LoopStatement& loop) {
  uint32_t loopStart = static_cast<uint32_t>(emitter_.currentFunction().instructions.size());
  pushLoopContext(loopStart);

  if (loop.countExpr) {
    // Count-based loop
    exprCompiler_.compile(*loop.countExpr);
    uint32_t counterSlot = emitter_.reserveLocalSlot();
    emitter_.emit(OpCode::STORE_VAR, counterSlot);

    uint32_t checkStart = static_cast<uint32_t>(emitter_.currentFunction().instructions.size());
    emitter_.emit(OpCode::LOAD_VAR, counterSlot);
    emitter_.emit(OpCode::LOAD_CONST, emitter_.addConstant(static_cast<int64_t>(0)));
    emitter_.emit(OpCode::GT);
    uint32_t exitJump = emitter_.emitJump(OpCode::JUMP_IF_FALSE);

    emitter_.emit(OpCode::LOAD_VAR, counterSlot);
    emitter_.emit(OpCode::LOAD_CONST, emitter_.addConstant(static_cast<int64_t>(1)));
    emitter_.emit(OpCode::SUB);
    emitter_.emit(OpCode::STORE_VAR, counterSlot);

    if (loop.body) {
      compile(*loop.body);
    }
    emitter_.emit(OpCode::JUMP, checkStart);

    emitter_.patchJump(exitJump,
      static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  } else if (loop.condition) {
    // While-style loop
    exprCompiler_.compile(*loop.condition);
    uint32_t exitJump = emitter_.emitJump(OpCode::JUMP_IF_FALSE);

    if (loop.body) {
      compile(*loop.body);
    }
    emitter_.emit(OpCode::JUMP, loopStart);

    emitter_.patchJump(exitJump,
      static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  } else {
    // Infinite loop
    if (loop.body) {
      compile(*loop.body);
    }
    emitter_.emit(OpCode::JUMP, loopStart);
  }

  patchBreaks(static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
  popLoopContext();
}

void StatementCompiler::compileReturnStatement(const ast::ReturnStatement& ret) {
  if (ret.argument) {
    exprCompiler_.compile(*ret.argument);
  } else {
    emitter_.emit(OpCode::LOAD_CONST, Value::makeNull());
  }
  emitter_.emit(OpCode::RETURN);
}

void StatementCompiler::compileBlockStatement(const ast::BlockStatement& block) {
  bindingResolver_.beginScope();
  for (const auto& stmt : block.body) {
    if (stmt) {
      compile(*stmt);
    }
  }
  bindingResolver_.endScope();
}

void StatementCompiler::compileTryStatement(const ast::TryExpression& tryExpr) {
  if (!tryExpr.tryBody) {
    COMPILER_THROW("Try statement missing body");
  }

  uint32_t catchJump = emitter_.emitJump(OpCode::TRY_ENTER);
  compile(*tryExpr.tryBody);
  uint32_t finallyJump = emitter_.emitJump(OpCode::TRY_EXIT);

  emitter_.patchJump(catchJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));

  if (tryExpr.catchBody) {
    bindingResolver_.beginScope();
    if (tryExpr.catchVariable) {
      uint32_t slot = bindingResolver_.declareLocal(
        tryExpr.catchVariable->symbol, tryExpr.catchVariable.get(), false);
      emitter_.emit(OpCode::STORE_VAR, slot);
    }
    compile(*tryExpr.catchBody);
    bindingResolver_.endScope();
  }

  if (tryExpr.finallyBlock) {
    compile(*tryExpr.finallyBlock);
  }

  emitter_.patchJump(finallyJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
}

void StatementCompiler::compileThrowStatement(const ast::ThrowStatement& throwStmt) {
  if (throwStmt.value) {
    exprCompiler_.compile(*throwStmt.value);
  } else {
    emitter_.emit(OpCode::LOAD_CONST, Value::makeNull());
  }
  emitter_.emit(OpCode::THROW);
}

void StatementCompiler::compileWhenBlock(const ast::WhenBlock& when) {
  if (!when.condition) {
    COMPILER_THROW("When block missing condition");
  }

  exprCompiler_.compile(*when.condition);
  uint32_t skipJump = emitter_.emitJump(OpCode::JUMP_IF_FALSE);

  bindingResolver_.beginScope();
  for (const auto& stmt : when.statements) {
    if (stmt) {
      compile(*stmt);
    }
  }
  bindingResolver_.endScope();

  emitter_.patchJump(skipJump,
    static_cast<uint32_t>(emitter_.currentFunction().instructions.size()));
}

void StatementCompiler::compileModeBlock(const ast::ModeBlock& mode) {
  bindingResolver_.beginScope();
  for (const auto& stmt : mode.statements) {
    if (stmt) {
      compile(*stmt);
    }
  }
  bindingResolver_.endScope();
}

void StatementCompiler::compileHotkeyBinding(const ast::HotkeyBinding& hotkey) {
  bindingResolver_.beginScope();
  if (hotkey.action) {
    compile(*hotkey.action);
  }
  bindingResolver_.endScope();
}

void StatementCompiler::compileConditionalHotkey(const ast::ConditionalHotkey& condHotkey) {
  if (condHotkey.condition) {
    exprCompiler_.compile(*condHotkey.condition);
  }
  if (condHotkey.binding) {
    compile(*condHotkey.binding);
  }
}

void StatementCompiler::compileInputStatement(const ast::InputStatement& input) {
  (void)input; // TODO: Implement input statement compilation
}

void StatementCompiler::compileUseStatement(const ast::UseStatement& use) {
  (void)use; // TODO: Implement use statement compilation
}

void StatementCompiler::compileExportStatement(const ast::ExportStatement& exportStmt) {
  (void)exportStmt; // TODO: Implement export statement compilation
}

void StatementCompiler::pushLoopContext(uint32_t start) {
  LoopContext ctx;
  ctx.loopStart = start;
  loopStack_.push_back(ctx);
}

void StatementCompiler::popLoopContext() {
  if (!loopStack_.empty()) {
    loopStack_.pop_back();
  }
}

void StatementCompiler::patchBreaks(uint32_t target) {
  if (loopStack_.empty()) return;
  for (uint32_t jump : loopStack_.back().breakJumps) {
    emitter_.patchJump(jump, target);
  }
}

void StatementCompiler::patchContinues(uint32_t target) {
  if (loopStack_.empty()) return;
  for (uint32_t jump : loopStack_.back().continueJumps) {
    emitter_.patchJump(jump, target);
  }
}

// ============================================================================
// PatternCompiler Implementation
// ============================================================================

PatternCompiler::PatternCompiler(CodeEmitter& emitter, BindingResolver& bindingResolver)
    : emitter_(emitter), bindingResolver_(bindingResolver) {}

void PatternCompiler::compilePatternMatch(const ast::Expression& pattern, uint32_t valueSlot) {
  switch (pattern.kind) {
    case ast::NodeType::Identifier:
      compileIdentifierPattern(static_cast<const ast::Identifier&>(pattern));
      break;
    case ast::NodeType::ArrayPattern:
      compileArrayPattern(static_cast<const ast::ArrayPattern&>(pattern), valueSlot);
      break;
    case ast::NodeType::ObjectPattern:
      compileObjectPattern(static_cast<const ast::ObjectPattern&>(pattern), valueSlot);
      break;
    default:
      COMPILER_THROW("Unsupported pattern type");
  }
}

std::vector<const ast::Identifier*> PatternCompiler::collectIdentifiers(
    const ast::Expression& pattern) const {
  std::vector<const ast::Identifier*> result;

  switch (pattern.kind) {
    case ast::NodeType::Identifier:
      result.push_back(static_cast<const ast::Identifier*>(&pattern));
      break;
    case ast::NodeType::ArrayPattern: {
      const auto& arr = static_cast<const ast::ArrayPattern&>(pattern);
      for (const auto& elem : arr.elements) {
        if (elem) {
          auto ids = collectIdentifiers(*elem);
          result.insert(result.end(), ids.begin(), ids.end());
        }
      }
      break;
    }
    case ast::NodeType::ObjectPattern: {
      const auto& obj = static_cast<const ast::ObjectPattern&>(pattern);
      for (const auto& [key, value] : obj.properties) {
        if (value) {
          auto ids = collectIdentifiers(*value);
          result.insert(result.end(), ids.begin(), ids.end());
        }
      }
      break;
    }
    default:
      break;
  }

  return result;
}

void PatternCompiler::declarePatternVariables(const ast::Expression& pattern) {
  auto ids = collectIdentifiers(pattern);
  for (const auto* id : ids) {
    bindingResolver_.declareLocal(id->symbol, id, false);
  }
}

void PatternCompiler::compileArrayPattern(const ast::ArrayPattern& pattern, uint32_t valueSlot) {
  emitter_.emit(OpCode::LOAD_VAR, valueSlot);

  for (size_t i = 0; i < pattern.elements.size(); ++i) {
    emitter_.emit(OpCode::DUP);
    emitter_.emit(OpCode::LOAD_CONST, emitter_.addConstant(static_cast<int64_t>(i)));
    emitter_.emit(OpCode::ARRAY_GET);

    if (pattern.elements[i]) {
      if (pattern.elements[i]->kind == ast::NodeType::Identifier) {
        const auto& id = static_cast<const ast::Identifier&>(*pattern.elements[i]);
        auto binding = bindingResolver_.resolveIdentifier(id.symbol);
        if (binding && binding->kind == ResolvedBindingKind::Local) {
          emitter_.emit(OpCode::STORE_VAR, binding->slot);
        }
      }
    }
  }

  emitter_.emit(OpCode::POP); // Pop the array
}

void PatternCompiler::compileObjectPattern(const ast::ObjectPattern& pattern, uint32_t valueSlot) {
  emitter_.emit(OpCode::LOAD_VAR, valueSlot);

  for (const auto& [key, value] : pattern.properties) {
    emitter_.emit(OpCode::DUP);
    uint32_t strId = emitter_.addStringConstant(key);
    emitter_.emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));
    emitter_.emit(OpCode::OBJECT_GET);

    if (value) {
      if (value->kind == ast::NodeType::Identifier) {
        const auto& id = static_cast<const ast::Identifier&>(*value);
        auto binding = bindingResolver_.resolveIdentifier(id.symbol);
        if (binding && binding->kind == ResolvedBindingKind::Local) {
          emitter_.emit(OpCode::STORE_VAR, binding->slot);
        }
      }
    }
  }

  emitter_.emit(OpCode::POP); // Pop the object
}

void PatternCompiler::compileIdentifierPattern(const ast::Identifier& ident) {
  auto binding = bindingResolver_.resolveIdentifier(ident.symbol);
  if (binding && binding->kind == ResolvedBindingKind::Local) {
    emitter_.emit(OpCode::STORE_VAR, binding->slot);
  }
}

// ============================================================================
// FunctionCompiler Implementation
// ============================================================================

FunctionCompiler::FunctionCompiler(CodeEmitter& emitter,
                                    StatementCompiler& stmtCompiler,
                                    ExpressionCompiler& exprCompiler,
                                    PatternCompiler& patternCompiler,
                                    BindingResolver& bindingResolver)
    : emitter_(emitter),
      stmtCompiler_(stmtCompiler),
      exprCompiler_(exprCompiler),
      patternCompiler_(patternCompiler),
      bindingResolver_(bindingResolver) {}

uint32_t FunctionCompiler::compileFunction(const ast::FunctionDeclaration& function) {
  BytecodeFunction func(function.name ? function.name->symbol : "<anonymous>",
                        static_cast<uint32_t>(function.parameters.size()), 0);

  emitter_.beginFunction(std::move(func));
  bindingResolver_.beginFunction(&function);

  emitPrologue();
  compileParameters(function.parameters);

  if (function.body) {
    compileFunctionBody(*function.body);
  }

  emitEpilogue();

  bindingResolver_.endFunction();
  return emitter_.endFunction();
}

uint32_t FunctionCompiler::compileLambda(const ast::LambdaExpression& lambda) {
  BytecodeFunction func("<lambda>",
                        static_cast<uint32_t>(lambda.parameters.size()), 0);

  emitter_.beginFunction(std::move(func));
  bindingResolver_.beginFunction(&lambda);

  emitPrologue();
  compileParameters(lambda.parameters);

  if (lambda.body) {
    if (lambda.body->kind == ast::NodeType::ExpressionStatement) {
      // Expression lambda - return the expression value
      const auto& exprStmt = static_cast<const ast::ExpressionStatement&>(*lambda.body);
      if (exprStmt.expression) {
        exprCompiler_.compile(*exprStmt.expression);
      }
      emitter_.emit(OpCode::RETURN);
    } else {
      // Block lambda body
      stmtCompiler_.compile(*lambda.body);
    }
  }

  emitEpilogue();

  bindingResolver_.endFunction();
  return emitter_.endFunction();
}

void FunctionCompiler::compileParameters(
    const std::vector<std::unique_ptr<ast::FunctionParameter>>& params) {
  for (const auto& param : params) {
    if (param && param->pattern) {
      if (param->pattern->kind == ast::NodeType::Identifier) {
        const auto& id = static_cast<const ast::Identifier&>(*param->pattern);
        bindingResolver_.declareLocal(id.symbol, &id, false);
      } else {
        patternCompiler_.declarePatternVariables(*param->pattern);
      }
    }
  }
}

void FunctionCompiler::compileFunctionBody(const ast::BlockStatement& body) {
  for (const auto& stmt : body.body) {
    if (stmt) {
      stmtCompiler_.compile(*stmt);
    }
  }
}

void FunctionCompiler::emitPrologue() {
  // Reserve slots for parameters
  // (Parameters are already on the stack from the caller)
}

void FunctionCompiler::emitEpilogue() {
  // Ensure function returns something
  emitter_.emit(OpCode::LOAD_CONST, Value::makeNull());
  emitter_.emit(OpCode::RETURN);
}

} // namespace havel::compiler
