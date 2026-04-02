#pragma once

#include "LexicalResolver.hpp"
#include "../core/CompilerUtils.hpp"
#include "../vm/Closure.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace havel::compiler {

// Forward declarations
struct LexicalResolutionResult;

// ============================================================================
// BindingResolver - Handles identifier resolution and binding creation
// ============================================================================
class BindingResolver {
public:
  struct FunctionContext {
    std::vector<std::unordered_map<std::string, uint32_t>> scopes;
    std::unordered_map<std::string, uint32_t> upvalueSlots;
    std::vector<UpvalueDescriptor> upvalues;
    uint32_t nextSlot = 0;
    const ast::ASTNode* owner = nullptr;
  };

  explicit BindingResolver(LexicalResolutionResult& result);

  // ============================================================================
  // Scope management
  // ============================================================================
  void beginFunction(const ast::ASTNode* function);
  void endFunction();
  void beginScope();
  void endScope();

  // ============================================================================
  // Variable declaration
  // ============================================================================
  uint32_t declareLocal(const std::string& name, const ast::Identifier* declaration, bool isConst);
  void declareGlobal(const std::string& name);
  void declareTopLevelFunction(const std::string& name);

  // ============================================================================
  // Identifier resolution
  // ============================================================================
  std::optional<ResolvedBinding> resolveIdentifier(const std::string& name);
  ResolvedBinding resolveOrCreateGlobal(const std::string& name);

  // ============================================================================
  // Binding recording
  // ============================================================================
  void recordIdentifierBinding(const ast::Identifier& identifier, const ResolvedBinding& binding);
  void recordFunctionBinding(const ast::FunctionDeclaration& function, bool isTopLevel);

  // ============================================================================
  // Upvalue management
  // ============================================================================
  uint32_t addUpvalue(uint32_t sourceIndex, bool capturesLocal);
  bool hasUpvalue(const std::string& name) const;
  std::optional<uint32_t> findUpvalue(const std::string& name) const;

  // ============================================================================
  // State queries
  // ============================================================================
  bool isInFunction() const { return !functionStack_.empty(); }
  bool isAtGlobalScope() const { return functionStack_.size() <= 1; }
  bool isTopLevelFunction(const std::string& name) const;
  bool isGlobalVariable(const std::string& name) const;
  size_t getFunctionDepth() const { return functionStack_.size(); }

  // ============================================================================
  // Error handling
  // ============================================================================
  void addError(const std::string& message);
  const std::vector<std::string>& getErrors() const { return errors_; }
  bool hasErrors() const { return !errors_.empty(); }

  // ============================================================================
  // Result access
  // ============================================================================
  LexicalResolutionResult& getResult() { return result_; }

private:
  LexicalResolutionResult& result_;
  std::vector<FunctionContext> functionStack_;
  std::unordered_set<std::string> topLevelFunctions_;
  std::unordered_set<std::string> globalVariables_;
  std::vector<std::string> errors_;

  // Internal resolution helpers
  std::optional<ResolvedBinding> resolveInFunction(const std::string& name, size_t functionIndex);
  std::optional<ResolvedBinding> resolveInScope(const std::string& name, size_t scopeIndex);
  ResolvedBinding createUpvalueBinding(const std::string& name, const ResolvedBinding& enclosing, size_t functionIndex);
};

} // namespace havel::compiler
