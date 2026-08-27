#pragma once

#include "LexicalResolver.hpp"
#include "TypeChecker.hpp"
#include "../../ast/BootstrapAST.h"
#include <string>
#include <vector>
#include <unordered_set>

namespace havel::compiler {

/// Result of semantic analysis
struct SemanticAnalysisResult {
  LexicalResolutionResult lexicalResolution;
  TypeCheckResult typeCheckResult;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  std::vector<std::string> info;

  bool hasErrors() const { return !errors.empty(); }
  bool hasWarnings() const { return !warnings.empty(); }
};

/// SemanticAnalyzer - standalone semantic analysis pass
/// Runs lexical resolution + type checking and collects diagnostics
/// Can be used independently of bytecode compilation
class SemanticAnalyzer {
public:
  struct Options {
    bool checkTypes = true;
    bool resolveNames = true;
    bool collectErrors = true;
    bool treatUndefinedAsError = true;
    std::unordered_set<std::string> knownGlobals;
    std::unordered_set<std::string> knownClassNames;
    std::unordered_set<std::string> knownStructNames;
    std::unordered_set<std::string> knownProtocolNames;
    std::unordered_set<std::string> knownImplNames;

    Options();
  };

  explicit SemanticAnalyzer(const Options& opts = Options());
  ~SemanticAnalyzer() = default;

  /// Run full semantic analysis on a program
  SemanticAnalysisResult analyze(const ast::Program& program);

  /// Run only lexical resolution (name/scope binding)
  LexicalResolutionResult resolveNames(const ast::Program& program);

  /// Run only type checking
  TypeCheckResult checkTypes(const ast::Program& program,
                             const LexicalResolutionResult& resolution = {});

  /// Get last analysis errors
  const std::vector<std::string>& getErrors() const { return lastErrors_; }
  const std::vector<std::string>& getWarnings() const { return lastWarnings_; }

private:
  Options options_;
  std::vector<std::string> lastErrors_;
  std::vector<std::string> lastWarnings_;

  LexicalResolver lexicalResolver_;
  TypeChecker typeChecker_;
};

} // namespace havel::compiler