#pragma once

#include "../../ast/AST.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel::compiler {

enum class ResolvedBindingKind {
  Local,
  Upvalue,
  GlobalFunction,
  Builtin
};

struct ResolvedBinding {
  ResolvedBindingKind kind = ResolvedBindingKind::Local;
  uint32_t slot = 0;
  uint32_t scope_distance = 0;
  std::string name;
};

struct LexicalResolutionResult {
  std::unordered_map<const ast::Identifier *, ResolvedBinding> identifier_bindings;
  std::unordered_map<const ast::FunctionDeclaration *,
                     std::vector<std::string>>
      captured_variables;
};

class LexicalResolver {
public:
  LexicalResolutionResult resolve(const ast::Program &program);
  const std::vector<std::string> &errors() const { return errors_; }

private:
  struct FunctionContext {
    const ast::FunctionDeclaration *owner = nullptr;
    std::vector<std::unordered_map<std::string, uint32_t>> scopes;
    uint32_t next_slot = 0;
  };

  LexicalResolutionResult result_;
  std::vector<std::string> errors_;
  std::unordered_set<std::string> top_level_functions_;
  std::vector<FunctionContext> function_stack_;
  std::unordered_set<std::string> builtins_{"print", "sleep_ms", "clock_ms"};

  void collectTopLevelFunctions(const ast::Program &program);

  void beginFunction(const ast::FunctionDeclaration *function);
  void endFunction();
  void beginScope();
  void endScope();
  uint32_t declareLocal(const std::string &name);

  void resolveStatement(const ast::Statement &statement);
  void resolveExpression(const ast::Expression &expression);
  void resolveFunctionDeclaration(const ast::FunctionDeclaration &function);

  std::optional<ResolvedBinding> resolveIdentifier(const std::string &name) const;
  void noteIdentifierBinding(const ast::Identifier &identifier,
                             const ResolvedBinding &binding);
};

} // namespace havel::compiler

