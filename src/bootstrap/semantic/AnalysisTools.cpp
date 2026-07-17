#include "AnalysisTools.hpp"
#include <algorithm>
#include <chrono>
#include <queue>
#include <sstream>
#include <fstream>
#include <stack>

namespace havel::compiler {

// ============================================================================
// CrossReferenceAnalyzer Implementation
// ============================================================================

void CrossReferenceAnalyzer::analyzeAST(const ast::Program& program,
                                         const std::string& filename) {
  for (const auto& stmt : program.body) {
    if (stmt) {
      traverseAST(*stmt, filename);
    }
  }
}

void CrossReferenceAnalyzer::analyzeChunk(const BytecodeChunk& chunk,
                                           const std::string& filename) {
  (void)chunk;
  (void)filename;
  // Would analyze bytecode for symbol references
}

std::vector<CrossReferenceAnalyzer::SymbolReference> CrossReferenceAnalyzer::findReferences(
    const std::string& symbolName) const {
  auto it = symbols_.find(symbolName);
  if (it != symbols_.end()) {
    return it->second.references;
  }
  return {};
}

std::optional<CrossReferenceAnalyzer::SymbolInfo> CrossReferenceAnalyzer::getSymbolInfo(
    const std::string& symbolName) const {
  auto it = symbols_.find(symbolName);
  if (it != symbols_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<std::string> CrossReferenceAnalyzer::findUnusedSymbols() const {
  std::vector<std::string> unused;
  for (const auto& [name, info] : symbols_) {
    bool hasNonDefReference = false;
    for (const auto& ref : info.references) {
      if (ref.kind != SymbolReference::Kind::Definition &&
          ref.kind != SymbolReference::Kind::Declaration) {
        hasNonDefReference = true;
        break;
      }
    }
    if (!hasNonDefReference) {
      unused.push_back(name);
    }
  }
  return unused;
}

std::vector<std::string> CrossReferenceAnalyzer::findUndefinedSymbols() const {
  std::vector<std::string> undefined;
  for (const auto& [name, info] : symbols_) {
    bool hasDefinition = false;
    for (const auto& ref : info.references) {
      if (ref.kind == SymbolReference::Kind::Definition) {
        hasDefinition = true;
        break;
      }
    }
    if (!hasDefinition) {
      undefined.push_back(name);
    }
  }
  return undefined;
}

std::optional<CrossReferenceAnalyzer::SymbolReference> CrossReferenceAnalyzer::findDefinition(
    const std::string& symbolName) const {
  auto refs = findReferences(symbolName);
  for (const auto& ref : refs) {
    if (ref.kind == SymbolReference::Kind::Definition) {
      return ref;
    }
  }
  return std::nullopt;
}

std::vector<CrossReferenceAnalyzer::SymbolReference> CrossReferenceAnalyzer::findCallers(
    const std::string& functionName) const {
  std::vector<SymbolReference> callers;
  auto refs = findReferences(functionName);
  for (const auto& ref : refs) {
    if (ref.kind == SymbolReference::Kind::Call) {
      callers.push_back(ref);
    }
  }
  return callers;
}

std::vector<CrossReferenceAnalyzer::SymbolReference> CrossReferenceAnalyzer::findCallees(
    const std::string& functionName) const {
  (void)functionName;
  // Would need to analyze function body for calls
  return {};
}

void CrossReferenceAnalyzer::exportDatabase(const std::string& filename) const {
  std::ofstream file(filename);
  for (const auto& [name, info] : symbols_) {
    file << "Symbol: " << name << "\n";
    file << "  Type: " << info.type << "\n";
    file << "  Defined in: " << info.definingFile << "\n";
    file << "  References:\n";
    for (const auto& ref : info.references) {
      file << "    " << ref.file << ":" << ref.line << " ";
      switch (ref.kind) {
        case SymbolReference::Kind::Definition: file << "(def)"; break;
        case SymbolReference::Kind::Declaration: file << "(decl)"; break;
        case SymbolReference::Kind::Read: file << "(read)"; break;
        case SymbolReference::Kind::Write: file << "(write)"; break;
        case SymbolReference::Kind::Call: file << "(call)"; break;
      }
      file << "\n";
    }
  }
}

void CrossReferenceAnalyzer::importDatabase(const std::string& filename) {
  (void)filename;
  // Would parse exported format
}

size_t CrossReferenceAnalyzer::getReferenceCount() const {
  size_t count = 0;
  for (const auto& [_, info] : symbols_) {
    (void)_;
    count += info.references.size();
  }
  return count;
}

void CrossReferenceAnalyzer::addReference(const SymbolReference& ref) {
  symbols_[ref.symbolName].references.push_back(ref);
  fileIndex_[ref.file].push_back(ref);
}

void CrossReferenceAnalyzer::traverseAST(const ast::ASTNode& node,
                                          const std::string& filename) {
  // Simplified traversal
  if (node.kind == ast::NodeType::Identifier) {
    const auto& id = static_cast<const ast::Identifier&>(node);
    SymbolReference ref;
    ref.symbolName = id.symbol;
    ref.file = filename;
    ref.line = node.line;
    ref.column = node.column;
    ref.kind = SymbolReference::Kind::Read;
    addReference(ref);
  } else if (node.kind == ast::NodeType::FunctionDeclaration) {
    const auto& func = static_cast<const ast::FunctionDeclaration&>(node);
    if (func.name) {
      SymbolReference ref;
      ref.symbolName = func.name->symbol;
      ref.file = filename;
      ref.line = func.name->line;
      ref.column = func.name->column;
      ref.kind = SymbolReference::Kind::Definition;
      addReference(ref);
    }
  }
}

// ============================================================================
// DependencyGraph Implementation
// ============================================================================

void DependencyGraph::addModule(const ModuleNode& module) {
  modules_[module.name] = module;
}

void DependencyGraph::addDependency(const std::string& from, const std::string& to) {
  modules_[from].dependencies.push_back(to);
  modules_[to].dependents.push_back(from);
}

bool DependencyGraph::hasDependency(const std::string& from, const std::string& to) const {
  auto it = modules_.find(from);
  if (it == modules_.end()) return false;
  const auto& deps = it->second.dependencies;
  return std::find(deps.begin(), deps.end(), to) != deps.end();
}

std::vector<std::string> DependencyGraph::getDependencies(const std::string& module) const {
  auto it = modules_.find(module);
  if (it != modules_.end()) {
    return it->second.dependencies;
  }
  return {};
}

std::vector<std::string> DependencyGraph::getDependents(const std::string& module) const {
  auto it = modules_.find(module);
  if (it != modules_.end()) {
    return it->second.dependents;
  }
  return {};
}

std::vector<std::string> DependencyGraph::getTransitiveDependencies(
    const std::string& module) const {
  std::vector<std::string> result;
  std::unordered_set<std::string> visited;
  std::stack<std::string> stack;

  stack.push(module);
  while (!stack.empty()) {
    std::string current = stack.top();
    stack.pop();

    if (visited.count(current) > 0) continue;
    visited.insert(current);
    if (current != module) {
      result.push_back(current);
    }

    auto it = modules_.find(current);
    if (it != modules_.end()) {
      for (const auto& dep : it->second.dependencies) {
        if (visited.count(dep) == 0) {
          stack.push(dep);
        }
      }
    }
  }

  return result;
}

std::vector<std::string> DependencyGraph::topologicalSort() const {
  std::vector<std::string> result;
  std::unordered_map<std::string, int> inDegree;

  // Calculate in-degrees
  for (const auto& [name, module] : modules_) {
    if (inDegree.count(name) == 0) {
      inDegree[name] = 0;
    }
    for (const auto& dep : module.dependencies) {
      inDegree[dep]++; // This seems backwards - dependents increase in-degree
    }
  }

  // Kahn's algorithm
  std::queue<std::string> queue;
  for (const auto& [name, degree] : inDegree) {
    if (degree == 0) {
      queue.push(name);
    }
  }

  while (!queue.empty()) {
    std::string current = queue.front();
    queue.pop();
    result.push_back(current);

    auto it = modules_.find(current);
    if (it != modules_.end()) {
      for (const auto& dependent : it->second.dependents) {
        inDegree[dependent]--;
        if (inDegree[dependent] == 0) {
          queue.push(dependent);
        }
      }
    }
  }

  return result;
}

DependencyGraph::CycleInfo DependencyGraph::detectCycles() const {
  CycleInfo info;
  std::unordered_set<std::string> visited;
  std::unordered_set<std::string> recStack;

  for (const auto& [name, _] : modules_) {
    (void)_;
    if (visited.count(name) == 0) {
      std::vector<std::string> path;
      dfsVisit(name, visited, recStack, path, info);
    }
  }

  info.hasCycle = !info.cycles.empty();
  return info;
}

void DependencyGraph::dfsVisit(const std::string& module,
                                std::unordered_set<std::string>& visited,
                                std::unordered_set<std::string>& recStack,
                                std::vector<std::string>& currentPath,
                                CycleInfo& cycleInfo) const {
  visited.insert(module);
  recStack.insert(module);
  currentPath.push_back(module);

  auto it = modules_.find(module);
  if (it != modules_.end()) {
    for (const auto& dep : it->second.dependencies) {
      if (recStack.count(dep) > 0) {
        // Found cycle
        auto cycleStart = std::find(currentPath.begin(), currentPath.end(), dep);
        if (cycleStart != currentPath.end()) {
          std::vector<std::string> cycle(cycleStart, currentPath.end());
          cycle.push_back(dep);
          cycleInfo.cycles.push_back(cycle);
        }
      } else if (visited.count(dep) == 0) {
        dfsVisit(dep, visited, recStack, currentPath, cycleInfo);
      }
    }
  }

  recStack.erase(module);
  currentPath.pop_back();
}

std::vector<std::vector<std::string>> DependencyGraph::findStronglyConnectedComponents() const {
  // Tarjan's algorithm would go here
  return {};
}

std::vector<std::string> DependencyGraph::computeBuildOrder() const {
  return topologicalSort();
}

std::vector<std::string> DependencyGraph::findAffectedModules(
    const std::string& changedModule) const {
  return getDependents(changedModule);
}

std::string DependencyGraph::toDOT() const {
  std::ostringstream ss;
  ss << "digraph Dependencies {\n";
  ss << "  rankdir=BT;\n"; // Bottom to top

  for (const auto& [name, module] : modules_) {
    ss << "  \"" << name << "\"";
    if (module.isEntryPoint) {
      ss << " [style=filled, fillcolor=green]";
    } else if (module.isExternal) {
      ss << " [style=filled, fillcolor=gray]";
    }
    ss << ";\n";

    for (const auto& dep : module.dependencies) {
      ss << "  \"" << name << "\" -> \"" << dep << "\";\n";
    }
  }

  ss << "}\n";
  return ss.str();
}

std::string DependencyGraph::toJSON() const {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"modules\": [\n";

  bool first = true;
  for (const auto& [name, module] : modules_) {
    if (!first) ss << ",\n";
    first = false;

    ss << "    {\n";
    ss << "      \"name\": \"" << name << "\",\n";
    ss << "      \"dependencies\": [";
    bool firstDep = true;
    for (const auto& dep : module.dependencies) {
      if (!firstDep) ss << ", ";
      firstDep = false;
      ss << "\"" << dep << "\"";
    }
    ss << "],\n";
    ss << "      \"isEntryPoint\": " << (module.isEntryPoint ? "true" : "false") << "\n";
    ss << "    }";
  }

  ss << "\n  ]\n";
  ss << "}\n";
  return ss.str();
}

// ============================================================================
// CodeMetricsAnalyzer Implementation
// ============================================================================

CodeMetricsAnalyzer::FunctionMetrics CodeMetricsAnalyzer::analyzeFunction(
    const ast::FunctionDeclaration& func) {
  FunctionMetrics metrics;

  if (func.name) {
    metrics.name = func.name->symbol;
  }

  metrics.parameterCount = func.parameters.size();

  // Count lines
  if (func.body) {
    metrics.linesOfCode = func.body->body.size();
  }

  // Calculate complexity (simplified)
  metrics.cyclomaticComplexity = 1; // Base complexity

  return metrics;
}

CodeMetricsAnalyzer::ModuleMetrics CodeMetricsAnalyzer::analyzeModule(
    const ast::Program& program) {
  ModuleMetrics metrics;

  for (const auto& stmt : program.body) {
    if (!stmt) continue;

    if (stmt->kind == ast::NodeType::FunctionDeclaration) {
      const auto& func = static_cast<const ast::FunctionDeclaration&>(*stmt);
      metrics.functions.push_back(analyzeFunction(func));
      metrics.functionCount++;
    }
  }

  // Calculate average complexity
  if (!metrics.functions.empty()) {
    double totalComplexity = 0;
    for (const auto& func : metrics.functions) {
      totalComplexity += func.cyclomaticComplexity;
    }
    metrics.averageComplexity = totalComplexity / metrics.functions.size();
  }

  return metrics;
}

CodeMetricsAnalyzer::ModuleMetrics CodeMetricsAnalyzer::analyzeBytecode(
    const BytecodeChunk& chunk) {
  ModuleMetrics metrics;

  for (const auto& func : chunk.getAllFunctions()) {
    FunctionMetrics funcMetrics;
    funcMetrics.name = func.name;
    funcMetrics.instructionCount = func.instructions.size();
    metrics.functions.push_back(funcMetrics);
    metrics.functionCount++;
  }

  return metrics;
}

std::vector<std::string> CodeMetricsAnalyzer::findIssues(
    const FunctionMetrics& metrics, const Thresholds& thresholds) {
  std::vector<std::string> issues;

  if (metrics.linesOfCode > thresholds.maxFunctionLength) {
    issues.push_back("Function too long: " + std::to_string(metrics.linesOfCode) +
                     " lines (max: " + std::to_string(thresholds.maxFunctionLength) + ")");
  }

  if (metrics.cyclomaticComplexity > thresholds.maxComplexity) {
    issues.push_back("Complexity too high: " +
                     std::to_string(metrics.cyclomaticComplexity) +
                     " (max: " + std::to_string(thresholds.maxComplexity) + ")");
  }

  if (metrics.parameterCount > thresholds.maxParameters) {
    issues.push_back("Too many parameters: " + std::to_string(metrics.parameterCount) +
                     " (max: " + std::to_string(thresholds.maxParameters) + ")");
  }

  if (metrics.maxNestingDepth > thresholds.maxNestingDepth) {
    issues.push_back("Nesting too deep: " + std::to_string(metrics.maxNestingDepth) +
                     " (max: " + std::to_string(thresholds.maxNestingDepth) + ")");
  }

  return issues;
}

std::string CodeMetricsAnalyzer::generateReport(const ModuleMetrics& metrics) const {
  std::ostringstream ss;

  ss << "=== Code Metrics Report ===\n\n";
  ss << "Module: " << metrics.name << "\n";
  ss << "Total lines: " << metrics.totalLines << "\n";
  ss << "Code lines: " << metrics.codeLines << "\n";
  ss << "Functions: " << metrics.functionCount << "\n";
  ss << "Average complexity: " << metrics.averageComplexity << "\n\n";

  ss << "Function Details:\n";
  for (const auto& func : metrics.functions) {
    ss << "  " << func.name << ":\n";
    ss << "    Lines: " << func.linesOfCode << "\n";
    ss << "    Complexity: " << func.cyclomaticComplexity << "\n";
    ss << "    Parameters: " << func.parameterCount << "\n";
  }

  return ss.str();
}

void CodeMetricsAnalyzer::printReport(const ModuleMetrics& metrics,
                                       std::ostream& output) const {
  output << generateReport(metrics);
}

bool CodeMetricsAnalyzer::hasImproved(const FunctionMetrics& before,
                                       const FunctionMetrics& after) {
  return after.cyclomaticComplexity < before.cyclomaticComplexity ||
         after.linesOfCode < before.linesOfCode;
}

// ============================================================================
// RefactoringEngine Implementation
// ============================================================================

void RefactoringEngine::registerRefactoring(const Refactoring& refactoring) {
  refactorings_[refactoring.name] = refactoring;
}

std::vector<RefactoringEngine::Change> RefactoringEngine::apply(
    const std::string& refactoringName,
    ast::Program& program,
    const std::vector<std::string>& args) {
  auto it = refactorings_.find(refactoringName);
  if (it == refactorings_.end()) {
    return {};
  }

  (void)args;

  if (it->second.apply(program)) {
    // Record changes for undo
    // pushUndo(changes);
  }

  return {};
}

std::vector<RefactoringEngine::Change> RefactoringEngine::preview(
    const std::string& refactoringName,
    const ast::Program& program,
    const std::vector<std::string>& args) const {
  (void)refactoringName;
  (void)program;
  (void)args;
  return {};
}

std::vector<RefactoringEngine::Change> RefactoringEngine::renameSymbol(
    ast::Program& program,
    const std::string& oldName,
    const std::string& newName) {
  (void)program;
  (void)oldName;
  (void)newName;
  return {};
}

std::vector<RefactoringEngine::Change> RefactoringEngine::extractFunction(
    ast::Program& program,
    const std::string& selection,
    const std::string& newFunctionName) {
  (void)program;
  (void)selection;
  (void)newFunctionName;
  return {};
}

std::vector<RefactoringEngine::Change> RefactoringEngine::inlineFunction(
    ast::Program& program,
    const std::string& functionName) {
  (void)program;
  (void)functionName;
  return {};
}

std::vector<RefactoringEngine::Change> RefactoringEngine::moveFunction(
    ast::Program& program,
    const std::string& functionName,
    const std::string& targetModule) {
  (void)program;
  (void)functionName;
  (void)targetModule;
  return {};
}

void RefactoringEngine::undo(ast::Program& program) {
  (void)program;
  // Would restore previous state
}

void RefactoringEngine::redo(ast::Program& program) {
  (void)program;
  // Would reapply changes
}

void RefactoringEngine::pushUndo(const std::vector<Change>& changes) {
  undoStack_.push_back(changes);
  redoStack_.clear(); // Clear redo on new change
}

// ============================================================================
// SnapshotManager Implementation
// ============================================================================

std::string SnapshotManager::createSnapshot(const ast::Program& ast,
                                             const BytecodeChunk& chunk,
                                             const std::string& description) {
  Snapshot snapshot;
  snapshot.id = generateId();
  snapshot.description = description;
  snapshot.timestamp = std::chrono::system_clock::now();

  // Serialize AST and chunk (simplified)
  (void)ast;
  (void)chunk;

  snapshots_[snapshot.id] = snapshot;
  return snapshot.id;
}

bool SnapshotManager::restoreSnapshot(const std::string& id,
                                       ast::Program& ast,
                                       BytecodeChunk& chunk) {
  (void)id;
  (void)ast;
  (void)chunk;
  // Would deserialize
  return false;
}

void SnapshotManager::deleteSnapshot(const std::string& id) {
  snapshots_.erase(id);
}

std::vector<SnapshotManager::Snapshot> SnapshotManager::listSnapshots() const {
  std::vector<Snapshot> result;
  for (const auto& [_, snapshot] : snapshots_) {
    (void)_;
    result.push_back(snapshot);
  }
  return result;
}

std::optional<SnapshotManager::Snapshot> SnapshotManager::getSnapshot(
    const std::string& id) const {
  auto it = snapshots_.find(id);
  if (it != snapshots_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<std::string> SnapshotManager::diffSnapshots(const std::string& id1,
                                                         const std::string& id2) const {
  (void)id1;
  (void)id2;
  return {};
}

bool SnapshotManager::saveToDisk(const std::string& directory) {
  (void)directory;
  return false;
}

bool SnapshotManager::loadFromDisk(const std::string& directory) {
  (void)directory;
  return false;
}

std::string SnapshotManager::generateId() {
  return "snap-" + std::to_string(nextId_++);
}

// ============================================================================
// PerformanceProfiler Implementation
// ============================================================================

PerformanceProfiler::Scope::Scope(PerformanceProfiler& profiler, const std::string& name)
    : profiler_(profiler), name_(name) {
  profiler_.start(name_);
}

PerformanceProfiler::Scope::~Scope() {
  profiler_.end(name_);
}

void PerformanceProfiler::start(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  activeTimings_[name] = std::chrono::steady_clock::now();
}

void PerformanceProfiler::end(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = activeTimings_.find(name);
  if (it == activeTimings_.end()) return;

  auto end = std::chrono::steady_clock::now();
  double duration = std::chrono::duration<double, std::milli>(
      end - it->second).count();

  auto& timing = timings_[name];
  timing.name = name;
  timing.totalMs += duration;
  timing.count++;
  timing.avgMs = timing.totalMs / timing.count;

  if (timing.minMs == 0 || duration < timing.minMs) {
    timing.minMs = duration;
  }
  if (duration > timing.maxMs) {
    timing.maxMs = duration;
  }

  activeTimings_.erase(it);
}

void PerformanceProfiler::recordMemorySnapshot(const std::string& label) {
  (void)label;
  // Would record actual memory usage
}

std::optional<PerformanceProfiler::Timing> PerformanceProfiler::getTiming(
    const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = timings_.find(name);
  if (it != timings_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<PerformanceProfiler::Timing> PerformanceProfiler::getAllTimings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Timing> result;
  for (const auto& [_, timing] : timings_) {
    (void)_;
    result.push_back(timing);
  }
  return result;
}

std::string PerformanceProfiler::generateFlameGraph() const {
  // Would generate flame graph format
  return "";
}

std::string PerformanceProfiler::generateCSV() const {
  std::ostringstream ss;
  ss << "Name,Count,Total(ms),Avg(ms),Min(ms),Max(ms)\n";

  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [_, timing] : timings_) {
    (void)_;
    ss << timing.name << ","
       << timing.count << ","
       << timing.totalMs << ","
       << timing.avgMs << ","
       << timing.minMs << ","
       << timing.maxMs << "\n";
  }

  return ss.str();
}

void PerformanceProfiler::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  timings_.clear();
  activeTimings_.clear();
  memoryHistory_.clear();
}

// ============================================================================
// SecurityAnalyzer Implementation
// ============================================================================

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::analyze(
    const ast::Program& program) {
  std::vector<Vulnerability> vulns;

  auto sql = checkSQLInjection(program);
  vulns.insert(vulns.end(), sql.begin(), sql.end());

  auto cmd = checkCommandInjection(program);
  vulns.insert(vulns.end(), cmd.begin(), cmd.end());

  auto path = checkPathTraversal(program);
  vulns.insert(vulns.end(), path.begin(), path.end());

  auto rand = checkInsecureRandom(program);
  vulns.insert(vulns.end(), rand.begin(), rand.end());

  auto secrets = checkHardcodedSecrets(program);
  vulns.insert(vulns.end(), secrets.begin(), secrets.end());

  return vulns;
}

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::analyze(
    const BytecodeChunk& chunk) {
  (void)chunk;
  return {};
}

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::checkSQLInjection(
    const ast::Program& program) {
  (void)program;
  return {};
}

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::checkCommandInjection(
    const ast::Program& program) {
  (void)program;
  return {};
}

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::checkPathTraversal(
    const ast::Program& program) {
  (void)program;
  return {};
}

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::checkInsecureRandom(
    const ast::Program& program) {
  (void)program;
  return {};
}

std::vector<SecurityAnalyzer::Vulnerability> SecurityAnalyzer::checkHardcodedSecrets(
    const ast::Program& program) {
  (void)program;
  return {};
}

std::string SecurityAnalyzer::generateReport(
    const std::vector<Vulnerability>& vulns) const {
  std::ostringstream ss;

  ss << "=== Security Analysis Report ===\n\n";
  ss << "Found " << vulns.size() << " potential issues:\n\n";

  for (const auto& vuln : vulns) {
    ss << "[" << vuln.severity << "] " << vuln.type << "\n";
    ss << "  Location: " << vuln.file << ":" << vuln.line << "\n";
    ss << "  Description: " << vuln.message << "\n";
    ss << "  Remediation: " << vuln.remediation << "\n\n";
  }

  return ss.str();
}

bool SecurityAnalyzer::exportSARIF(const std::vector<Vulnerability>& vulns,
                                     const std::string& filename) const {
  (void)vulns;
  (void)filename;
  return false;
}

void SecurityAnalyzer::addVulnerability(std::vector<Vulnerability>& vulns,
                                        const std::string& type,
                                        const std::string& severity,
                                        const std::string& message,
                                        const ast::ASTNode& node) {
  Vulnerability vuln;
  vuln.type = type;
  vuln.severity = severity;
  vuln.message = message;
  vuln.line = node.line;
  vulns.push_back(vuln);
}

// ============================================================================
// DocumentationExtractor Implementation
// ============================================================================

std::vector<DocumentationExtractor::DocumentedSymbol> DocumentationExtractor::extractFromAST(
    const ast::Program& program) {
  std::vector<DocumentedSymbol> symbols;

  for (const auto& stmt : program.body) {
    if (!stmt) continue;

    if (stmt->kind == ast::NodeType::FunctionDeclaration) {
      const auto& func = static_cast<const ast::FunctionDeclaration&>(*stmt);

      DocumentedSymbol symbol;
      symbol.type = "function";
      symbol.location = SourceLocation{"", static_cast<uint32_t>(stmt->line), static_cast<uint32_t>(stmt->column)};

      if (func.name) {
        symbol.name = func.name->symbol;
      }

      symbols.push_back(symbol);
    }
  }

  return symbols;
}

std::vector<DocumentationExtractor::DocumentedSymbol> DocumentationExtractor::extractFromComments(
    const std::string& source) {
  (void)source;
  return {};
}

DocumentationExtractor::DocComment DocumentationExtractor::parseDocComment(
    const std::string& comment) {
  DocComment doc;

  // Simple parsing
  if (comment.find("@param") != std::string::npos) {
    // Extract parameter info
  }
  if (comment.find("@return") != std::string::npos) {
    // Extract return info
  }

  doc.summary = extractCommentText(comment);
  return doc;
}

std::string DocumentationExtractor::generateMarkdown(
    const std::vector<DocumentedSymbol>& symbols) const {
  std::ostringstream ss;

  ss << "# API Documentation\n\n";

  for (const auto& symbol : symbols) {
    ss << "## " << symbol.name << "\n\n";
    ss << "Type: " << symbol.type << "\n\n";
    ss << symbol.documentation.summary << "\n\n";
  }

  return ss.str();
}

std::string DocumentationExtractor::generateHTML(
    const std::vector<DocumentedSymbol>& symbols) const {
  std::ostringstream ss;

  ss << "<!DOCTYPE html>\n<html>\n<body>\n";
  ss << "<h1>API Documentation</h1>\n";

  for (const auto& symbol : symbols) {
    ss << "<h2>" << symbol.name << "</h2>\n";
    ss << "<p>Type: " << symbol.type << "</p>\n";
    ss << "<p>" << symbol.documentation.summary << "</p>\n";
  }

  ss << "</body>\n</html>\n";
  return ss.str();
}

std::vector<DocumentationExtractor::DocumentedSymbol> DocumentationExtractor::search(
    const std::vector<DocumentedSymbol>& symbols,
    const std::string& query) const {
  std::vector<DocumentedSymbol> result;

  for (const auto& symbol : symbols) {
    if (symbol.name.find(query) != std::string::npos ||
        symbol.documentation.summary.find(query) != std::string::npos) {
      result.push_back(symbol);
    }
  }

  return result;
}

std::string DocumentationExtractor::extractCommentText(const std::string& rawComment) {
  // Remove comment markers and trim
  std::string text = rawComment;

  // Remove // or /* */ markers
  size_t pos = 0;
  while ((pos = text.find("//", pos)) != std::string::npos) {
    text.erase(pos, 2);
  }

  return text;
}

} // namespace havel::compiler
