#pragma once

#include "BytecodeIR.hpp"
#include "../semantic/LexicalResolver.hpp"
#include <string>
#include <vector>

namespace havel::compiler {

// ============================================================================
// CompilerUtils - Shared utilities for compiler components
// ============================================================================
class CompilerUtils {
public:
  // ============================================================================
  // String utilities
  // ============================================================================
  static std::string mangleName(const std::string& name, const std::string& scope);
  static std::string demangleName(const std::string& mangled);
  static bool isValidIdentifier(const std::string& name);
  static std::string sanitizeSymbol(const std::string& input);

  // ============================================================================
  // Slot management utilities
  // ============================================================================
  static uint32_t allocateSlot(std::vector<bool>& slotMap);
  static void freeSlot(std::vector<bool>& slotMap, uint32_t slot);
  static uint32_t countUsedSlots(const std::vector<bool>& slotMap);

  // ============================================================================
  // Binding utilities
  // ============================================================================
  static bool isGlobalBinding(const ResolvedBinding& binding);
  static bool isLocalBinding(const ResolvedBinding& binding);
  static bool isUpvalueBinding(const ResolvedBinding& binding);
  static bool isFunctionBinding(const ResolvedBinding& binding);

// ============================================================================
// Debug utilities
// ============================================================================
static std::string bindingKindToString(ResolvedBindingKind kind);
static void printBinding(const ResolvedBinding& binding);

  // ============================================================================
  // Validation utilities
  // ============================================================================
  static bool validateSlot(uint32_t slot, uint32_t maxSlots);
  static bool validateFunctionIndex(uint32_t index, uint32_t maxFunctions);
  static bool validateConstantIndex(uint32_t index, uint32_t maxConstants);
};



} // namespace havel::compiler
