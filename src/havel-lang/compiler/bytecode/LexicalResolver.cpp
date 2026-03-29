#include "LexicalResolver.hpp"

namespace havel::compiler {

LexicalResolutionResult LexicalResolver::resolve(const ast::Program &program) {
  result_ = {};
  errors_.clear();
  top_level_functions_.clear();
  top_level_structs_.clear();
  global_variables_.clear();
  function_stack_.clear();

  collectTopLevelFunctions(program);
  collectTopLevelStructs(program);

  // Enter global scope
  beginFunction(nullptr);

  // First pass: declare ALL top-level bindings (let and fn names)
  // This ensures function bodies can see global variables
  for (const auto &statement : program.body) {
    if (!statement) continue;

    if (statement->kind == ast::NodeType::LetDeclaration) {
      // Declare the variable and track as global
      const auto &let = static_cast<const ast::LetDeclaration &>(*statement);
      if (let.value) {
        resolveExpression(*let.value);
      }
      if (auto *identifier = dynamic_cast<const ast::Identifier *>(let.pattern.get())) {
        declareLocal(identifier->symbol, identifier, let.isConst);
        global_variables_.insert(identifier->symbol);
      }
    } else if (statement->kind == ast::NodeType::FunctionDeclaration) {
      // Just add to top_level_functions_ - don't declare as local
      const auto &fn = static_cast<const ast::FunctionDeclaration &>(*statement);
      if (fn.name) {
        top_level_functions_.insert(fn.name->symbol);
      }
    }
  }

  // Second pass: resolve function bodies (can now see globals)
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
      continue;
    }
    const auto &fn = static_cast<const ast::FunctionDeclaration &>(*statement);
    resolveFunctionDeclaration(fn);
  }

  // Third pass: resolve top-level non-function, non-let statements
  for (const auto &statement : program.body) {
    if (!statement ||
        statement->kind == ast::NodeType::FunctionDeclaration ||
        statement->kind == ast::NodeType::LetDeclaration) {
      continue;
    }
    resolveStatement(*statement);
  }

  // Copy to result for bytecode compiler
  result_.global_variables = global_variables_;

  endFunction();
  return result_;
}

void LexicalResolver::collectTopLevelFunctions(const ast::Program &program) {
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::FunctionDeclaration) {
      continue;
    }

    const auto &fn = static_cast<const ast::FunctionDeclaration &>(*statement);
    if (fn.name) {
      top_level_functions_.insert(fn.name->symbol);
    }
  }
}

void LexicalResolver::collectTopLevelStructs(const ast::Program &program) {
  for (const auto &statement : program.body) {
    if (!statement || statement->kind != ast::NodeType::StructDeclaration) {
      continue;
    }
    const auto &decl = static_cast<const ast::StructDeclaration &>(*statement);
    top_level_structs_.insert(decl.name);
  }
}

void LexicalResolver::beginFunction(const ast::ASTNode *function) {
  function_stack_.push_back(FunctionContext{});
  auto &ctx = function_stack_.back();
  ctx.owner = function;
  beginScope();
}

void LexicalResolver::endFunction() {
  if (function_stack_.empty()) {
    return;
  }

  const auto &ctx = function_stack_.back();
  if (ctx.owner) {
    if (ctx.owner->kind == ast::NodeType::FunctionDeclaration) {
      result_.function_upvalues[static_cast<const ast::FunctionDeclaration *>(
          ctx.owner)] = ctx.upvalues;
    } else if (ctx.owner->kind == ast::NodeType::LambdaExpression) {
      result_.lambda_upvalues[static_cast<const ast::LambdaExpression *>(
          ctx.owner)] = ctx.upvalues;
    }
  }

  function_stack_.pop_back();
}

void LexicalResolver::beginScope() {
  if (function_stack_.empty()) {
    return;
  }
  function_stack_.back().scopes.emplace_back();
}

void LexicalResolver::endScope() {
  if (function_stack_.empty() || function_stack_.back().scopes.empty()) {
    return;
  }
  function_stack_.back().scopes.pop_back();
}

uint32_t LexicalResolver::declareLocal(const std::string &name,
                                       const ast::Identifier *declaration,
                                       bool is_const) {
  auto &ctx = function_stack_.back();
  if (ctx.scopes.empty()) {
    beginScope();
  }

  auto &scope = ctx.scopes.back();
  auto it = scope.find(name);
  if (it != scope.end()) {
    // Duplicate declaration in same scope - report error with source location
    std::string msg = "Duplicate declaration: '" + name + "' already defined in this scope";
    if (declaration) {
      msg += " at " + std::to_string(declaration->line) + ":" + 
             std::to_string(declaration->column);
    }
    errors_.push_back(msg);
    return it->second.slot;
  }

  uint32_t slot = ctx.next_slot++;
  scope[name] =
      FunctionContext::LocalSymbol{.slot = slot, .is_const = is_const};
  if (declaration) {
    result_.declaration_slots[declaration] = slot;
  }
  return slot;
}

void LexicalResolver::resolveFunctionDeclaration(
    const ast::FunctionDeclaration &function) {
  beginFunction(&function);

  for (const auto &param : function.parameters) {
    if (param && param->pattern) {
      collectPatternIdentifiers(*param->pattern);
    }
  }

  if (function.body) {
    for (const auto &statement : function.body->body) {
      if (statement) {
        resolveStatement(*statement);
      }
    }
  }

  endFunction();
}

void LexicalResolver::resolveLambdaExpression(
    const ast::LambdaExpression &lambda) {
  beginFunction(&lambda);

  // For lambda parameters, allocate slots in order:
  // - Each parameter gets a slot (0, 1, 2... for params)
  // - Pattern params also allocate additional slots for extracted values
  for (size_t i = 0; i < lambda.parameters.size(); i++) {
    const auto &param = lambda.parameters[i];
    if (param && param->pattern) {
      if (param->pattern->kind == ast::NodeType::Identifier) {
        // Simple identifier - allocate one slot for this param
        const auto &ident = static_cast<const ast::Identifier &>(*param->pattern);
        declareLocal(ident.symbol, &ident, false);
      } else {
        // Pattern - allocate slot for parameter value (using temp name)
        // The extracted values will get subsequent slots
        declareLocal("_p" + std::to_string(i), nullptr, false);
        // Then allocate slots for extracted values
        collectPatternIdentifiers(*param->pattern);
      }
    }
  }

  if (lambda.body) {
    resolveStatement(*lambda.body);
  }

  endFunction();
}

// Collect all identifiers from a pattern and declare them as locals
void LexicalResolver::collectPatternIdentifiers(const ast::Expression &pattern) {
  switch (pattern.kind) {
  case ast::NodeType::Identifier: {
    const auto &ident = static_cast<const ast::Identifier &>(pattern);
    declareLocal(ident.symbol, &ident, false);
    break;
  }
  case ast::NodeType::ObjectPattern: {
    const auto &objPat = static_cast<const ast::ObjectPattern &>(pattern);
    for (const auto &prop : objPat.properties) {
      collectPatternIdentifiers(*prop.second);
    }
    break;
  }
  case ast::NodeType::ArrayPattern: {
    const auto &arrPat = static_cast<const ast::ArrayPattern &>(pattern);
    for (const auto &elem : arrPat.elements) {
      if (elem) {
        collectPatternIdentifiers(*elem);
      }
    }
    break;
  }
  default:
    break;
  }
}

void LexicalResolver::resolveStatement(const ast::Statement &statement) {
  switch (statement.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &expr_stmt =
        static_cast<const ast::ExpressionStatement &>(statement);
    if (expr_stmt.expression) {
      resolveExpression(*expr_stmt.expression);
    }
    break;
  }

  case ast::NodeType::LetDeclaration: {
    const auto &let = static_cast<const ast::LetDeclaration &>(statement);
    if (let.value) {
      resolveExpression(*let.value);
    }

    auto *identifier = dynamic_cast<const ast::Identifier *>(let.pattern.get());
    if (identifier) {
      // Always allocate a new slot for `let` declarations.
      // This preserves lexical shadowing across nested blocks.
      declareLocal(identifier->symbol, identifier, let.isConst);
    } else if (let.pattern && let.pattern->kind == ast::NodeType::ListPattern) {
      const auto &tuple_pattern =
          static_cast<const ast::ArrayPattern &>(*let.pattern);
      for (const auto &element : tuple_pattern.elements) {
        auto *element_id =
            element ? dynamic_cast<const ast::Identifier *>(element.get())
                    : nullptr;
        if (element_id) {
          declareLocal(element_id->symbol, element_id, let.isConst);
        }
      }
    }
    break;
  }

  case ast::NodeType::ReturnStatement: {
    const auto &ret = static_cast<const ast::ReturnStatement &>(statement);
    if (ret.argument) {
      resolveExpression(*ret.argument);
    }
    break;
  }

  case ast::NodeType::IfStatement: {
    const auto &if_stmt = static_cast<const ast::IfStatement &>(statement);
    if (if_stmt.condition) {
      resolveExpression(*if_stmt.condition);
    }
    if (if_stmt.consequence) {
      resolveStatement(*if_stmt.consequence);
    }
    if (if_stmt.alternative) {
      resolveStatement(*if_stmt.alternative);
    }
    break;
  }

  case ast::NodeType::WhileStatement: {
    const auto &while_stmt = static_cast<const ast::WhileStatement &>(statement);
    if (while_stmt.condition) {
      resolveExpression(*while_stmt.condition);
    }
    if (while_stmt.body) {
      resolveStatement(*while_stmt.body);
    }
    break;
  }

  case ast::NodeType::LoopStatement: {
    const auto &loop_stmt = static_cast<const ast::LoopStatement &>(statement);
    // Resolve count expression if present (loop 5 { ... })
    if (loop_stmt.countExpr) {
      resolveExpression(*loop_stmt.countExpr);
    }
    // Resolve condition if present (loop while condition { ... })
    if (loop_stmt.condition) {
      resolveExpression(*loop_stmt.condition);
    }
    // Resolve body - loop body creates a new scope
    if (loop_stmt.body) {
      resolveStatement(*loop_stmt.body);
    }
    break;
  }

  case ast::NodeType::DoWhileStatement: {
    const auto &doWhile_stmt = static_cast<const ast::DoWhileStatement &>(statement);
    if (doWhile_stmt.body) {
      resolveStatement(*doWhile_stmt.body);
    }
    if (doWhile_stmt.condition) {
      resolveExpression(*doWhile_stmt.condition);
    }
    break;
  }

  case ast::NodeType::ForStatement: {
    const auto &for_stmt = static_cast<const ast::ForStatement &>(statement);
    // Resolve the iterable expression in the current scope
    if (for_stmt.iterable) {
      resolveExpression(*for_stmt.iterable);
    }
    // Create a new scope for the loop body
    beginScope();
    // Declare iterator variables in the loop scope
    for (const auto &iter : for_stmt.iterators) {
      if (iter) {
        declareLocal(iter->symbol, iter.get(), false);
      }
    }
    // Resolve the body
    if (for_stmt.body) {
      resolveStatement(*for_stmt.body);
    }
    endScope();
    break;
  }

  case ast::NodeType::BlockStatement: {
    beginScope();
    const auto &block = static_cast<const ast::BlockStatement &>(statement);
    for (const auto &nested : block.body) {
      if (nested) {
        resolveStatement(*nested);
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::FunctionDeclaration:
  {
    const auto &fn = static_cast<const ast::FunctionDeclaration &>(statement);
    if (fn.name) {
      // Check if this is a top-level function (in program body, not nested)
      if (top_level_functions_.count(fn.name->symbol) > 0) {
        // Top-level function - create GlobalFunction binding
        ResolvedBinding binding;
        binding.kind = ResolvedBindingKind::GlobalFunction;
        binding.name = fn.name->symbol;
        result_.identifier_bindings[fn.name.get()] = binding;
      } else {
        // Nested function - declare as local
        declareLocal(fn.name->symbol, fn.name.get(), false);
      }
    }
    resolveFunctionDeclaration(fn);
    break;
  }

  case ast::NodeType::TryExpression: {
    const auto &try_stmt = static_cast<const ast::TryExpression &>(statement);
    if (try_stmt.tryBody) {
      resolveStatement(*try_stmt.tryBody);
    }
    if (try_stmt.catchBody) {
      beginScope();
      if (try_stmt.catchVariable) {
        declareLocal(try_stmt.catchVariable->symbol, try_stmt.catchVariable.get(),
                     false);
      }
      resolveStatement(*try_stmt.catchBody);
      endScope();
    }
    if (try_stmt.finallyBlock) {
      resolveStatement(*try_stmt.finallyBlock);
    }
    break;
  }

  case ast::NodeType::WhenBlockStatement: {
    const auto &when_stmt = static_cast<const ast::WhenBlock &>(statement);
    // Resolve condition
    if (when_stmt.condition) {
      resolveExpression(*when_stmt.condition);
    }
    // Resolve statements in a new scope
    beginScope();
    for (const auto &stmt : when_stmt.statements) {
      if (stmt) {
        resolveStatement(*stmt);
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::ModeBlock: {
    const auto &mode_stmt = static_cast<const ast::ModeBlock &>(statement);
    // Resolve statements in a new scope
    beginScope();
    for (const auto &stmt : mode_stmt.statements) {
      if (stmt) {
        resolveStatement(*stmt);
      }
    }
    endScope();
    break;
  }

  case ast::NodeType::ThrowStatement: {
    const auto &throw_stmt = static_cast<const ast::ThrowStatement &>(statement);
    if (throw_stmt.value) {
      resolveExpression(*throw_stmt.value);
    }
    break;
  }

  // Type system declarations - register type names in scope
  case ast::NodeType::StructDeclaration: {
    const auto &structDecl = static_cast<const ast::StructDeclaration &>(statement);
    // Register struct type name in current scope
    // For now, just skip - full type system requires semantic analysis pass
    break;
  }

  case ast::NodeType::EnumDeclaration: {
    const auto &enumDecl = static_cast<const ast::EnumDeclaration &>(statement);
    // Register enum type name in current scope
    break;
  }

  case ast::NodeType::TraitDeclaration: {
    const auto &traitDecl = static_cast<const ast::TraitDeclaration &>(statement);
    // Register trait name in current scope
    break;
  }

  case ast::NodeType::ImplDeclaration: {
    const auto &implDecl = static_cast<const ast::ImplDeclaration &>(statement);
    // Register impl block - methods are added to type
    break;
  }

  default:
    break;
  }
}

void LexicalResolver::resolveExpression(const ast::Expression &expression) {
  switch (expression.kind) {
  case ast::NodeType::Identifier: {
    const auto &id = static_cast<const ast::Identifier &>(expression);
    
    // Skip resolution for global scope identifiers (::x)
    if (id.isGlobalScope) {
      break;
    }
    
    auto binding = resolveIdentifier(id.symbol);
    if (!binding) {
      errors_.push_back("Unresolved identifier '" + id.symbol + "' at " +
                        std::to_string(id.line) + ":" +
                        std::to_string(id.column));
      return;
    }
    noteIdentifierBinding(id, *binding);
    break;
  }

  case ast::NodeType::BinaryExpression: {
    const auto &binary = static_cast<const ast::BinaryExpression &>(expression);
    if (binary.left) {
      resolveExpression(*binary.left);
    }
    if (binary.right) {
      resolveExpression(*binary.right);
    }
    break;
  }

  case ast::NodeType::CallExpression: {
    const auto &call = static_cast<const ast::CallExpression &>(expression);
    if (call.callee) {
      resolveExpression(*call.callee);
    }
    for (const auto &arg : call.args) {
      if (arg) {
        resolveExpression(*arg);
      }
    }
    break;
  }

  case ast::NodeType::AssignmentExpression: {
    const auto &assignment =
        static_cast<const ast::AssignmentExpression &>(expression);
    
    // Handle assignment target - may be implicit declaration
    if (assignment.target && assignment.target->kind == ast::NodeType::Identifier) {
      const auto &ident = static_cast<const ast::Identifier &>(*assignment.target);
      auto binding = resolveIdentifier(ident.symbol);

      if (!binding) {
        // Implicit declaration on first assignment (Option C)
        // Check if we're in global scope (function_index == 0)
        bool isGlobalScope = (function_stack_.size() == 1);
        
        if (isGlobalScope) {
          // Declare as global variable
          uint32_t slot = declareLocal(ident.symbol, &ident, false);
          global_variables_.insert(ident.symbol);
          // Record as HostGlobal for proper LOAD_GLOBAL/STORE_GLOBAL
          ResolvedBinding newBinding;
          newBinding.kind = ResolvedBindingKind::HostGlobal;
          newBinding.slot = 0;  // Slot doesn't matter for HostGlobal
          newBinding.name = ident.symbol;
          newBinding.is_const = false;
          noteIdentifierBinding(ident, newBinding);
        } else {
          // Declare as local variable in current scope
          uint32_t slot = declareLocal(ident.symbol, &ident, false);
          // Record the binding so ByteCompiler can find it
          ResolvedBinding newBinding;
          newBinding.kind = ResolvedBindingKind::Local;
          newBinding.slot = slot;
          newBinding.name = ident.symbol;
          noteIdentifierBinding(ident, newBinding);
        }
      } else {
        // Variable exists, just note the binding
        noteIdentifierBinding(ident, *binding);
      }
    } else if (assignment.target) {
      resolveExpression(*assignment.target);
    }
    
    // Resolve right side
    if (assignment.value) {
      resolveExpression(*assignment.value);
    }
    break;
  }

  case ast::NodeType::MemberExpression: {
    const auto &member = static_cast<const ast::MemberExpression &>(expression);
    if (member.object) {
      resolveExpression(*member.object);
    }
    break;
  }

  case ast::NodeType::IndexExpression: {
    const auto &index = static_cast<const ast::IndexExpression &>(expression);
    if (index.object) {
      resolveExpression(*index.object);
    }
    if (index.index) {
      resolveExpression(*index.index);
    }
    break;
  }

  case ast::NodeType::PipelineExpression: {
    const auto &pipeline = static_cast<const ast::PipelineExpression &>(expression);
    // Resolve all stages in the pipeline
    for (const auto &stage : pipeline.stages) {
      if (stage) {
        resolveExpression(*stage);
      }
    }
    break;
  }

  case ast::NodeType::ArrayLiteral: {
    const auto &array = static_cast<const ast::ArrayLiteral &>(expression);
    for (const auto &element : array.elements) {
      if (element) {
        resolveExpression(*element);
      }
    }
    break;
  }

  case ast::NodeType::TupleExpression: {
    const auto &tuple = static_cast<const ast::TupleExpression &>(expression);
    for (const auto &element : tuple.elements) {
      if (element) {
        resolveExpression(*element);
      }
    }
    break;
  }

  case ast::NodeType::SetExpression: {
    const auto &set = static_cast<const ast::SetExpression &>(expression);
    for (const auto &element : set.elements) {
      if (element) {
        resolveExpression(*element);
      }
    }
    break;
  }

  case ast::NodeType::ObjectLiteral: {
    const auto &object = static_cast<const ast::ObjectLiteral &>(expression);
    for (const auto &pair : object.pairs) {
      if (pair.second) {
        resolveExpression(*pair.second);
      }
    }
    break;
  }

  case ast::NodeType::SpreadExpression: {
    const auto &spread = static_cast<const ast::SpreadExpression &>(expression);
    if (spread.target) {
      resolveExpression(*spread.target);
    }
    break;
  }

  case ast::NodeType::InterpolatedStringExpression: {
    const auto &interp = static_cast<const ast::InterpolatedStringExpression &>(expression);
    // Resolve each expression segment in the current scope
    for (const auto &segment : interp.segments) {
      if (!segment.isString && segment.expression) {
        resolveExpression(*segment.expression);
      }
    }
    break;
  }

  case ast::NodeType::LambdaExpression: {
    const auto &lambda = static_cast<const ast::LambdaExpression &>(expression);
    resolveLambdaExpression(lambda);
    break;
  }

  case ast::NodeType::AwaitExpression: {
    const auto &await_expr =
        static_cast<const ast::AwaitExpression &>(expression);
    if (await_expr.argument) {
      resolveExpression(*await_expr.argument);
    }
    break;
  }

  case ast::NodeType::AsyncExpression: {
    const auto &async_expr =
        static_cast<const ast::AsyncExpression &>(expression);
    if (async_expr.body) {
      resolveStatement(*async_expr.body);
    }
    break;
  }

  case ast::NodeType::UpdateExpression: {
    const auto &update_expr =
        static_cast<const ast::UpdateExpression &>(expression);
    if (update_expr.argument) {
      resolveExpression(*update_expr.argument);
    }
    break;
  }

  default:
    break;
  }
}

std::optional<ResolvedBinding> LexicalResolver::resolveIdentifier(
    const std::string &name) {
  if (function_stack_.empty()) {
    return std::nullopt;
  }
  return resolveIdentifierInFunction(name, function_stack_.size() - 1);
}

std::optional<ResolvedBinding> LexicalResolver::resolveIdentifierInFunction(
    const std::string &name, size_t function_index) {
  if (function_index >= function_stack_.size()) {
    return std::nullopt;
  }

  // FIRST: Check if this is a global variable - if so, return immediately
  // Globals should NEVER become locals or upvalues
  if (global_variables_.count(name) > 0) {
    return ResolvedBinding{ResolvedBindingKind::HostGlobal, 0, 0, name, false};
  }

  auto &ctx = function_stack_[function_index];
  for (size_t sc = ctx.scopes.size(); sc > 0; --sc) {
    const auto &scope = ctx.scopes[sc - 1];
    auto it = scope.find(name);
    if (it == scope.end()) {
      continue;
    }

    return ResolvedBinding{ResolvedBindingKind::Local, it->second.slot,
                           static_cast<uint32_t>(function_stack_.size() - 1 -
                                                 function_index),
                           name, it->second.is_const};
  }

  if (function_index == 0) {
    // In global scope, check builtins and host globals
    if (top_level_structs_.find(name) != top_level_structs_.end()) {
      return ResolvedBinding{ResolvedBindingKind::Builtin, 0, 0, name, false};
    }
    if (top_level_functions_.find(name) != top_level_functions_.end()) {
      return ResolvedBinding{ResolvedBindingKind::GlobalFunction, 0, 0, name,
                             false};
    }

    if (host_globals_.find(name) != host_globals_.end()) {
      return ResolvedBinding{ResolvedBindingKind::HostGlobal, 0, 0, name,
                             false};
    }

    if (builtins_.find(name) != builtins_.end()) {
      return ResolvedBinding{ResolvedBindingKind::Builtin, 0, 0, name, false};
    }

    // Check for implicitly declared global variables
    if (!function_stack_.empty() && !function_stack_[0].scopes.empty()) {
      for (size_t sc = function_stack_[0].scopes.size(); sc > 0; --sc) {
        const auto &scope = function_stack_[0].scopes[sc - 1];
        auto it = scope.find(name);
        if (it != scope.end()) {
          return ResolvedBinding{ResolvedBindingKind::Local, it->second.slot,
                                 0, name, it->second.is_const};
        }
      }
    }

    return std::nullopt;
  }

  auto enclosing = resolveIdentifierInFunction(name, function_index - 1);
  if (!enclosing) {
    return std::nullopt;
  }

  if (enclosing->kind == ResolvedBindingKind::GlobalFunction ||
      enclosing->kind == ResolvedBindingKind::HostGlobal ||
      enclosing->kind == ResolvedBindingKind::Builtin) {
    return enclosing;
  }

  if (enclosing->kind == ResolvedBindingKind::Local) {
    uint32_t upvalue_slot = addUpvalue(function_index, name, enclosing->slot, true);
    return ResolvedBinding{ResolvedBindingKind::Upvalue, upvalue_slot,
                           static_cast<uint32_t>(function_stack_.size() - 1 -
                                                 function_index),
                           name, enclosing->is_const};
  }

  uint32_t upvalue_slot =
      addUpvalue(function_index, name, enclosing->slot, false);
  return ResolvedBinding{ResolvedBindingKind::Upvalue, upvalue_slot,
                         static_cast<uint32_t>(function_stack_.size() - 1 -
                                               function_index),
                         name, enclosing->is_const};
}

uint32_t LexicalResolver::addUpvalue(size_t function_index,
                                     const std::string &name,
                                     uint32_t source_index,
                                     bool captures_local) {
  auto &ctx = function_stack_[function_index];
  auto it = ctx.upvalue_slots.find(name);
  if (it != ctx.upvalue_slots.end()) {
    return it->second;
  }

  uint32_t slot = static_cast<uint32_t>(ctx.upvalues.size());
  ctx.upvalues.push_back(
      UpvalueDescriptor{.index = source_index, .captures_local = captures_local});
  ctx.upvalue_slots[name] = slot;
  return slot;
}

void LexicalResolver::noteIdentifierBinding(const ast::Identifier &identifier,
                                            const ResolvedBinding &binding) {
  result_.identifier_bindings[&identifier] = binding;
}

} // namespace havel::compiler
