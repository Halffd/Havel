#include "SemanticAnalyzer.hpp"

namespace havel::compiler {

SemanticAnalyzer::Options::Options() = default;

SemanticAnalyzer::SemanticAnalyzer(const Options& opts) : options_(opts) {
  lexicalResolver_.setKnownGlobals(options_.knownGlobals);
  if (options_.treatUndefinedAsError) {
    lexicalResolver_.setStrictMode(true);
  }
}

SemanticAnalysisResult SemanticAnalyzer::analyze(const ast::Program& program) {
  SemanticAnalysisResult result;
  lastErrors_.clear();
  lastWarnings_.clear();

  // Step 1: Lexical resolution (name/scope binding)
  if (options_.resolveNames) {
    result.lexicalResolution = lexicalResolver_.resolve(program);
    
    // Collect lexical resolution errors
    for (const auto& err : lexicalResolver_.errors()) {
      result.errors.push_back("SemanticError: " + err);
      lastErrors_.push_back(err);
    }
  }

  // Step 2: Type checking
  if (options_.checkTypes) {
    result.typeCheckResult = typeChecker_.check(program);
    
    // Collect type checking errors
    for (const auto& err : typeChecker_.errors()) {
      result.errors.push_back("TypeError: " + err);
      lastErrors_.push_back(err);
    }
    for (const auto& warn : typeChecker_.warnings()) {
      result.warnings.push_back(warn);
      lastWarnings_.push_back(warn);
    }
  }

  // Step 3: Additional undefined variable diagnostics
  if (options_.treatUndefinedAsError) {
    // The lexical resolver already reports unresolved identifiers as errors
    // The type checker also reports undefined variable errors
    // These are already captured above
  }

  return result;
}

LexicalResolutionResult SemanticAnalyzer::resolveNames(const ast::Program& program) {
  lastErrors_.clear();
  auto result = lexicalResolver_.resolve(program);
  for (const auto& err : lexicalResolver_.errors()) {
    lastErrors_.push_back(err);
  }
  return result;
}

TypeCheckResult SemanticAnalyzer::checkTypes(const ast::Program& program,
                                             const LexicalResolutionResult&) {
  lastErrors_.clear();
  lastWarnings_.clear();
  auto result = typeChecker_.check(program);
  for (const auto& err : typeChecker_.errors()) {
    lastErrors_.push_back(err);
  }
  for (const auto& warn : typeChecker_.warnings()) {
    lastWarnings_.push_back(warn);
  }
  return result;
}

} // namespace havel::compiler