#pragma once

#include "AdvancedUtils.hpp"
#include "BytecodeIR.hpp"
#include "CompilationPipeline.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace havel::compiler {

// ============================================================================
// SemanticAnalyzer - Deep semantic analysis of AST
// ============================================================================
class SemanticAnalyzer : public ASTVisitor<void> {
public:
  struct AnalysisResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::unordered_map<std::string, std::vector<std::string>> symbolReferences;
  };

  explicit SemanticAnalyzer(bool strictMode = false);

  AnalysisResult analyze(ast::Program& program);

  // Individual checks
  void checkTypes(ast::ASTNode& node);
  void checkScopes(ast::ASTNode& node);
  void checkControlFlow(ast::ASTNode& node);
  void checkVariableUsage(ast::ASTNode& node);

protected:
  void visitDefault(ast::ASTNode& node) override;
  void visitProgram(ast::Program& node) override;
  void visitFunctionDeclaration(ast::FunctionDeclaration& node) override;
  void visitLetDeclaration(ast::LetDeclaration& node) override;
  void visitIfStatement(ast::IfStatement& node) override;
  void visitWhileStatement(ast::WhileStatement& node) override;
  void visitForStatement(ast::ForStatement& node) override;
  void visitReturnStatement(ast::ReturnStatement& node) override;
  void visitIdentifier(ast::Identifier& node) override;
  void visitBinaryExpression(ast::BinaryExpression& node) override;
  void visitCallExpression(ast::CallExpression& node) override;

private:
  bool strictMode_;
  AnalysisResult result_;
  std::vector<std::unique_ptr<SymbolTable>> scopeStack_;
  bool inFunction_ = false;
  bool inLoop_ = false;
  std::vector<std::string> currentFunctionReturns_;

  void enterScope();
  void exitScope();
  SymbolTable& currentScope();

  void addError(const std::string& message, const ast::ASTNode& node);
  void addWarning(const std::string& message, const ast::ASTNode& node);

  bool isTypeCompatible(const std::string& type1, const std::string& type2);
  std::string inferType(ast::Expression& expr);
};

// ============================================================================
// ControlFlowGraph - CFG for flow analysis
// ============================================================================
class ControlFlowGraph {
public:
  struct Block {
    uint32_t id = 0;
    std::vector<size_t> instructions; // Indices into function instructions
    std::vector<uint32_t> predecessors;
    std::vector<uint32_t> successors;
    bool isEntry = false;
    bool isExit = false;
  };

  explicit ControlFlowGraph(const BytecodeFunction& function);

  // Build CFG from function
  void build();

  // Access
  const std::vector<Block>& getBlocks() const { return blocks_; }
  const Block& getEntryBlock() const { return blocks_[entryBlock_]; }
  std::vector<uint32_t> getExitBlocks() const;

  // Queries
  bool dominates(uint32_t dominator, uint32_t block) const;
  std::vector<uint32_t> getDominators(uint32_t block) const;
  std::vector<uint32_t> getDominated(uint32_t block) const;

  // Analysis
  std::vector<uint32_t> findUnreachableBlocks() const;
  bool isReachable(uint32_t block) const;

  // Visualization
  std::string toDOT() const; // Graphviz format

private:
  const BytecodeFunction& function_;
  std::vector<Block> blocks_;
  uint32_t entryBlock_ = 0;
  std::unordered_map<size_t, uint32_t> instructionToBlock_;

  void splitBlocks();
  void connectBlocks();
  void computeDominators();

  std::vector<std::vector<bool>> dominatorSets_;
};

// ============================================================================
// DataFlowAnalyzer - Data flow analysis framework
// ============================================================================
class DataFlowAnalyzer {
public:
  using VariableSet = std::unordered_set<std::string>;

  struct DataFlowResult {
    // For each block: variables live at entry/exit
    std::unordered_map<uint32_t, VariableSet> liveIn;
    std::unordered_map<uint32_t, VariableSet> liveOut;

    // For each block: reaching definitions
    std::unordered_map<uint32_t, VariableSet> reachingDefs;

    // For each block: available expressions
    std::unordered_map<uint32_t, std::vector<std::string>> availableExprs;
  };

  explicit DataFlowAnalyzer(const ControlFlowGraph& cfg);

  // Analyses
  DataFlowResult analyzeLiveness();
  DataFlowResult analyzeReachingDefinitions();
  DataFlowResult analyzeAvailableExpressions();

  // Use results
  bool isVariableLive(uint32_t block, const std::string& var) const;
  std::vector<std::string> getLiveVariables(uint32_t block) const;

private:
  const ControlFlowGraph& cfg_;
  DataFlowResult currentResult_;

  void computeLiveness();
  void computeReachingDefinitions();
  void computeAvailableExpressions();

  VariableSet unionSets(const std::vector<VariableSet>& sets);
  VariableSet intersectSets(const std::vector<VariableSet>& sets);
};

// ============================================================================
// OptimizationPass - Base class for optimization passes
// ============================================================================
class OptimizationPass {
public:
  struct Result {
    bool modified = false;
    std::string name;
    std::vector<std::string> changes;
    double timeMs = 0.0;
  };

  virtual ~OptimizationPass() = default;
  virtual Result run(BytecodeFunction& function) = 0;
  virtual std::string getName() const = 0;
  virtual bool isEnabled() const { return enabled_; }
  virtual void setEnabled(bool enabled) { enabled_ = enabled; }

protected:
  bool enabled_ = true;
};

// ============================================================================
// DeadCodeEliminator - Remove unreachable/dead code
// ============================================================================
class DeadCodeEliminator : public OptimizationPass {
public:
  std::string getName() const override { return "DeadCodeEliminator"; }
  Result run(BytecodeFunction& function) override;

private:
  void removeUnreachableBlocks(BytecodeFunction& function,
                                const std::vector<uint32_t>& unreachable);
  void removeUnusedStores(BytecodeFunction& function);
  void simplifyInstructions(BytecodeFunction& function);
};

// ============================================================================
// ConstantPropagator - Propagate constant values
// ============================================================================
class ConstantPropagator : public OptimizationPass {
public:
  std::string getName() const override { return "ConstantPropagator"; }
  Result run(BytecodeFunction& function) override;

private:
  struct ConstantValue {
    bool isConstant = false;
    BytecodeValue value;
  };

  std::unordered_map<uint32_t, ConstantValue> constants_; // slot -> value

  void propagateConstants(BytecodeFunction& function);
  void foldConstants(BytecodeFunction& function);

  std::optional<BytecodeValue> evaluateConstantExpression(
      OpCode op, const BytecodeValue& left, const BytecodeValue& right);
};

// ============================================================================
// InlineExpansion - Function inlining optimization
// ============================================================================
class InlineExpansion : public OptimizationPass {
public:
  struct Options {
    size_t maxFunctionSize;     // Don't inline functions larger than this
    size_t maxCallSites;         // Max inline occurrences per function
    int maxDepth;                // Max inline nesting depth
    bool preserveDebugInfo;
    Options() : maxFunctionSize(50), maxCallSites(5), maxDepth(3), preserveDebugInfo(true) {}
  };

  explicit InlineExpansion(const Options& options = Options{});

  std::string getName() const override { return "InlineExpansion"; }
  Result run(BytecodeFunction& function) override;

  // Register function bodies available for inlining
  void registerFunction(const BytecodeFunction& function);

private:
  Options options_;
  std::unordered_map<std::string, const BytecodeFunction*> inlineableFunctions_;

  bool shouldInline(const BytecodeFunction& callee, size_t callSiteCount) const;
  void inlineCall(BytecodeFunction& caller, size_t callIndex,
                  const BytecodeFunction& callee);
};

// ============================================================================
// LoopOptimizer - Loop optimizations
// ============================================================================
class LoopOptimizer : public OptimizationPass {
public:
  std::string getName() const override { return "LoopOptimizer"; }
  Result run(BytecodeFunction& function) override;

  // Individual optimizations
  void optimizeInvariantCodeMotion(BytecodeFunction& function);
  void optimizeStrengthReduction(BytecodeFunction& function);
  void optimizeInductionVariables(BytecodeFunction& function);
  void unrollLoops(BytecodeFunction& function, size_t unrollFactor = 4);

private:
  struct LoopInfo {
    uint32_t headerBlock;
    std::vector<uint32_t> bodyBlocks;
    std::vector<uint32_t> exitBlocks;
    bool isInnermost = true;
  };

  std::vector<LoopInfo> findLoops(const ControlFlowGraph& cfg);
  bool isLoopInvariant(const Instruction& instr, const LoopInfo& loop) const;
};

// ============================================================================
// RegisterAllocator - Simple register allocation
// ============================================================================
class RegisterAllocator : public OptimizationPass {
public:
  struct Allocation {
    uint32_t virtualRegister;
    uint32_t physicalRegister;
    bool spilled;
    uint32_t stackSlot;
  };

  explicit RegisterAllocator(size_t numPhysicalRegisters = 16);

  std::string getName() const override { return "RegisterAllocator"; }
  Result run(BytecodeFunction& function) override;

  // Get allocation results
  const std::unordered_map<uint32_t, Allocation>& getAllocations() const {
    return allocations_;
  }

private:
  size_t numPhysicalRegisters_;
  std::unordered_map<uint32_t, Allocation> allocations_;

  void computeLiveIntervals(const BytecodeFunction& function);
  void allocateRegisters();
  void insertSpillCode(BytecodeFunction& function);

  struct LiveInterval {
    uint32_t reg;
    size_t start;
    size_t end;
    bool active = false;
  };

  std::vector<LiveInterval> intervals_;
};

// ============================================================================
// TailCallOptimizer - Optimize tail calls
// ============================================================================
class TailCallOptimizer : public OptimizationPass {
public:
  std::string getName() const override { return "TailCallOptimizer"; }
  Result run(BytecodeFunction& function) override;

private:
  bool isTailCall(const BytecodeFunction& function, size_t instructionIndex) const;
  void convertToTailCall(BytecodeFunction& function, size_t callIndex);
};

// ============================================================================
// PeepholeOptimizer - Pattern-based local optimizations
// ============================================================================
class PeepholeOptimizer : public OptimizationPass {
public:
  struct Pattern {
    std::vector<OpCode> match;
    std::vector<Instruction> replace;
    std::function<bool(const std::vector<Instruction>&)> condition;
  };

  PeepholeOptimizer();

  std::string getName() const override { return "PeepholeOptimizer"; }
  Result run(BytecodeFunction& function) override;

  // Add custom pattern
  void addPattern(const Pattern& pattern);

private:
  std::vector<Pattern> patterns_;

  void addDefaultPatterns();
  bool tryMatch(const std::vector<Instruction>& instructions,
                size_t startIndex, const Pattern& pattern);
};

// ============================================================================
// GlobalOptimizer - Cross-function optimizations
// ============================================================================
class GlobalOptimizer {
public:
  struct Options {
    bool enableDeadCodeElimination;
    bool enableConstantPropagation;
    bool enableInlining;
    bool enableLoopOptimization;
    bool enableRegisterAllocation;
    bool enableTailCallOptimization;
    bool enablePeepholeOptimization;
    int maxPasses;
    Options() : enableDeadCodeElimination(true), enableConstantPropagation(true),
                enableInlining(true), enableLoopOptimization(true),
                enableRegisterAllocation(true), enableTailCallOptimization(true),
                enablePeepholeOptimization(true), maxPasses(3) {}
  };

  explicit GlobalOptimizer(const Options& options = Options{});

  // Optimize entire chunk
  void optimize(BytecodeChunk& chunk);

  // Optimize single function
  void optimizeFunction(BytecodeFunction& function);

  // Statistics
  struct Stats {
    int passes = 0;
    std::vector<std::string> appliedPasses;
    size_t instructionsRemoved = 0;
    size_t instructionsAdded = 0;
    double totalTimeMs = 0.0;
  };

  Stats getStats() const { return stats_; }

private:
  Options options_;
  Stats stats_;

  std::vector<std::unique_ptr<OptimizationPass>> passes_;

  void initializePasses();
  OptimizationPass::Result runPass(OptimizationPass& pass, BytecodeFunction& function);
};

} // namespace havel::compiler
