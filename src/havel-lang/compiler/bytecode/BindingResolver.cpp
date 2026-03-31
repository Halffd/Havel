#include "BindingResolver.hpp"
#include "AST.h"

namespace havel::compiler {

BindingResolver::BindingResolver(LexicalResolutionResult& result) : result_(result) {}

void BindingResolver::beginFunction(const ast::ASTNode* function) {
  FunctionContext ctx;
  ctx.owner = function;
  ctx.nextSlot = 0;
  functionStack_.push_back(std::move(ctx));
  beginScope();
}

void BindingResolver::endFunction() {
  if (!functionStack_.empty()) {
    // Store upvalues in result before popping
    auto& ctx = functionStack_.back();
    if (ctx.owner) {
      if (ctx.owner->kind == ast::NodeType::FunctionDeclaration) {
        result_.function_upvalues[static_cast<const ast::FunctionDeclaration*>(ctx.owner)] = ctx.upvalues;
      } else if (ctx.owner->kind == ast::NodeType::LambdaExpression) {
        result_.lambda_upvalues[static_cast<const ast::LambdaExpression*>(ctx.owner)] = ctx.upvalues;
      }
    }
    functionStack_.pop_back();
  }
}

void BindingResolver::beginScope() {
  if (!functionStack_.empty()) {
    functionStack_.back().scopes.emplace_back();
  }
}

void BindingResolver::endScope() {
  if (!functionStack_.empty() && !functionStack_.back().scopes.empty()) {
    functionStack_.back().scopes.pop_back();
  }
}

uint32_t BindingResolver::declareLocal(const std::string& name, const ast::Identifier* declaration, bool isConst) {
  if (functionStack_.empty()) {
    return 0;
  }

  auto& ctx = functionStack_.back();
  if (ctx.scopes.empty()) {
    beginScope();
  }

  auto& scope = ctx.scopes.back();
  auto it = scope.find(name);
  if (it != scope.end()) {
    // Duplicate declaration - return existing slot
    return it->second;
  }

  uint32_t slot = ctx.nextSlot++;
  scope[name] = slot;

  if (declaration) {
    result_.declaration_slots[declaration] = slot;
  }

  return slot;
}

void BindingResolver::declareGlobal(const std::string& name) {
  globalVariables_.insert(name);
}

void BindingResolver::declareTopLevelFunction(const std::string& name) {
  topLevelFunctions_.insert(name);
}

std::optional<ResolvedBinding> BindingResolver::resolveIdentifier(const std::string& name) {
  if (functionStack_.empty()) {
    return std::nullopt;
  }
  return resolveInFunction(name, functionStack_.size() - 1);
}

ResolvedBinding BindingResolver::resolveOrCreateGlobal(const std::string& name) {
  // First try to resolve
  auto binding = resolveIdentifier(name);
  if (binding) {
    return *binding;
  }

  // Create global binding
  declareGlobal(name);
  return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
}

void BindingResolver::recordIdentifierBinding(const ast::Identifier& identifier, const ResolvedBinding& binding) {
  result_.identifier_bindings[&identifier] = binding;
}

void BindingResolver::recordFunctionBinding(const ast::FunctionDeclaration& function, bool isTopLevel) {
  if (!function.name) {
    return;
  }

  const std::string& name = function.name->symbol;

  if (isTopLevel) {
    topLevelFunctions_.insert(name);
    ResolvedBinding binding;
    binding.kind = ResolvedBindingKind::Function;
    binding.name = name;
    result_.identifier_bindings[function.name.get()] = binding;
  } else {
    uint32_t slot = declareLocal(name, function.name.get(), false);
    ResolvedBinding binding;
    binding.kind = ResolvedBindingKind::Local;
    binding.slot = slot;
    binding.name = name;
    binding.is_const = false;
    result_.identifier_bindings[function.name.get()] = binding;
  }
}

uint32_t BindingResolver::addUpvalue(uint32_t sourceIndex, bool capturesLocal) {
  if (functionStack_.empty()) {
    return 0;
  }

  auto& ctx = functionStack_.back();
  uint32_t slot = static_cast<uint32_t>(ctx.upvalues.size());
  ctx.upvalues.push_back(UpvalueDescriptor{sourceIndex, capturesLocal});
  return slot;
}

bool BindingResolver::hasUpvalue(const std::string& name) const {
  if (functionStack_.empty()) {
    return false;
  }
  const auto& ctx = functionStack_.back();
  return ctx.upvalueSlots.count(name) > 0;
}

std::optional<uint32_t> BindingResolver::findUpvalue(const std::string& name) const {
  if (functionStack_.empty()) {
    return std::nullopt;
  }
  const auto& ctx = functionStack_.back();
  auto it = ctx.upvalueSlots.find(name);
  if (it != ctx.upvalueSlots.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool BindingResolver::isTopLevelFunction(const std::string& name) const {
  return topLevelFunctions_.count(name) > 0;
}

bool BindingResolver::isGlobalVariable(const std::string& name) const {
  return globalVariables_.count(name) > 0;
}

void BindingResolver::addError(const std::string& message) {
  errors_.push_back(message);
}

std::optional<ResolvedBinding> BindingResolver::resolveInFunction(const std::string& name, size_t functionIndex) {
  if (functionIndex >= functionStack_.size()) {
    return std::nullopt;
  }

  // Check if it's a global variable
  if (globalVariables_.count(name) > 0) {
    return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
  }

  // If at global scope, check top-level functions
  if (functionIndex == 0) {
    if (topLevelFunctions_.count(name) > 0) {
      return ResolvedBinding{ResolvedBindingKind::Function, 0, 0, name, false};
    }
    return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
  }

  // Search in current function's scopes
  auto& ctx = functionStack_[functionIndex];
  for (size_t sc = ctx.scopes.size(); sc > 0; --sc) {
    const auto& scope = ctx.scopes[sc - 1];
    auto it = scope.find(name);
    if (it != scope.end()) {
      return ResolvedBinding{
          ResolvedBindingKind::Local, it->second,
          static_cast<uint32_t>(functionStack_.size() - 1 - functionIndex),
          name, false};
    }
  }

  // Recurse to enclosing function
  auto enclosing = resolveInFunction(name, functionIndex - 1);
  if (!enclosing) {
    return ResolvedBinding{ResolvedBindingKind::Global, 0, 0, name, false};
  }

  // Return non-local bindings directly
  if (enclosing->kind == ResolvedBindingKind::Global ||
      enclosing->kind == ResolvedBindingKind::Function ||
      enclosing->kind == ResolvedBindingKind::HostFunction) {
    return enclosing;
  }

  // Create upvalue for local from enclosing scope
  return createUpvalueBinding(name, *enclosing, functionIndex);
}

ResolvedBinding BindingResolver::createUpvalueBinding(const std::string& name, const ResolvedBinding& enclosing, size_t functionIndex) {
  auto& ctx = functionStack_[functionIndex];

  // Check if we already have this upvalue
  auto it = ctx.upvalueSlots.find(name);
  if (it != ctx.upvalueSlots.end()) {
    return ResolvedBinding{
        ResolvedBindingKind::Upvalue, it->second,
        static_cast<uint32_t>(functionStack_.size() - 1 - functionIndex),
        name, enclosing.is_const};
  }

  // Create new upvalue
  uint32_t upvalueSlot = addUpvalue(enclosing.slot, enclosing.kind == ResolvedBindingKind::Local);
  ctx.upvalueSlots[name] = upvalueSlot;

  return ResolvedBinding{
      ResolvedBindingKind::Upvalue, upvalueSlot,
      static_cast<uint32_t>(functionStack_.size() - 1 - functionIndex),
      name, enclosing.is_const};
}

} // namespace havel::compiler
