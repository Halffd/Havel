// ============================================================================
// Example: Using the Refactored Havel Compiler API
// ============================================================================

#include "compiler/bytecode/CompilerAPI.hpp"
#include "compiler/bytecode/CompilationPipeline.hpp"
#include "compiler/bytecode/VMExecutionContext.hpp"
#include <iostream>

using namespace havel::compiler;

// Example 1: Simple Compilation
void example_simpleCompilation() {
  // Create compiler driver with default options
  CompilerDriver driver;
  
  // Compile a simple Havel expression
  std::string source = R"(
    fn add(a, b) {
      return a + b
    }
    
    let result = add(40, 2)
    print(result)
  )";
  
  auto result = driver.compileString(source, "example.havel");
  
  if (result.success) {
    std::cout << "Compilation successful!\n";
    std::cout << "Compiled in " << result.compileTimeMs << " ms\n";
    std::cout << "Source lines: " << result.sourceLines << "\n";
    
    // Execute the compiled code
    auto execResult = driver.execute(*result.chunk, "main", {});
    std::cout << "Execution result: " << RuntimeTypeSystem::stringify(execResult) << "\n";
  } else {
    std::cout << "Compilation failed:\n";
    for (const auto& error : result.errors) {
      std::cout << "  Error: " << error << "\n";
    }
  }
}

// Example 2: Advanced Configuration
void example_advancedConfiguration() {
  CompileOptions options;
  options.optimize = true;
  options.optimizationLevel = 3;
  options.generateDebugInfo = true;
  options.strictMode = true;
  options.importPaths = {"/usr/local/lib/havel", "./lib"};
  options.useCache = true;
  options.cacheDir = ".havel_cache";
  
  CompilerDriver driver(options);
  
  // Register native functions
  driver.registerNativeFunction("nativeAdd", [](int a, int b) {
    return a + b;
  });
  
  // Compile with options
  auto result = driver.compileFile("myproject.havel");
  
  if (result.success && result.outputFile) {
    std::cout << "Output written to: " << result.outputFile->string() << "\n";
  }
}

// Example 3: Using the REPL
void example_repl() {
  REPL::Options replOptions;
  replOptions.prompt = "havel> ";
  replOptions.showBytecode = false;
  replOptions.enableOptimizations = true;
  
  REPL repl(replOptions);
  
  // Run interactively (would normally read from stdin)
  // repl.run();
  
  // Or evaluate individual expressions
  auto result = repl.evaluate("let x = 42");
  if (result.success) {
    std::cout << "Result: " << RuntimeTypeSystem::stringify(result.value) << "\n";
  }
}

// Example 4: Code Analysis
void example_codeAnalysis() {
  // Create a semantic analyzer
  SemanticAnalyzer analyzer(true); // strict mode
  
  // Parse some code
  std::string source = R"(
    fn fibonacci(n) {
      if n <= 1 {
        return n
      }
      return fibonacci(n - 1) + fibonacci(n - 2)
    }
  )";
  
  // Would parse to AST and analyze
  // auto result = analyzer.analyze(ast);
  
  // Check for issues
  // for (const auto& error : result.errors) {
  //   std::cout << "Error: " << error << "\n";
  // }
}

// Example 5: Bytecode Optimization
void example_optimization() {
  // Create optimizer with custom options
  GlobalOptimizer::Options optOptions;
  optOptions.enableDeadCodeElimination = true;
  optOptions.enableConstantPropagation = true;
  optOptions.enableInlining = true;
  optOptions.enableLoopOptimization = true;
  optOptions.maxPasses = 5;
  
  GlobalOptimizer optimizer(optOptions);
  
  // Compile some code
  CompilerDriver driver;
  auto result = driver.compileString("let x = 2 + 2");
  
  if (result.success && result.chunk) {
    // Optimize the bytecode
    optimizer.optimize(*result.chunk);
    
    // Check statistics
    auto stats = optimizer.getStats();
    std::cout << "Optimization passes: " << stats.passes << "\n";
    std::cout << "Instructions removed: " << stats.instructionsRemoved << "\n";
  }
}

// Example 6: Cross-Reference Analysis
void example_crossReference() {
  CrossReferenceAnalyzer xra;
  
  // Analyze multiple files
  CompilerDriver driver;
  
  std::vector<std::string> files = {"main.havel", "utils.havel", "lib.havel"};
  for (const auto& file : files) {
    auto result = driver.compileFile(file);
    if (result.success && result.chunk) {
      // xra.analyzeChunk(*result.chunk, file);
    }
  }
  
  // Find unused symbols
  auto unused = xra.findUnusedSymbols();
  std::cout << "Unused symbols:\n";
  for (const auto& sym : unused) {
    std::cout << "  " << sym << "\n";
  }
  
  // Find references to a symbol
  auto refs = xra.findReferences("myFunction");
  std::cout << "References to myFunction: " << refs.size() << "\n";
}

// Example 7: Module Dependencies
void example_dependencies() {
  DependencyGraph graph;
  
  // Add modules
  DependencyGraph::ModuleNode main;
  main.name = "main";
  main.isEntryPoint = true;
  graph.addModule(main);
  
  DependencyGraph::ModuleNode utils;
  utils.name = "utils";
  graph.addModule(utils);
  
  DependencyGraph::ModuleNode lib;
  lib.name = "lib";
  graph.addModule(lib);
  
  // Add dependencies
  graph.addDependency("main", "utils");
  graph.addDependency("main", "lib");
  graph.addDependency("utils", "lib");
  
  // Check for cycles
  auto cycleInfo = graph.detectCycles();
  if (cycleInfo.hasCycle) {
    std::cout << "Dependency cycles detected!\n";
  }
  
  // Get build order
  auto buildOrder = graph.computeBuildOrder();
  std::cout << "Build order:\n";
  for (const auto& mod : buildOrder) {
    std::cout << "  " << mod << "\n";
  }
  
  // Export as DOT for visualization
  std::cout << graph.toDOT() << "\n";
}

// Example 8: Profiling and Metrics
void example_profiling() {
  CompilerMetrics metrics;
  
  // Compile with metrics collection
  PerformanceProfiler profiler;
  
  {
    PerformanceProfiler::Scope scope(profiler, "compilation");
    
    CompilerDriver driver;
    auto result = driver.compileFile("large_project.havel");
    
    if (result.success) {
      CompilerMetrics::CompileMetrics compileMetrics;
      compileMetrics.filename = "large_project.havel";
      compileMetrics.totalTimeMs = result.compileTimeMs;
      compileMetrics.sourceLines = result.sourceLines;
      
      metrics.recordCompile(compileMetrics);
    }
  }
  
  // Get timing
  auto timing = profiler.getTiming("compilation");
  if (timing) {
    std::cout << "Compilation time: " << timing->totalMs << " ms\n";
  }
  
  // Generate report
  metrics.printReport();
}

// Example 9: Testing Framework
void example_testing() {
  // Define test cases
  TestFramework::TestCase test1;
  test1.name = "addition";
  test1.description = "Test basic addition";
  test1.test = []() {
    // Compile and execute
    CompilerDriver driver;
    auto result = driver.compileString("let x = 2 + 2");
    
    if (!result.success) return false;
    
    auto execResult = driver.execute(*result.chunk, "main", {});
    return RuntimeTypeSystem::equals(execResult, BytecodeValue(static_cast<int64_t>(4)));
  };
  
  // Register and run
  TestFramework::instance().registerTest("arithmetic", test1);
  auto results = TestFramework::instance().runSuite("arithmetic");
  
  // Print report
  TestFramework::instance().printReport({results});
}

// Example 10: Snapshots and Versioning
void example_snapshots() {
  SnapshotManager snapshots;
  
  // Compile initial version
  CompilerDriver driver;
  auto v1 = driver.compileString("fn add(a, b) { return a + b }");
  
  if (v1.success && v1.chunk) {
    // Create snapshot
    // std::string id1 = snapshots.createSnapshot(ast, *v1.chunk, "Initial version");
    
    // Compile modified version
    auto v2 = driver.compileString("fn add(a, b) { return a + b + 1 }");
    
    if (v2.success && v2.chunk) {
      // std::string id2 = snapshots.createSnapshot(ast2, *v2.chunk, "Modified version");
      
      // Compare snapshots
      // auto diff = snapshots.diffSnapshots(id1, id2);
    }
  }
}

// Example 11: Security Analysis
void example_security() {
  SecurityAnalyzer security;
  
  // Compile potentially unsafe code
  CompilerDriver driver;
  auto result = driver.compileString(R"(
    let userInput = getInput()
    let query = "SELECT * FROM users WHERE name = '" + userInput + "'"
  )");
  
  if (result.success) {
    // Analyze for vulnerabilities
    // auto vulns = security.analyze(*result.chunk);
    
    // Print report
    // std::cout << security.generateReport(vulns) << "\n";
  }
}

// Example 12: Documentation Generation
void example_documentation() {
  DocumentationExtractor docs;
  
  // Extract from source
  CompilerDriver driver;
  auto result = driver.compileString(R"(
    /// Calculate the factorial of n
    /// @param n - The number to calculate factorial for
    /// @return The factorial of n
    fn factorial(n) {
      if n <= 1 { return 1 }
      return n * factorial(n - 1)
    }
  )");
  
  if (result.success) {
    // auto symbols = docs.extractFromAST(ast);
    // auto markdown = docs.generateMarkdown(symbols);
    // std::cout << markdown << "\n";
  }
}

// Example 13: Parallel Compilation
void example_parallelCompilation() {
  // Create thread pool
  ThreadPool pool(4);
  
  // Parallel compiler
  ParallelCompiler pc(pool);
  
  // Compile multiple files
  std::vector<std::pair<std::string, std::string>> sources = {
    {"file1.havel", "fn f1() {}"},
    {"file2.havel", "fn f2() {}"},
    {"file3.havel", "fn f3() {}"},
    {"file4.havel", "fn f4() {}"}
  };
  
  auto results = pc.compileBatch(sources);
  
  int successCount = 0;
  for (const auto& result : results) {
    if (result.success) successCount++;
  }
  
  std::cout << "Compiled " << successCount << "/" << results.size() << " files\n";
}

// Example 14: Configuration Management
void example_configuration() {
  ConfigManager& config = ConfigManager::instance();
  
  // Set configuration values
  config.set<bool>("compiler.optimize", true);
  config.set<int>("compiler.optimizationLevel", 3);
  config.set<std::string>("compiler.target", "native");
  
  // Load from file
  // config.loadFromFile("havel.config");
  
  // Get pipeline options
  auto pipelineOpts = config.getPipelineOptions();
  
  // Create driver with config
  CompilerDriver driver(pipelineOpts);
}

// Main entry point
int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  
  std::cout << "Havel Compiler API Examples\n";
  std::cout << "===========================\n\n";
  
  example_simpleCompilation();
  std::cout << "\n";
  
  example_advancedConfiguration();
  std::cout << "\n";
  
  example_optimization();
  std::cout << "\n";
  
  example_dependencies();
  std::cout << "\n";
  
  example_profiling();
  std::cout << "\n";
  
  std::cout << "All examples completed!\n";
  
  return 0;
}
