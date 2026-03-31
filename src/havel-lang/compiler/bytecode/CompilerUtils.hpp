#pragma once

#include "BytecodeIR.hpp"
#include "LexicalResolver.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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
  static std::string opcodeToString(OpCode opcode);
  static void printBinding(const ResolvedBinding& binding);

  // ============================================================================
  // Validation utilities
  // ============================================================================
  static bool validateSlot(uint32_t slot, uint32_t maxSlots);
  static bool validateFunctionIndex(uint32_t index, uint32_t maxFunctions);
  static bool validateConstantIndex(uint32_t index, uint32_t maxConstants);
};

// ============================================================================
// ScopeTracker - Helper for managing nested scopes during compilation
// ============================================================================
class ScopeTracker {
public:
  struct Scope {
    std::unordered_map<std::string, uint32_t> locals;
    std::unordered_set<std::string> consts;
    uint32_t depth = 0;
  };

  void enterScope();
  void exitScope();
  bool isAtGlobalScope() const { return scopes_.size() <= 1; }
  uint32_t currentDepth() const { return scopes_.empty() ? 0 : scopes_.back().depth; }

  // Variable management
  bool declareVariable(const std::string& name, uint32_t slot, bool isConst = false);
  std::optional<uint32_t> lookupVariable(const std::string& name) const;
  bool isVariableConst(const std::string& name) const;
  bool hasVariable(const std::string& name) const;

  // Slot management
  uint32_t getNextSlot() const { return nextSlot_; }
  void reserveSlot(uint32_t slot);
  void releaseSlot(uint32_t slot);

  // Get all variables in current scope
  std::vector<std::string> getCurrentScopeVariables() const;

private:
  std::vector<Scope> scopes_;
  uint32_t nextSlot_ = 0;
};

// ============================================================================
// FunctionContext - Manages function compilation context
// ============================================================================
class FunctionContext {
public:
  struct LocalInfo {
    uint32_t slot = 0;
    bool isConst = false;
    bool isCaptured = false;
  };

  void beginFunction(uint32_t functionIndex);
  void endFunction();
  bool isInFunction() const { return currentFunction_.has_value(); }
  uint32_t currentFunctionIndex() const { return currentFunction_.value_or(UINT32_MAX); }

  // Local management
  uint32_t declareLocal(const std::string& name, bool isConst = false);
  std::optional<uint32_t> findLocal(const std::string& name) const;
  bool isLocalConst(const std::string& name) const;
  void markLocalAsCaptured(const std::string& name);

  // Upvalue management
  uint32_t addUpvalue(uint32_t sourceIndex, bool capturesLocal);
  std::optional<uint32_t> findUpvalue(const std::string& name) const;
  const std::vector<UpvalueDescriptor>& getUpvalues() const { return upvalues_; }

  // Parameter management
  void addParameter(const std::string& name);
  bool isParameter(const std::string& name) const;
  uint32_t getParameterCount() const { return parameterCount_; }

private:
  std::optional<uint32_t> currentFunction_;
  std::vector<std::unordered_map<std::string, LocalInfo>> scopes_;
  std::vector<UpvalueDescriptor> upvalues_;
  std::unordered_map<std::string, uint32_t> upvalueMap_;
  std::unordered_set<std::string> parameters_;
  uint32_t nextSlot_ = 0;
  uint32_t parameterCount_ = 0;
};

} // namespace havel::compiler
