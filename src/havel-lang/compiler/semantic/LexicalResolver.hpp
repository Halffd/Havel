#pragma once

#include "../core/BytecodeIR.hpp"
#include "../../ast/AST.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace havel::compiler {

enum class ResolvedBindingKind {
  Local,
  Upvalue,
  Global,       // Global variable
  Function,     // User-defined function
  HostFunction  // Built-in host function (print, type, etc.)
};

struct ResolvedBinding {
  ResolvedBindingKind kind = ResolvedBindingKind::Local;
  uint32_t slot = 0;
  uint32_t scope_distance = 0;
  std::string name;
  bool is_const = false;
};

struct LexicalResolutionResult {
  std::unordered_map<const ast::Identifier *, ResolvedBinding> identifier_bindings;
  std::unordered_map<const ast::Identifier *, uint32_t> declaration_slots;
  std::unordered_map<const ast::FunctionDeclaration *, std::vector<UpvalueDescriptor>>
      function_upvalues;
  std::unordered_map<const ast::LambdaExpression *, std::vector<UpvalueDescriptor>>
      lambda_upvalues;
  std::unordered_set<std::string> global_variables;  // Top-level let declarations
};

class LexicalResolver {
public:
  LexicalResolver() = default;

  LexicalResolutionResult resolve(const ast::Program &program);
  const std::vector<std::string> &errors() const { return errors_; }

private:
  struct FunctionContext {
    struct LocalSymbol {
      uint32_t slot = 0;
      bool is_const = false;
    };
    const ast::ASTNode *owner = nullptr;
    std::vector<std::unordered_map<std::string, LocalSymbol>> scopes;
    std::unordered_map<std::string, uint32_t> upvalue_slots;
    std::vector<UpvalueDescriptor> upvalues;
    uint32_t next_slot = 0;
  };

  LexicalResolutionResult result_;
  std::vector<std::string> errors_;
  std::unordered_set<std::string> top_level_functions_;
  std::unordered_set<std::string> top_level_structs_;
  std::unordered_set<std::string> global_variables_;  // Top-level let declarations
  std::vector<FunctionContext> function_stack_;

  // Simple resolution: if not local/upvalue, it's a global
  // Runtime decides if it exists or errors

  void collectTopLevelFunctions(const ast::Program &program);
  void collectTopLevelStructs(const ast::Program &program);

  void beginFunction(const ast::ASTNode *function);
  void endFunction();
  void beginScope();
  void endScope();
  uint32_t declareLocal(const std::string &name,
                        const ast::Identifier *declaration = nullptr,
                        bool is_const = false);
  uint32_t declareLocalUnchecked(const std::string &name,
                                  const ast::Identifier *declaration = nullptr,
                                  bool is_const = false);

  void resolveStatement(const ast::Statement &statement);
  void resolveExpression(const ast::Expression &expression);
  void resolveFunctionDeclaration(const ast::FunctionDeclaration &function);
  void resolveLambdaExpression(const ast::LambdaExpression &lambda);
  void collectPatternIdentifiers(const ast::Expression &pattern);
  void resolvePatternWithBindings(const ast::Expression &pattern);

  std::optional<ResolvedBinding> resolveIdentifier(const std::string &name);
  std::optional<ResolvedBinding> resolveIdentifierInFunction(
      const std::string &name, size_t function_index);
  uint32_t addUpvalue(size_t function_index, const std::string &name,
                      uint32_t source_index, bool captures_local);
  void noteIdentifierBinding(const ast::Identifier &identifier,
                             const ResolvedBinding &binding);
};

} // namespace havel::compiler
