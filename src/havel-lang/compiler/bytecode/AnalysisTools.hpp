#pragma once

#include "AST.h"
#include "BytecodeIR.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace havel::compiler {

// ============================================================================
// CrossReferenceAnalyzer - Analyze symbol references across codebase
// ============================================================================
class CrossReferenceAnalyzer {
public:
  struct SymbolReference {
    std::string symbolName;
    std::string file;
    uint32_t line;
    uint32_t column;
    enum class Kind { Definition, Declaration, Read, Write, Call } kind;
  };

  struct SymbolInfo {
    std::string name;
    std::string type;
    std::string definingFile;
    std::vector<SymbolReference> references;
  };

  // Build cross-reference database
  void analyzeAST(const ast::Program& program, const std::string& filename);
  void analyzeChunk(const BytecodeChunk& chunk, const std::string& filename);

  // Queries
  std::vector<SymbolReference> findReferences(const std::string& symbolName) const;
  std::optional<SymbolInfo> getSymbolInfo(const std::string& symbolName) const;
  std::vector<std::string> findUnusedSymbols() const;
  std::vector<std::string> findUndefinedSymbols() const;

  // Navigation
  std::optional<SymbolReference> findDefinition(const std::string& symbolName) const;
  std::vector<SymbolReference> findCallers(const std::string& functionName) const;
  std::vector<SymbolReference> findCallees(const std::string& functionName) const;

  // Export/Import
  void exportDatabase(const std::string& filename) const;
  void importDatabase(const std::string& filename);

  // Statistics
  size_t getSymbolCount() const { return symbols_.size(); }
  size_t getReferenceCount() const;

private:
  std::unordered_map<std::string, SymbolInfo> symbols_;
  std::unordered_map<std::string, std::vector<SymbolReference>> fileIndex_;

  void addReference(const SymbolReference& ref);
  void traverseAST(const ast::ASTNode& node, const std::string& filename);
};

// ============================================================================
// DependencyGraph - Module dependency analysis
// ============================================================================
class DependencyGraph {
public:
  struct ModuleNode {
    std::string name;
    std::filesystem::path path;
    std::vector<std::string> dependencies;
    std::vector<std::string> dependents;
    bool isEntryPoint = false;
    bool isExternal = false;
  };

  struct CycleInfo {
    bool hasCycle = false;
    std::vector<std::vector<std::string>> cycles;
  };

  // Build graph
  void addModule(const ModuleNode& module);
  void addDependency(const std::string& from, const std::string& to);

  // Queries
  bool hasDependency(const std::string& from, const std::string& to) const;
  std::vector<std::string> getDependencies(const std::string& module) const;
  std::vector<std::string> getDependents(const std::string& module) const;
  std::vector<std::string> getTransitiveDependencies(const std::string& module) const;

  // Topological operations
  std::vector<std::string> topologicalSort() const;
  CycleInfo detectCycles() const;
  std::vector<std::vector<std::string>> findStronglyConnectedComponents() const;

  // Build order
  std::vector<std::string> computeBuildOrder() const;

  // Impact analysis
  std::vector<std::string> findAffectedModules(const std::string& changedModule) const;

  // Visualization
  std::string toDOT() const;
  std::string toJSON() const;

private:
  std::unordered_map<std::string, ModuleNode> modules_;

  void dfsVisit(const std::string& module,
                std::unordered_set<std::string>& visited,
                std::unordered_set<std::string>& recStack,
                std::vector<std::string>& currentPath,
                CycleInfo& cycleInfo) const;
};

// ============================================================================
// CodeMetricsAnalyzer - Code quality and complexity metrics
// ============================================================================
class CodeMetricsAnalyzer {
public:
  struct FunctionMetrics {
    std::string name;
    uint32_t linesOfCode = 0;
    uint32_t cyclomaticComplexity = 0;
    uint32_t cognitiveComplexity = 0;
    uint32_t parameterCount = 0;
    uint32_t localVariableCount = 0;
    uint32_t maxNestingDepth = 0;
    uint32_t instructionCount = 0;
    double maintainabilityIndex = 0.0;
  };

  struct ModuleMetrics {
    std::string name;
    uint32_t totalLines = 0;
    uint32_t codeLines = 0;
    uint32_t commentLines = 0;
    uint32 blankLines = 0;
    uint32_t functionCount = 0;
    uint32_t classCount = 0;
    double averageComplexity = 0.0;
    std::vector<FunctionMetrics> functions;
  };

  // Analyze
  FunctionMetrics analyzeFunction(const ast::FunctionDeclaration& func);
  ModuleMetrics analyzeModule(const ast::Program& program);
  ModuleMetrics analyzeBytecode(const BytecodeChunk& chunk);

  // Thresholds
  struct Thresholds {
    uint32_t maxFunctionLength = 50;
    uint32_t maxComplexity = 10;
    uint32_t maxParameters = 5;
    uint32_t maxNestingDepth = 4;
  };

  std::vector<std::string> findIssues(const FunctionMetrics& metrics,
                                       const Thresholds& thresholds = Thresholds{});

  // Reporting
  std::string generateReport(const ModuleMetrics& metrics) const;
  void printReport(const ModuleMetrics& metrics, std::ostream& output = std::cout) const;

  // Comparison
  bool hasImproved(const FunctionMetrics& before, const FunctionMetrics& after);
};

// ============================================================================
// RefactoringEngine - Automated code transformations
// ============================================================================
class RefactoringEngine {
public:
  struct Refactoring {
    std::string name;
    std::string description;
    std::function<bool(ast::Program&)> apply;
    bool safe = true; // Can be undone
  };

  struct Change {
    std::string file;
    uint32_t startLine;
    uint32_t endLine;
    std::string originalText;
    std::string newText;
    std::string description;
  };

  // Register refactorings
  void registerRefactoring(const Refactoring& refactoring);

  // Apply refactoring
  std::vector<Change> apply(const std::string& refactoringName,
                            ast::Program& program,
                            const std::vector<std::string>& args = {});

  // Preview changes without applying
  std::vector<Change> preview(const std::string& refactoringName,
                               const ast::Program& program,
                               const std::vector<std::string>& args = {}) const;

  // Common refactorings
  std::vector<Change> renameSymbol(ast::Program& program,
                                     const std::string& oldName,
                                     const std::string& newName);
  std::vector<Change> extractFunction(ast::Program& program,
                                       const std::string& selection,
                                       const std::string& newFunctionName);
  std::vector<Change> inlineFunction(ast::Program& program,
                                      const std::string& functionName);
  std::vector<Change> moveFunction(ast::Program& program,
                                    const std::string& functionName,
                                    const std::string& targetModule);

  // Undo/Redo
  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }
  void undo(ast::Program& program);
  void redo(ast::Program& program);

private:
  std::unordered_map<std::string, Refactoring> refactorings_;
  std::vector<std::vector<Change>> undoStack_;
  std::vector<std::vector<Change>> redoStack_;

  void pushUndo(const std::vector<Change>& changes);
};

// ============================================================================
// SnapshotManager - Capture and restore compiler state
// ============================================================================
class SnapshotManager {
public:
  struct Snapshot {
    std::string id;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    std::vector<uint8_t> serializedAST;
    std::vector<uint8_t> serializedBytecode;
    std::unordered_map<std::string, std::string> metadata;
  };

  // Create snapshot
  std::string createSnapshot(const ast::Program& ast,
                              const BytecodeChunk& chunk,
                              const std::string& description = "");

  // Restore snapshot
  bool restoreSnapshot(const std::string& id,
                        ast::Program& ast,
                        BytecodeChunk& chunk);

  // Manage snapshots
  void deleteSnapshot(const std::string& id);
  std::vector<Snapshot> listSnapshots() const;
  std::optional<Snapshot> getSnapshot(const std::string& id) const;

  // Comparison
  std::vector<std::string> diffSnapshots(const std::string& id1,
                                          const std::string& id2) const;

  // Persistence
  bool saveToDisk(const std::string& directory);
  bool loadFromDisk(const std::string& directory);

private:
  std::unordered_map<std::string, Snapshot> snapshots_;
  uint64_t nextId_ = 1;

  std::string generateId();
};

// ============================================================================
// PerformanceProfiler - Fine-grained performance analysis
// ============================================================================
class PerformanceProfiler {
public:
  struct Timing {
    std::string name;
    double totalMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double avgMs = 0.0;
    uint64_t count = 0;
  };

  struct MemorySnapshot {
    size_t heapUsed = 0;
    size_t heapTotal = 0;
    size_t stackUsed = 0;
    size_t objectsAllocated = 0;
    size_t objectsFreed = 0;
  };

  // Scoped profiling
  class Scope {
  public:
    Scope(PerformanceProfiler& profiler, const std::string& name);
    ~Scope();
  private:
    PerformanceProfiler& profiler_;
    std::string name_;
    std::chrono::steady_clock::time_point start_;
  };

  // Manual timing
  void start(const std::string& name);
  void end(const std::string& name);

  // Memory tracking
  void recordMemorySnapshot(const std::string& label);

  // Query results
  std::optional<Timing> getTiming(const std::string& name) const;
  std::vector<Timing> getAllTimings() const;
  std::vector<MemorySnapshot> getMemoryHistory() const;

  // Reports
  std::string generateFlameGraph() const;
  std::string generateCSV() const;

  // Reset
  void clear();

private:
  std::unordered_map<std::string, Timing> timings_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> activeTimings_;
  std::vector<std::pair<std::string, MemorySnapshot>> memoryHistory_;
  mutable std::mutex mutex_;
};

// ============================================================================
// SecurityAnalyzer - Security vulnerability detection
// ============================================================================
class SecurityAnalyzer {
public:
  struct Vulnerability {
    std::string type;
    std::string severity; // critical, high, medium, low
    std::string message;
    std::string file;
    uint32_t line;
    std::string remediation;
  };

  // Analysis
  std::vector<Vulnerability> analyze(const ast::Program& program);
  std::vector<Vulnerability> analyze(const BytecodeChunk& chunk);

  // Checks
  std::vector<Vulnerability> checkSQLInjection(const ast::Program& program);
  std::vector<Vulnerability> checkCommandInjection(const ast::Program& program);
  std::vector<Vulnerability> checkPathTraversal(const ast::Program& program);
  std::vector<Vulnerability> checkInsecureRandom(const ast::Program& program);
  std::vector<Vulnerability> checkHardcodedSecrets(const ast::Program& program);

  // Reporting
  std::string generateReport(const std::vector<Vulnerability>& vulns) const;
  bool exportSARIF(const std::vector<Vulnerability>& vulns,
                   const std::string& filename) const;

private:
  void addVulnerability(std::vector<Vulnerability>& vulns,
                        const std::string& type,
                        const std::string& severity,
                        const std::string& message,
                        const ast::ASTNode& node);
};

// ============================================================================
// DocumentationExtractor - Extract documentation from code
// ============================================================================
class DocumentationExtractor {
public:
  struct DocComment {
    std::string summary;
    std::string description;
    std::vector<std::string> parameters;
    std::string returns;
    std::vector<std::string> examples;
    std::vector<std::string> tags;
  };

  struct DocumentedSymbol {
    std::string name;
    std::string type; // function, class, variable
    DocComment documentation;
    SourceLocation location;
  };

  // Extract
  std::vector<DocumentedSymbol> extractFromAST(const ast::Program& program);
  std::vector<DocumentedSymbol> extractFromComments(const std::string& source);

  // Parse doc comments
  DocComment parseDocComment(const std::string& comment);

  // Generate output
  std::string generateMarkdown(const std::vector<DocumentedSymbol>& symbols) const;
  std::string generateHTML(const std::vector<DocumentedSymbol>& symbols) const;

  // Search
  std::vector<DocumentedSymbol> search(const std::vector<DocumentedSymbol>& symbols,
                                        const std::string& query) const;

private:
  std::string extractCommentText(const std::string& rawComment);
};

} // namespace havel::compiler
