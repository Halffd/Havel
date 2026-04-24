#include "CompilerUtils.hpp"
#include "../../../utils/Logger.hpp"
#include <cctype>
#include <iostream>

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

void CompilerUtils::printBinding(const ResolvedBinding& binding) {
    havel::debug("Binding: {} [{}] slot={} distance={}",
                 binding.name, bindingKindToString(binding.kind),
                 binding.slot, binding.scope_distance);
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

} // namespace havel::compiler
