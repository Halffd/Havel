#include "LexicalResolver.hpp"

namespace havel::compiler {

LexicalResolutionResult LexicalResolver::resolve(const ast::Program &program) {
  result_ = {};
  errors_.clear();
  top_level_functions_.clear();
  function_stack_.clear();

  collectTopLevelFunctions(program);

  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }

    if (statement->kind == ast::NodeType::FunctionDeclaration) {
      resolveFunctionDeclaration(
          static_cast<const ast::FunctionDeclaration &>(*statement));
    }
  }

  beginFunction(nullptr);
  for (const auto &statement : program.body) {
    if (!statement) {
      continue;
    }

    if (statement->kind == ast::NodeType::FunctionDeclaration) {
      continue;
    }

    resolveStatement(*statement);
  }
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

void LexicalResolver::beginFunction(const ast::FunctionDeclaration *function) {
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
    result_.function_upvalues[ctx.owner] = ctx.upvalues;
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
                                       const ast::Identifier *declaration) {
  auto &ctx = function_stack_.back();
  if (ctx.scopes.empty()) {
    beginScope();
  }

  auto &scope = ctx.scopes.back();
  auto it = scope.find(name);
  if (it != scope.end()) {
    return it->second;
  }

  uint32_t slot = ctx.next_slot++;
  scope[name] = slot;
  if (declaration) {
    result_.declaration_slots[declaration] = slot;
  }
  return slot;
}

void LexicalResolver::resolveFunctionDeclaration(
    const ast::FunctionDeclaration &function) {
  beginFunction(&function);

  for (const auto &param : function.parameters) {
    if (param && param->paramName) {
      declareLocal(param->paramName->symbol, param->paramName.get());
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
      declareLocal(identifier->symbol, identifier);
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
        declareLocal(iter->symbol, iter.get());
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
        declareLocal(fn.name->symbol, fn.name.get());
      }
    }
    resolveFunctionDeclaration(fn);
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
    auto binding = resolveIdentifier(id.symbol);
    if (!binding) {
      errors_.push_back("Unresolved identifier: " + id.symbol);
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
    if (assignment.target) {
      resolveExpression(*assignment.target);
    }
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

  case ast::NodeType::ArrayLiteral: {
    const auto &array = static_cast<const ast::ArrayLiteral &>(expression);
    for (const auto &element : array.elements) {
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

  auto &ctx = function_stack_[function_index];
  for (size_t sc = ctx.scopes.size(); sc > 0; --sc) {
    const auto &scope = ctx.scopes[sc - 1];
    auto it = scope.find(name);
    if (it == scope.end()) {
      continue;
    }

    return ResolvedBinding{ResolvedBindingKind::Local, it->second,
                           static_cast<uint32_t>(function_stack_.size() - 1 -
                                                 function_index),
                           name};
  }

  if (function_index == 0) {
    if (top_level_functions_.find(name) != top_level_functions_.end()) {
      return ResolvedBinding{ResolvedBindingKind::GlobalFunction, 0, 0, name};
    }

    if (host_globals_.find(name) != host_globals_.end()) {
      return ResolvedBinding{ResolvedBindingKind::HostGlobal, 0, 0, name};
    }

    if (builtins_.find(name) != builtins_.end()) {
      return ResolvedBinding{ResolvedBindingKind::Builtin, 0, 0, name};
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
                           name};
  }

  uint32_t upvalue_slot =
      addUpvalue(function_index, name, enclosing->slot, false);
  return ResolvedBinding{ResolvedBindingKind::Upvalue, upvalue_slot,
                         static_cast<uint32_t>(function_stack_.size() - 1 -
                                               function_index),
                         name};
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
