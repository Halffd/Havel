#include "LexicalResolver.hpp"

#include <algorithm>

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
  if (!function_stack_.empty()) {
    function_stack_.pop_back();
  }
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
      auto existing = resolveIdentifier(identifier->symbol);
      if (existing && existing->kind == ResolvedBindingKind::Local) {
        // Current language behavior treats repeated `let x = ...` in nested
        // blocks as rebinding the same storage slot.
        result_.declaration_slots[identifier] = existing->slot;
      } else {
        declareLocal(identifier->symbol, identifier);
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
      // Allow nested function references from the surrounding function scope.
      declareLocal(fn.name->symbol, fn.name.get());
    }
    resolveFunctionDeclaration(fn);
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

  case ast::NodeType::MemberExpression: {
    const auto &member = static_cast<const ast::MemberExpression &>(expression);
    if (member.object) {
      resolveExpression(*member.object);
    }
    break;
  }

  default:
    break;
  }
}

std::optional<ResolvedBinding>
LexicalResolver::resolveIdentifier(const std::string &name) const {
  if (function_stack_.empty()) {
    return std::nullopt;
  }

  for (size_t fn = function_stack_.size(); fn > 0; --fn) {
    const auto &ctx = function_stack_[fn - 1];

    for (size_t sc = ctx.scopes.size(); sc > 0; --sc) {
      const auto &scope = ctx.scopes[sc - 1];
      auto it = scope.find(name);
      if (it == scope.end()) {
        continue;
      }

      ResolvedBinding binding;
      binding.name = name;
      binding.slot = it->second;
      binding.scope_distance = static_cast<uint32_t>(function_stack_.size() - fn);
      binding.kind = (fn == function_stack_.size()) ? ResolvedBindingKind::Local
                                                    : ResolvedBindingKind::Upvalue;
      return binding;
    }
  }

  if (top_level_functions_.find(name) != top_level_functions_.end()) {
    return ResolvedBinding{ResolvedBindingKind::GlobalFunction, 0, 0, name};
  }

  if (builtins_.find(name) != builtins_.end()) {
    return ResolvedBinding{ResolvedBindingKind::Builtin, 0, 0, name};
  }

  return std::nullopt;
}

void LexicalResolver::noteIdentifierBinding(const ast::Identifier &identifier,
                                            const ResolvedBinding &binding) {
  result_.identifier_bindings[&identifier] = binding;

  if (binding.kind != ResolvedBindingKind::Upvalue || function_stack_.empty()) {
    return;
  }

  const auto *current_function = function_stack_.back().owner;
  if (!current_function) {
    return;
  }

  auto &captures = result_.captured_variables[current_function];
  if (std::find(captures.begin(), captures.end(), binding.name) == captures.end()) {
    captures.push_back(binding.name);
  }
}

} // namespace havel::compiler
