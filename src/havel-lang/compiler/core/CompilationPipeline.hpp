#pragma once

#include "../semantic/BindingResolver.hpp"
#include "CodeEmitter.hpp"
#include "ExpressionCompiler.hpp"
#include "../semantic/ModuleResolver.hpp"
#include "BytecodeIR.hpp"
#include "../gc/GCManager.hpp"
#include <memory>
#include <vector>
#include <string>
#include <filesystem>

namespace havel::compiler {

// Forward declarations
class BytecodeChunk;
class Lexer;
class Parser;

// ============================================================================
// CompilationPipeline - Orchestrates the entire compilation process
// ============================================================================
class CompilationPipeline {
public:
  struct Options {
    bool enableOptimizations = true;
    bool enableDebugInfo = true;
    bool strictMode = false;
    size_t maxOptimizationPasses = 3;
    std::vector<std::filesystem::path> modulePaths;
  };

  struct Result {
    std::unique_ptr<BytecodeChunk> chunk;
    bool success = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    double compilationTimeMs = 0.0;
  };

  struct Stage {
    std::string name;
    double timeMs = 0.0;
    bool success = false;
  };

  explicit CompilationPipeline(const Options& options);
  ~CompilationPipeline();

  // Main compilation entry point
  Result compile(const std::string& source, const std::string& filename = "<input>");
  Result compileFile(const std::filesystem::path& path);

  // Stage-by-stage compilation (for debugging/profiling)
  Result compileWithStages(const std::string& source, const std::string& filename);
  const std::vector<Stage>& getLastStages() const { return lastStages_; }

  // Configuration
  void setOptions(const Options& options) { options_ = options; }
  const Options& getOptions() const { return options_; }

  // Component access (for advanced usage)
  BindingResolver& getBindingResolver() { return *bindingResolver_; }
  CodeEmitter& getCodeEmitter() { return *codeEmitter_; }
  ExpressionCompiler& getExpressionCompiler() { return *exprCompiler_; }
  StatementCompiler& getStatementCompiler() { return *stmtCompiler_; }
  ModuleResolver& getModuleResolver() { return *moduleResolver_; }

private:
  Options options_;
  std::vector<Stage> lastStages_;

  // Pipeline components
  std::unique_ptr<BindingResolver> bindingResolver_;
  std::unique_ptr<CodeEmitter> codeEmitter_;
  std::unique_ptr<ExpressionCompiler> exprCompiler_;
  std::unique_ptr<StatementCompiler> stmtCompiler_;
  std::unique_ptr<FunctionCompiler> funcCompiler_;
  std::unique_ptr<PatternCompiler> patternCompiler_;
  std::unique_ptr<ModuleResolver> moduleResolver_;
  std::unique_ptr<BytecodeChunk> currentChunk_;

  // Compilation stages
  bool lexingStage(const std::string& source, const std::string& filename);
  bool parsingStage();
  bool resolutionStage();
  bool compilationStage();
  bool optimizationStage();

  // AST storage between stages
  std::unique_ptr<ast::Program> ast_;
  LexicalResolutionResult lexicalResult_;
};

// ============================================================================
// BytecodeOptimizer - Optimization passes for bytecode
// ============================================================================
class BytecodeOptimizer {
public:
  struct Stats {
    size_t instructionsRemoved = 0;
    size_t instructionsModified = 0;
    size_t constantsFolded = 0;
    size_t deadCodeEliminated = 0;
    size_t jumpsOptimized = 0;
  };

  explicit BytecodeOptimizer(BytecodeChunk& chunk);

  // Run all optimization passes
  Stats optimize(size_t maxPasses = 3);

  // Individual optimization passes
  void constantFolding();
  void deadCodeElimination();
  void jumpOptimization();
  void peepholeOptimization();
  void registerAllocation();

  // Stats
  const Stats& getStats() const { return stats_; }
  void resetStats() { stats_ = Stats{}; }

private:
  BytecodeChunk& chunk_;
  Stats stats_;

  // Helper methods
  bool isConstant(const Value& value) const;
  std::optional<Value> evaluateBinaryOp(OpCode op,
                                                const Value& left,
                                                const Value& right) const;
  void removeInstruction(size_t functionIndex, size_t instructionIndex);
  void replaceInstruction(size_t functionIndex,
                          size_t instructionIndex,
                          const Instruction& newInstruction) {
    if (functionIndex >= chunk_.getFunctionCount()) return;
    auto* function = const_cast<BytecodeFunction*>(chunk_.getFunction(functionIndex));
    if (!function) return;
  }

};

// ============================================================================
// ASTValidator - Semantic validation of AST
// ============================================================================
class ASTValidator {
public:
  struct ValidationError {
    std::string message;
    uint32_t line = 0;
    uint32_t column = 0;
    std::string file;
  };

  explicit ASTValidator(bool strictMode = false);

  // Validation methods
  bool validate(const ast::Program& program);
  bool validateExpression(const ast::Expression& expr);
  bool validateStatement(const ast::Statement& stmt);

  // Error access
  const std::vector<ValidationError>& getErrors() const { return errors_; }
  bool hasErrors() const { return !errors_.empty(); }
  void clearErrors() { errors_.clear(); }

  // Validation options
  void setStrictMode(bool strict) { strictMode_ = strict; }
  bool isStrictMode() const { return strictMode_; }

private:
  bool strictMode_;
  std::vector<ValidationError> errors_;

  // Validation helpers
  void addError(const std::string& message, const ast::ASTNode& node);
  bool checkIdentifier(const ast::Identifier& id);
  bool checkAssignment(const ast::AssignmentExpression& assignment);
  bool checkCall(const ast::CallExpression& call);
  bool checkReturn(const ast::ReturnStatement& ret);
  bool checkBreakContinue(const ast::Statement& stmt);
  bool checkDuplicateDeclaration(const std::string& name, const ast::ASTNode& node);

  // Symbol tracking during validation
  std::vector<std::unordered_set<std::string>> scopeStack_;
  bool inLoop_ = false;
  bool inFunction_ = false;
};

// ============================================================================
// SymbolTable - Efficient hierarchical symbol lookup
// ============================================================================
class SymbolTable : public std::enable_shared_from_this<SymbolTable> {
public:
  struct Symbol {
    std::string name;
    enum class Kind { Variable, Function, Parameter, Type, Unknown } kind;
    uint32_t slot = 0;
    bool isConst = false;
    bool isCaptured = false;
    uint32_t scopeDepth = 0;
    const ast::ASTNode* declaration = nullptr;
  };

  SymbolTable();
  explicit SymbolTable(std::shared_ptr<SymbolTable> parent);

  // Scope management
  std::shared_ptr<SymbolTable> enterScope();
  std::shared_ptr<SymbolTable> exitScope();
  std::shared_ptr<SymbolTable> getParent() const { return parent_; }
  uint32_t getDepth() const { return depth_; }

  // Symbol management
  bool declare(const std::string& name, Symbol::Kind kind,
               const ast::ASTNode* declaration = nullptr);
  std::optional<Symbol> lookup(const std::string& name) const;
  std::optional<Symbol> lookupLocal(const std::string& name) const;
  bool hasSymbol(const std::string& name) const;

  // Symbol modification
  bool markAsCaptured(const std::string& name);
  bool markAsConst(const std::string& name);

  // Bulk operations
  std::vector<Symbol> getAllSymbols() const;
  std::vector<Symbol> getLocalSymbols() const;
  size_t getSymbolCount() const { return symbols_.size(); }

  // Slot management
  uint32_t allocateSlot();
  uint32_t getNextSlot() const { return nextSlot_; }

private:
  std::shared_ptr<SymbolTable> parent_;
  std::unordered_map<std::string, Symbol> symbols_;
  uint32_t depth_ = 0;
  uint32_t nextSlot_ = 0;
};

// ============================================================================
// ParserUtils - Shared utilities for parsing
// ============================================================================
class ParserUtils {
public:
  // Token classification
  static bool isLiteral(const Token& token);
  static bool isOperator(const Token& token);
  static bool isKeyword(const Token& token);
  static bool isExpressionStart(const Token& token);
  static bool isStatementStart(const Token& token);

  // Precedence handling
  static int getPrecedence(TokenType type);
  static bool isRightAssociative(TokenType type);

  // Error recovery
  static Token synchronize(std::vector<Token>::const_iterator& current,
                           const std::vector<Token>& tokens,
                           TokenType expected);

  // AST construction helpers
  static std::unique_ptr<ast::Identifier> createIdentifier(const Token& token);
  static std::unique_ptr<ast::BinaryExpression> createBinaryOp(
      std::unique_ptr<ast::Expression> left,
      TokenType op,
      std::unique_ptr<ast::Expression> right);

  // Pattern matching helpers
  static bool isPatternStart(const Token& token);
  static bool isValidPattern(const ast::Expression& expr);
};

} // namespace havel::compiler
