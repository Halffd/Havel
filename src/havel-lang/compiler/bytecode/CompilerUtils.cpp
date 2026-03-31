#include "CompilerUtils.hpp"
#include <cctype>
#include <sstream>
#include <iomanip>

namespace havel::compiler {

// ============================================================================
// CompilerUtils Implementation
// ============================================================================

std::string CompilerUtils::mangleName(const std::string& name, const std::string& scope) {
  return scope + "::" + name;
}

std::string CompilerUtils::demangleName(const std::string& mangled) {
  size_t pos = mangled.find("::");
  if (pos == std::string::npos) {
    return mangled;
  }
  return mangled.substr(pos + 2);
}

bool CompilerUtils::isValidIdentifier(const std::string& name) {
  if (name.empty()) return false;
  if (!std::isalpha(name[0]) && name[0] != '_') return false;
  for (char c : name) {
    if (!std::isalnum(c) && c != '_') return false;
  }
  return true;
}

std::string CompilerUtils::sanitizeSymbol(const std::string& input) {
  std::string result;
  result.reserve(input.size());
  for (char c : input) {
    if (std::isalnum(c) || c == '_') {
      result += c;
    } else {
      result += '_';
    }
  }
  return result;
}

uint32_t CompilerUtils::allocateSlot(std::vector<bool>& slotMap) {
  for (uint32_t i = 0; i < slotMap.size(); ++i) {
    if (!slotMap[i]) {
      slotMap[i] = true;
      return i;
    }
  }
  slotMap.push_back(true);
  return static_cast<uint32_t>(slotMap.size() - 1);
}

void CompilerUtils::freeSlot(std::vector<bool>& slotMap, uint32_t slot) {
  if (slot < slotMap.size()) {
    slotMap[slot] = false;
  }
}

uint32_t CompilerUtils::countUsedSlots(const std::vector<bool>& slotMap) {
  uint32_t count = 0;
  for (bool used : slotMap) {
    if (used) ++count;
  }
  return count;
}

bool CompilerUtils::isGlobalBinding(const ResolvedBinding& binding) {
  return binding.kind == ResolvedBindingKind::Global;
}

bool CompilerUtils::isLocalBinding(const ResolvedBinding& binding) {
  return binding.kind == ResolvedBindingKind::Local;
}

bool CompilerUtils::isUpvalueBinding(const ResolvedBinding& binding) {
  return binding.kind == ResolvedBindingKind::Upvalue;
}

bool CompilerUtils::isFunctionBinding(const ResolvedBinding& binding) {
  return binding.kind == ResolvedBindingKind::Function;
}

std::string CompilerUtils::bindingKindToString(ResolvedBindingKind kind) {
  switch (kind) {
    case ResolvedBindingKind::Local: return "Local";
    case ResolvedBindingKind::Upvalue: return "Upvalue";
    case ResolvedBindingKind::Global: return "Global";
    case ResolvedBindingKind::Function: return "Function";
    case ResolvedBindingKind::HostFunction: return "HostFunction";
    default: return "Unknown";
  }
}

std::string CompilerUtils::opcodeToString(OpCode opcode) {
  switch (opcode) {
    case OpCode::LOAD_CONST: return "LOAD_CONST";
    case OpCode::LOAD_GLOBAL: return "LOAD_GLOBAL";
    case OpCode::STORE_GLOBAL: return "STORE_GLOBAL";
    case OpCode::LOAD_VAR: return "LOAD_VAR";
    case OpCode::STORE_VAR: return "STORE_VAR";
    case OpCode::LOAD_UPVALUE: return "LOAD_UPVALUE";
    case OpCode::STORE_UPVALUE: return "STORE_UPVALUE";
    case OpCode::POP: return "POP";
    case OpCode::DUP: return "DUP";
    case OpCode::CALL: return "CALL";
    case OpCode::TAIL_CALL: return "TAIL_CALL";
    case OpCode::RETURN: return "RETURN";
    case OpCode::CLOSURE: return "CLOSURE";
    default: return "UNKNOWN";
  }
}

void CompilerUtils::printBinding(const ResolvedBinding& binding) {
  std::cout << "Binding: " << binding.name
            << " [" << bindingKindToString(binding.kind) << "]"
            << " slot=" << binding.slot
            << " distance=" << binding.scope_distance;
}

bool CompilerUtils::validateSlot(uint32_t slot, uint32_t maxSlots) {
  return slot < maxSlots;
}

bool CompilerUtils::validateFunctionIndex(uint32_t index, uint32_t maxFunctions) {
  return index < maxFunctions;
}

bool CompilerUtils::validateConstantIndex(uint32_t index, uint32_t maxConstants) {
  return index < maxConstants;
}

// ============================================================================
// ScopeTracker Implementation
// ============================================================================

void ScopeTracker::enterScope() {
  Scope scope;
  scope.depth = scopes_.empty() ? 0 : scopes_.back().depth + 1;
  scopes_.push_back(std::move(scope));
}

void ScopeTracker::exitScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

bool ScopeTracker::declareVariable(const std::string& name, uint32_t slot, bool isConst) {
  if (scopes_.empty()) {
    enterScope();
  }
  auto& scope = scopes_.back();
  if (scope.locals.count(name) > 0) {
    return false; // Already declared in this scope
  }
  scope.locals[name] = slot;
  if (isConst) {
    scope.consts.insert(name);
  }
  nextSlot_ = std::max(nextSlot_, slot + 1);
  return true;
}

std::optional<uint32_t> ScopeTracker::lookupVariable(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto localIt = it->locals.find(name);
    if (localIt != it->locals.end()) {
      return localIt->second;
    }
  }
  return std::nullopt;
}

bool ScopeTracker::isVariableConst(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    if (it->consts.count(name) > 0) {
      return true;
    }
    if (it->locals.count(name) > 0) {
      return false;
    }
  }
  return false;
}

bool ScopeTracker::hasVariable(const std::string& name) const {
  return lookupVariable(name).has_value();
}

void ScopeTracker::reserveSlot(uint32_t slot) {
  nextSlot_ = std::max(nextSlot_, slot + 1);
}

void ScopeTracker::releaseSlot(uint32_t slot) {
  (void)slot; // Slot release for future optimization
}

std::vector<std::string> ScopeTracker::getCurrentScopeVariables() const {
  std::vector<std::string> result;
  if (!scopes_.empty()) {
    for (const auto& [name, slot] : scopes_.back().locals) {
      result.push_back(name);
    }
  }
  return result;
}

// ============================================================================
// FunctionContext Implementation
// ============================================================================

void FunctionContext::beginFunction(uint32_t functionIndex) {
  currentFunction_ = functionIndex;
  scopes_.clear();
  scopes_.emplace_back(); // Add root scope
  upvalues_.clear();
  upvalueMap_.clear();
  parameters_.clear();
  nextSlot_ = 0;
  parameterCount_ = 0;
}

void FunctionContext::endFunction() {
  currentFunction_.reset();
}

uint32_t FunctionContext::declareLocal(const std::string& name, bool isConst) {
  if (scopes_.empty()) {
    scopes_.emplace_back();
  }
  uint32_t slot = nextSlot_++;
  LocalInfo info{slot, isConst, false};
  scopes_.back()[name] = info;
  return slot;
}

std::optional<uint32_t> FunctionContext::findLocal(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto localIt = it->find(name);
    if (localIt != it->end()) {
      return localIt->second.slot;
    }
  }
  return std::nullopt;
}

bool FunctionContext::isLocalConst(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto localIt = it->find(name);
    if (localIt != it->end()) {
      return localIt->second.isConst;
    }
  }
  return false;
}

void FunctionContext::markLocalAsCaptured(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto localIt = it->find(name);
    if (localIt != it->end()) {
      localIt->second.isCaptured = true;
      return;
    }
  }
}

uint32_t FunctionContext::addUpvalue(uint32_t sourceIndex, bool capturesLocal) {
  uint32_t slot = static_cast<uint32_t>(upvalues_.size());
  upvalues_.push_back(UpvalueDescriptor{sourceIndex, capturesLocal});
  return slot;
}

std::optional<uint32_t> FunctionContext::findUpvalue(const std::string& name) const {
  auto it = upvalueMap_.find(name);
  if (it != upvalueMap_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void FunctionContext::addParameter(const std::string& name) {
  parameters_.insert(name);
  ++parameterCount_;
  declareLocal(name, false);
}

bool FunctionContext::isParameter(const std::string& name) const {
  return parameters_.count(name) > 0;
}

} // namespace havel::compiler
