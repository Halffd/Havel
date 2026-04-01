#pragma once

#include "CompilerAPI.hpp"
#include "TestFramework.hpp"
#include <filesystem>
#include <vector>
#include <functional>

namespace havel::compiler {

// ============================================================================
// IntegrationTests - Comprehensive tests for compiler components
// ============================================================================
class IntegrationTests {
public:
  struct TestSuite {
    std::string name;
    std::vector<std::function<bool()>> tests;
    std::function<void()> setup;
    std::function<void()> teardown;
  };

  struct TestReport {
    int totalTests = 0;
    int passedTests = 0;
    int failedTests = 0;
    double totalTimeMs = 0.0;
    std::vector<std::pair<std::string, bool>> results;
    std::vector<std::string> errors;
  };

  static IntegrationTests& instance();

  // Register test suites
  void registerSuite(const TestSuite& suite);

  // Run tests
  TestReport runAll();
  TestReport runSuite(const std::string& name);
  bool runTest(const std::string& suiteName, const std::string& testName);

  // Built-in test suites
  void registerClosureTests();
  void registerCompilerTests();
  void registerVMTests();
  void registerOptimizationTests();
  void registerAnalysisTests();
  void registerPipelineTests();

  // Generate report
  void printReport(const TestReport& report, std::ostream& output = std::cout);
  std::string generateHTMLReport(const TestReport& report);

private:
  IntegrationTests();
  std::unordered_map<std::string, TestSuite> suites_;

  void registerAllBuiltInTests();
};

// ============================================================================
// BenchmarkingFramework - Performance benchmarking
// ============================================================================
class BenchmarkingFramework {
public:
  struct Benchmark {
    std::string name;
    std::function<void()> setup;
    std::function<void()> teardown;
    std::function<void()> work;
    int iterations = 100;
    int warmupIterations = 10;
  };

  struct BenchmarkResult {
    std::string name;
    double minTimeMs = 0.0;
    double maxTimeMs = 0.0;
    double avgTimeMs = 0.0;
    double medianTimeMs = 0.0;
    double stdDevMs = 0.0;
    double throughput = 0.0; // ops/sec
    std::vector<double> allTimes;
  };

  struct ComparisonResult {
    std::string baselineName;
    std::string candidateName;
    double speedup = 0.0; // 1.0 = same, >1 = faster, <1 = slower
    bool significant = false;
  };

  static BenchmarkingFramework& instance();

  // Register and run
  void registerBenchmark(const Benchmark& benchmark);
  BenchmarkResult run(const std::string& name);
  std::vector<BenchmarkResult> runAll();

  // Comparisons
  ComparisonResult compare(const std::string& baseline,
                            const std::string& candidate);

  // Built-in benchmarks
  void registerCompilerBenchmarks();
  void registerVMBenchmarks();
  void registerGCBenchmarks();
  void registerOptimizationBenchmarks();

  // Reporting
  void printResults(const std::vector<BenchmarkResult>& results,
                     std::ostream& output = std::cout);
  bool exportJSON(const std::vector<BenchmarkResult>& results,
                   const std::string& filename);
  bool exportCSV(const std::vector<BenchmarkResult>& results,
                  const std::string& filename);

  // Baseline management
  void saveBaseline(const std::string& filename);
  bool loadBaseline(const std::string& filename);
  std::vector<ComparisonResult> compareToBaseline(
      const std::vector<BenchmarkResult>& results);

private:
  BenchmarkingFramework();
  std::unordered_map<std::string, Benchmark> benchmarks_;
  std::unordered_map<std::string, BenchmarkResult> baselineResults_;

  void registerAllBuiltInBenchmarks();
  double measureIteration(const Benchmark& benchmark);
};

// ============================================================================
// MigrationBridge - Bridge for gradual migration from old to new code
// ============================================================================
class MigrationBridge {
public:
  // Adapter patterns for using new classes with old interfaces
  class LegacyVMAdapter {
  public:
    explicit LegacyVMAdapter(VMExecutionContext& newVM);

    // Old VM interface methods
    void execute(const BytecodeChunk& chunk);
    void pushValue(const BytecodeValue& value);
    BytecodeValue popValue();

  private:
    VMExecutionContext& newVM_;
  };

  class LegacyCompilerAdapter {
  public:
    explicit LegacyCompilerAdapter(CompilationPipeline& newPipeline);

    // Old compiler interface
    bool compile(const std::string& source, BytecodeChunk& output);
    std::vector<std::string> getErrors() const;

  private:
    CompilationPipeline& pipeline_;
    std::vector<std::string> errors_;
  };

  // Feature detection
  static bool isNewCompilerAvailable();
  static bool isNewVMAvailable();

  // Gradual migration helpers
  static void enableNewCompiler(bool enable);
  static void enableNewVM(bool enable);
  static bool isNewCompilerEnabled();
  static bool isNewVMEnabled();

  // Compatibility layer
  static BytecodeChunk convertOldToNewFormat(const void* oldChunk);
  static void* convertNewToOldFormat(const BytecodeChunk& newChunk);

  // Performance comparison
  struct Comparison {
    double oldTimeMs = 0.0;
    double newTimeMs = 0.0;
    double speedup = 0.0;
    bool resultsMatch = false;
  };

  static Comparison compareCompilation(const std::string& source);
  static Comparison compareExecution(const BytecodeChunk& chunk);

private:
  static bool newCompilerEnabled_;
  static bool newVMEnabled_;
};

// ============================================================================
// PluginSystem - Extensible plugin architecture
// ============================================================================
class PluginSystem {
public:
  struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::vector<std::string> dependencies;
  };

  class Plugin {
  public:
    virtual ~Plugin() = default;
    virtual PluginInfo getInfo() const = 0;
    virtual bool initialize(PluginSystem& system) = 0;
    virtual void shutdown() = 0;
  };

  using PluginFactory = std::function<std::unique_ptr<Plugin>()>;

  static PluginSystem& instance();

  // Plugin management
  bool registerPlugin(const std::string& name, PluginFactory factory);
  bool loadPlugin(const std::filesystem::path& path);
  bool unloadPlugin(const std::string& name);

  // Access
  std::vector<std::string> listPlugins() const;
  Plugin* getPlugin(const std::string& name);
  template<typename T>
  T* getPluginAs(const std::string& name);

  // Hooks for extension points
  using Hook = std::function<void()>;
  void registerHook(const std::string& extensionPoint, Hook hook);
  void executeHooks(const std::string& extensionPoint);

  // Compiler extensions
  void registerNativeFunction(const std::string& name,
                               BytecodeHostFunction func);

  // Event system
  using EventHandler = std::function<void(const std::string& event, const void* data)>;
  void subscribe(const std::string& event, EventHandler handler);
  void publish(const std::string& event, const void* data);

private:
  PluginSystem() = default;
  std::unordered_map<std::string, PluginFactory> pluginFactories_;
  std::unordered_map<std::string, std::unique_ptr<Plugin>> loadedPlugins_;
  std::unordered_map<std::string, std::vector<Hook>> hooks_;
  std::unordered_map<std::string, std::vector<EventHandler>> eventHandlers_;
};

// ============================================================================
// HotReloadManager - Hot code reloading for development
// ============================================================================
class HotReloadManager {
public:
  struct WatchConfig {
    std::filesystem::path path;
    bool recursive = true;
    std::vector<std::string> extensions = {".havel"};
    std::chrono::milliseconds debounceMs = std::chrono::milliseconds(100);
  };

  struct ReloadResult {
    bool success = false;
    std::string error;
    std::vector<std::string> changedFiles;
    std::chrono::system_clock::time_point timestamp;
  };

  using ReloadCallback = std::function<void(const ReloadResult& result)>;

  explicit HotReloadManager(CompilerDriver& driver);

  // Watch configuration
  void addWatch(const WatchConfig& config);
  void removeWatch(const std::filesystem::path& path);
  void clearWatches();

  // Control
  void start();
  void stop();
  bool isRunning() const { return running_; }

  // Callbacks
  void onReload(ReloadCallback callback);
  void onCompileError(std::function<void(const std::string& error)> callback);

  // Manual reload
  ReloadResult reload(const std::vector<std::filesystem::path>& files);

  // State preservation
  void enableStatePreservation(bool enable);
  bool isStatePreservationEnabled() const { return preserveState_; }

private:
  CompilerDriver& driver_;
  bool running_ = false;
  bool preserveState_ = true;
  std::vector<WatchConfig> watches_;
  std::vector<ReloadCallback> reloadCallbacks_;
  std::vector<std::function<void(const std::string&)>> errorCallbacks_;

  void watchLoop();
  std::vector<std::filesystem::path> scanForChanges();
  bool hasFileChanged(const std::filesystem::path& path,
                       std::filesystem::file_time_type& lastMtime);
  std::unordered_map<std::string, std::filesystem::file_time_type> fileMtimes_;
};

// ============================================================================
// SerializationFormats - Additional serialization formats
// ============================================================================
class SerializationFormats {
public:
  // JSON format
  static std::string toJSON(const BytecodeChunk& chunk);
  static std::optional<BytecodeChunk> fromJSON(const std::string& json);

  // MessagePack format (binary JSON)
  static std::vector<uint8_t> toMessagePack(const BytecodeChunk& chunk);
  static std::optional<BytecodeChunk> fromMessagePack(const std::vector<uint8_t>& data);

  // Protocol Buffers format (stub)
  static std::vector<uint8_t> toProtobuf(const BytecodeChunk& chunk);
  static std::optional<BytecodeChunk> fromProtobuf(const std::vector<uint8_t>& data);

  // XML format
  static std::string toXML(const BytecodeChunk& chunk);
  static std::optional<BytecodeChunk> fromXML(const std::string& xml);

  // YAML format
  static std::string toYAML(const BytecodeChunk& chunk);
  static std::optional<BytecodeChunk> fromYAML(const std::string& yaml);

  // Verification
  static bool verify(const BytecodeChunk& chunk);
  static bool verify(const std::string& data, const std::string& format);
};

// ============================================================================
// WASMTarget - WebAssembly compilation target (stub)
// ============================================================================
class WASMTarget {
public:
  struct Options {
    bool optimize;
    bool enableSIMD;
    bool enableThreads;
    bool enableBulkMemory;
    bool enableMutableGlobals;
    Options() : optimize(true), enableSIMD(false), enableThreads(false),
                enableBulkMemory(true), enableMutableGlobals(true) {}
  };

  explicit WASMTarget(const Options& options = Options{});

  // Compilation
  std::vector<uint8_t> compile(const BytecodeChunk& chunk);
  std::vector<uint8_t> compileFunction(const BytecodeFunction& function);

  // Validation
  bool validate(const std::vector<uint8_t>& wasm);

  // Statistics
  struct Stats {
    size_t functionsGenerated = 0;
    size_t codeSize = 0;
    size_t dataSize = 0;
    double compileTimeMs = 0.0;
  };

  Stats getStats() const { return stats_; }

private:
  Options options_;
  Stats stats_;
};

// ============================================================================
// LLVMTarget - LLVM IR compilation target (stub for JIT/AOT)
// ============================================================================
class LLVMTarget {
public:
  struct Options {
    enum class OptimizationLevel { O0, O1, O2, O3, Os, Oz };
    OptimizationLevel optLevel;
    bool enableLTO;
    std::string targetTriple;
    std::string cpu;
    std::string features;
    Options() : optLevel(OptimizationLevel::O2), enableLTO(false) {}
  };

  explicit LLVMTarget(const Options& options = Options{});
  ~LLVMTarget();

  // IR generation
  std::string generateIR(const BytecodeChunk& chunk);
  std::string generateIR(const BytecodeFunction& function);

  // Compilation
  bool compileToObjectFile(const std::string& ir,
                            const std::filesystem::path& output);
  bool compileToSharedLibrary(const std::string& ir,
                               const std::filesystem::path& output);

  // JIT execution
  void* compileForJIT(const std::string& ir);
  void* getFunctionPointer(void* module, const std::string& name);

  // Optimization
  std::string optimizeIR(const std::string& ir);

private:
  Options options_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ============================================================================
// CodeCoverage - Code coverage analysis
// ============================================================================
class CodeCoverage {
public:
  struct CoverageInfo {
    std::string file;
    uint32_t line;
    uint32_t column;
    uint64_t executionCount = 0;
    bool isCovered() const { return executionCount > 0; }
  };

  struct FileCoverage {
    std::string filename;
    std::vector<CoverageInfo> lines;
    size_t totalLines = 0;
    size_t coveredLines = 0;
    double coveragePercent() const {
      return totalLines > 0 ? (100.0 * coveredLines / totalLines) : 0.0;
    }
  };

  // Instrumentation
  void instrument(BytecodeChunk& chunk);

  // Execution tracking
  void recordExecution(const std::string& file, uint32_t line);
  void reset();

  // Reporting
  FileCoverage getCoverage(const std::string& filename) const;
  std::vector<FileCoverage> getAllCoverage() const;
  double getTotalCoverage() const;

  // Export
  bool exportLCOV(const std::filesystem::path& filename) const;
  bool exportHTML(const std::filesystem::path& directory) const;
  std::string generateReport() const;

private:
  std::unordered_map<std::string, std::vector<CoverageInfo>> coverage_;
  mutable std::mutex mutex_;
};

// ============================================================================
// FuzzingHarness - Fuzz testing support
// ============================================================================
class FuzzingHarness {
public:
  struct FuzzOptions {
    size_t maxIterations = 10000;
    size_t maxInputSize = 1024;
    double maxExecutionTimeSec = 60.0;
    bool detectCrashes = true;
    bool detectHangs = true;
    bool detectMemoryIssues = true;
  };

  // Fuzz targets
  using FuzzTarget = std::function<bool(const std::vector<uint8_t>& input)>;

  void registerTarget(const std::string& name, FuzzTarget target);

  // Run fuzzing
  struct FuzzResult {
    bool passed = false;
    size_t iterations = 0;
    size_t crashes = 0;
    size_t hangs = 0;
    std::vector<std::vector<uint8_t>> crashInputs;
    double timeSec = 0.0;
    std::string error;
  };

  FuzzResult run(const std::string& targetName, const FuzzOptions& options);

  // Built-in targets
  void registerLexerFuzzTarget();
  void registerParserFuzzTarget();
  void registerCompilerFuzzTarget();
  void registerVMFuzzTarget();

  // Corpus management
  void loadCorpus(const std::filesystem::path& directory);
  void saveCorpus(const std::filesystem::path& directory);

private:
  std::unordered_map<std::string, FuzzTarget> targets_;
  std::vector<std::vector<uint8_t>> corpus_;
};

// ============================================================================
// DocumentationGenerator - Generate comprehensive documentation
// ============================================================================
class DocumentationGenerator {
public:
  struct Options {
    std::string title = "Havel Language Documentation";
    std::string version = "1.0.0";
    bool includeExamples = true;
    bool includeSource = false;
    std::filesystem::path outputDir = "docs";
    std::vector<std::string> inputFiles;
  };

  explicit DocumentationGenerator(const Options& options);

  // Generate documentation
  bool generate();

  // Individual generators
  bool generateAPIReference();
  bool generateUserGuide();
  bool generateInternalDocs();
  bool generateChangelog();

  // Search index
  bool generateSearchIndex();

private:
  Options options_;

  bool writeMarkdownFile(const std::filesystem::path& path,
                         const std::string& content);
  bool writeHTMLFile(const std::filesystem::path& path,
                     const std::string& content);
  std::string generateNavigation();
  std::string generateFooter();
};

} // namespace havel::compiler
