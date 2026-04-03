#pragma once

#include <future>
#include <queue>
#include <thread>
#include <shared_mutex>
#include "CompilationPipeline.hpp"
#include "../vm/VMExecutionContext.hpp"
#include "../tools/AdvancedUtils.hpp"
#include "../semantic/ModuleResolver.hpp"
#include "../runtime/RuntimeSupport.hpp"
#include <filesystem>
#include <memory>
#include <functional>
#include <stdexcept>

// Macro for throwing errors with source location info
#define COMPILER_THROW(msg) throw std::runtime_error(std::string(msg) + " [" + __FILE__ + ":" + std::to_string(__LINE__) + "]")

namespace havel::compiler {

// ============================================================================
// CompileOptions - Configuration for compilation
// ============================================================================
struct CompileOptions {
  // Pipeline options
  bool optimize = true;
  int optimizationLevel = 2;
  bool generateDebugInfo = true;
  bool strictMode = false;

  // Module options
  std::vector<std::filesystem::path> importPaths;
  bool useCache = true;
  std::filesystem::path cacheDir = ".havel_cache";

  // Output options
  enum class OutputFormat { Bytecode, Assembly, Object, Executable };
  OutputFormat outputFormat = OutputFormat::Bytecode;
  std::filesystem::path outputPath;

  // Execution options
  bool executeImmediately = false;
  std::vector<std::string> entryPointArgs;

  // Validation
  bool verifyBytecode = true;
  bool checkTypes = true;
};

// ============================================================================
// CompileResult - Result of compilation
// ============================================================================
struct CompileResult {
  bool success = false;
  std::unique_ptr<BytecodeChunk> chunk;

  // Metrics
  double compileTimeMs = 0.0;
  size_t sourceLines = 0;
  size_t bytecodeSize = 0;

  // Diagnostics
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  // Execution result (if executeImmediately was true)
  std::optional<Value> executionResult;

  // Output file (if outputPath was specified)
  std::optional<std::filesystem::path> outputFile;
};

// ============================================================================
// CompilerDriver - High-level compiler API
// ============================================================================
class CompilerDriver {
public:
  // Construction
  CompilerDriver();
  explicit CompilerDriver(const CompileOptions& options);
  ~CompilerDriver();

  // Configuration
  void setOptions(const CompileOptions& options);
  const CompileOptions& getOptions() const { return options_; }

  // Native function registration
  void registerNativeFunction(const std::string& name,
                               NativeFunctionBridge::NativeFunction func);
  template<typename ReturnType, typename... ArgTypes>
  void registerNativeFunction(const std::string& name,
                               ReturnType (*func)(ArgTypes...));

  // Compilation entry points
  CompileResult compileString(const std::string& source,
                               const std::string& filename = "<input>");
  CompileResult compileFile(const std::filesystem::path& path);
  CompileResult compileFiles(const std::vector<std::filesystem::path>& paths);

  // Execution
  Value execute(const BytecodeChunk& chunk,
                        const std::string& functionName = "main",
                        const std::vector<Value>& args = {});

  // REPL
  void startREPL();

  // Testing
  bool runTests(const std::filesystem::path& testDir);

  // Utilities
  std::string disassemble(const BytecodeChunk& chunk);
  bool verify(const BytecodeChunk& chunk);

  // Component access
  ModuleResolver& getModuleResolver() { return *moduleResolver_; }
  ModuleCache& getModuleCache() { return *moduleCache_; }
  NativeFunctionBridge& getNativeBridge() { return *nativeBridge_; }
  ErrorReporter& getErrorReporter() { return ErrorReporter::instance(); }

  // Statistics
  struct Stats {
    int compilations = 0;
    int executions = 0;
    double totalCompileTimeMs = 0.0;
    double totalExecTimeMs = 0.0;
    size_t cacheHits = 0;
    size_t cacheMisses = 0;
  };

  Stats getStats() const { return stats_; }
  void resetStats() { stats_ = Stats{}; }

  // Global instance
  static CompilerDriver& instance();

private:
  CompileOptions options_;
  Stats stats_;

  std::unique_ptr<CompilationPipeline> pipeline_;
  std::unique_ptr<VMExecutionContext> executionContext_;
  std::unique_ptr<ModuleResolver> moduleResolver_;
  std::unique_ptr<ModuleCache> moduleCache_;
  std::unique_ptr<NativeFunctionBridge> nativeBridge_;
  std::unique_ptr<ConfigManager> config_;

  void initialize();
  CompileResult executeCompilation(const std::string& source,
                                    const std::string& filename);
  bool writeOutput(const BytecodeChunk& chunk, const std::filesystem::path& path);
};

// ============================================================================
// BytecodeVerifier - Verify bytecode correctness
// ============================================================================
class BytecodeVerifier {
public:
  struct VerificationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
  };

  VerificationResult verify(const BytecodeChunk& chunk);
  VerificationResult verifyFunction(const BytecodeFunction& function,
                                       const BytecodeChunk& chunk);

  // Individual checks
  bool checkInstructionIntegrity(const BytecodeFunction& function);
  bool checkStackBalance(const BytecodeFunction& function);
  bool checkJumpTargets(const BytecodeFunction& function);
  bool checkLocalSlots(const BytecodeFunction& function);
  bool checkConstantIndices(const BytecodeFunction& function,
                            const BytecodeChunk& chunk);

private:
  void addError(VerificationResult& result, const std::string& message);
  void addWarning(VerificationResult& result, const std::string& message);
};

// ============================================================================
// SourceLocationMapper - Map between source and compiled locations
// ============================================================================
class SourceLocationMapper {
public:
  struct Mapping {
    SourceLocation source;
    uint32_t functionIndex;
    uint32_t instructionIndex;
  };

  void addMapping(const Mapping& mapping);

  // Lookup
  std::optional<SourceLocation> findSourceLocation(uint32_t functionIndex,
                                                    uint32_t instructionIndex) const;
  std::optional<std::pair<uint32_t, uint32_t>> findCompiledLocation(
      const SourceLocation& source) const;

  // Range queries
  std::vector<Mapping> findMappingsInRange(const SourceLocation& start,
                                              const SourceLocation& end) const;

  // Build from debug info
  void buildFromDebugInfo(const void* debugInfo);

  // Serialization
  std::string serialize() const;
  bool deserialize(const std::string& data);

private:
  std::vector<Mapping> mappings_;
  std::unordered_map<std::string, std::vector<size_t>> sourceIndex_;
};

// ============================================================================
// MemoryPool - Efficient memory allocation
// ============================================================================
template<typename T>
class MemoryPool {
public:
  explicit MemoryPool(size_t blockSize = 1024);
  ~MemoryPool();

  // Allocation
  T* allocate();
  void deallocate(T* ptr);

  // Bulk operations
  void reserve(size_t count);
  void clear();

  // Stats
  size_t getAllocatedCount() const { return allocated_; }
  size_t getAvailableCount() const { return available_; }
  size_t getBlockCount() const { return blocks_.size(); }

private:
  struct Block {
    std::unique_ptr<std::byte[]> data;
    size_t used = 0;
  };

  size_t blockSize_;
  std::vector<Block> blocks_;
  std::vector<T*> freeList_;
  size_t allocated_ = 0;
  size_t available_ = 0;

  T* allocateFromBlock();
  void grow();
};

// ============================================================================
// ThreadPool - Parallel compilation support
// ============================================================================
class ThreadPool {
public:
  explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
  ~ThreadPool();

  // Task submission
  template<typename Func, typename... Args>
  auto submit(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
    using ReturnType = decltype(func(args...));
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    std::future<ReturnType> result = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      if (stop_) {
        COMPILER_THROW("Cannot submit task: thread pool is stopped");
      }
      tasks_.emplace([task]() { (*task)(); });
      pending_++;
    }
    condition_.notify_one();
    return result;
  }

  // Control
  void pause();
  void resume();
  void stop();
  void waitForAll();

  // Stats
  size_t getPendingCount() const;
  size_t getActiveCount() const;
  size_t getCompletedCount() const;

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  mutable std::mutex queueMutex_;
  std::condition_variable condition_;
  std::atomic<bool> paused_{false};
  std::atomic<bool> stop_{false};
  std::atomic<size_t> pending_{0};
  std::atomic<size_t> active_{0};
  std::atomic<size_t> completed_{0};

  void workerLoop();
};

// ============================================================================
// ParallelCompiler - Compile multiple files in parallel
// ============================================================================
class ParallelCompiler {
public:
  explicit ParallelCompiler(ThreadPool& threadPool);

  // Compile multiple sources
  std::vector<CompileResult> compileBatch(
      const std::vector<std::pair<std::string, std::string>>& sources);

  // Compile directory
  std::vector<CompileResult> compileDirectory(const std::filesystem::path& dir);

private:
  ThreadPool& threadPool_;
};

// ============================================================================
// CompilerMetrics - Performance metrics collection
// ============================================================================
class CompilerMetrics {
public:
  struct PhaseMetrics {
    std::string phaseName;
    double timeMs = 0.0;
    size_t memoryBytes = 0;
  };

  struct CompileMetrics {
    std::string filename;
    double totalTimeMs = 0.0;
    std::vector<PhaseMetrics> phases;
    size_t sourceLines = 0;
    size_t outputBytes = 0;
  };

  void recordCompile(const CompileMetrics& metrics);
  void recordPhase(const std::string& filename, const PhaseMetrics& phase);

  // Reporting
  std::string generateReport() const;
  void printReport(std::ostream& output = std::cout) const;

  // Analysis
  double getAverageCompileTime() const;
  std::string getSlowestPhase() const;

  // Export
  bool exportJSON(const std::filesystem::path& path) const;
  bool exportCSV(const std::filesystem::path& path) const;

private:
  std::vector<CompileMetrics> compiles_;
  mutable std::mutex mutex_;
};

} // namespace havel::compiler
