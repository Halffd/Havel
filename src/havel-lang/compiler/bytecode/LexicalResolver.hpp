#pragma once

#include "BytecodeIR.hpp"
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
  GlobalFunction,
  HostGlobal,
  Builtin
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
};

class LexicalResolver {
public:
  LexicalResolver() = default;
  explicit LexicalResolver(std::unordered_set<std::string> builtins,
                           std::unordered_set<std::string> host_globals = {})
      : builtins_(std::move(builtins)), host_globals_(std::move(host_globals)) {}

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
  std::vector<FunctionContext> function_stack_;
  std::unordered_set<std::string> builtins_{
      "print",      "sleep_ms",        "clock_ms",
      "system.gc",  "system.gcStats",  "system_gc",
      "system_gcStats"};
  std::unordered_set<std::string> host_globals_{
      "print", "sleep", "sleep_ms", "clock_ms", "time.now", "fmt",
      "window", "io", "system", "hotkey", "mode", "process", "async",
      "thread", "interval", "timeout", "struct"};

  void collectTopLevelFunctions(const ast::Program &program);
  void collectTopLevelStructs(const ast::Program &program);

  void beginFunction(const ast::ASTNode *function);
  void endFunction();
  void beginScope();
  void endScope();
  uint32_t declareLocal(const std::string &name,
                        const ast::Identifier *declaration = nullptr,
                        bool is_const = false);

  void resolveStatement(const ast::Statement &statement);
  void resolveExpression(const ast::Expression &expression);
  void resolveFunctionDeclaration(const ast::FunctionDeclaration &function);
  void resolveLambdaExpression(const ast::LambdaExpression &lambda);
  void collectPatternIdentifiers(const ast::Expression &pattern);

  std::optional<ResolvedBinding> resolveIdentifier(const std::string &name);
  std::optional<ResolvedBinding> resolveIdentifierInFunction(
      const std::string &name, size_t function_index);
  uint32_t addUpvalue(size_t function_index, const std::string &name,
                      uint32_t source_index, bool captures_local);
  void noteIdentifierBinding(const ast::Identifier &identifier,
                             const ResolvedBinding &binding);
};

} // namespace havel::compiler
