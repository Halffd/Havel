// src/havel-lang/engine/Engine.hpp
#pragma once

#include "../lexer/Lexer.hpp"
#include "../parser/Parser.h"
#include "../runtime/Interpreter.hpp"
#include "../ast/AST.h"
#include <string>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>

#ifdef HAVEL_ENABLE_LLVM
#include "../compiler/Compiler.hpp"
#include "../compiler/JIT.hpp"
#endif

namespace havel::engine {

enum class ExecutionMode {
    INTERPRETER,    // Pure interpreter (debugging, development)
    JIT,           // LLVM JIT compilation (production speed)
    AOT            // Ahead-of-time compilation (standalone executables)
};

enum class OptimizationLevel {
    NONE,      // -O0 (debugging)
    BASIC,     // -O1 (basic optimizations)
    STANDARD,  // -O2 (standard optimizations)
    AGGRESSIVE // -O3 (maximum optimizations)
};

struct PerformanceStats {
    std::chrono::microseconds lexingTime{0};
    std::chrono::microseconds parsingTime{0};
    std::chrono::microseconds compilationTime{0};
    std::chrono::microseconds executionTime{0};
    size_t hotkeyCount = 0;
    size_t astNodeCount = 0;
    bool jitEnabled = false;
};

struct EngineConfig {
    ExecutionMode mode = ExecutionMode::JIT;
    OptimizationLevel optimization = OptimizationLevel::STANDARD;
    bool verboseOutput = false;
    bool enableProfiler = false;
    bool dumpIR = false;  // Dump LLVM IR for debugging
    bool dumpAST = false; // Dump AST for debugging
    std::string targetTriple = ""; // For cross-compilation
    std::string logLevel = "INFO"; // DEBUG, INFO, WARN, ERROR
};

class Engine {
private:
    EngineConfig config;
    PerformanceStats stats;

    // Core components
    std::unique_ptr<havel::Lexer> lexer;
    std::unique_ptr<havel::parser::Parser> parser;
    std::unique_ptr<havel::Interpreter> interpreter;

#ifdef HAVEL_ENABLE_LLVM
    std::unique_ptr<havel::compiler::Compiler> llvmCompiler;
    std::unique_ptr<havel::compiler::JIT> jitEngine;
#endif

    // Performance tracking
    std::chrono::high_resolution_clock::time_point startTime;

public:
    explicit Engine(const EngineConfig& cfg = {});
    ~Engine() = default;

    // 🔥 MAIN EXECUTION METHODS 🔥

    // Execute Havel script from file
    havel::HavelValue RunScript(const std::string& filePath);

    // Execute Havel code from string
    havel::HavelValue ExecuteCode(const std::string& sourceCode);

    // Register hotkeys from script
    void RegisterHotkeys(const std::string& filePath);
    void RegisterHotkeysFromCode(const std::string& sourceCode);

    // 🚀 COMPILATION METHODS 🚀

#ifdef HAVEL_ENABLE_LLVM
    // JIT compile and execute (hybrid mode)
    havel::HavelValue ExecuteJIT(const std::string& sourceCode);

    // Ahead-of-time compilation to executable
    bool CompileToExecutable(const std::string& inputFile, const std::string& outputPath);

    // Compile to object file
    bool CompileToObject(const std::string& inputFile, const std::string& objectPath);

    // Pre-compile hotkeys for maximum speed
    void PrecompileHotkeys(const std::string& sourceCode);

    // Get LLVM IR as string (for debugging)
    std::string GetLLVMIR(const std::string& sourceCode);
#endif

    // 🛠️ UTILITY METHODS 🛠️

    // Change execution mode at runtime
    void SetExecutionMode(ExecutionMode mode);

    // Update configuration
    void UpdateConfig(const EngineConfig& newConfig);

    // Performance profiling
    void StartProfiling();
    void StopProfiling();
    void PrintPerformanceStats() const;
    const PerformanceStats& GetPerformanceStats() const;

    // AST utilities
    void DumpAST(const std::string& sourceCode);
    void ValidateScript(const std::string& filePath);
    std::unique_ptr<havel::ast::Program> ParseToAST(const std::string& sourceCode);

    // Engine information
    std::string GetVersionInfo() const;
    std::string GetBuildInfo() const;
    bool IsLLVMEnabled() const;
    ExecutionMode GetExecutionMode() const;

    // Testing utilities
    bool RunTestSuite();
    bool BenchmarkPerformance();

private:
    // Helper methods
    std::string ReadFile(const std::string& filePath);
    void InitializeComponents();
    void LogExecutionTime(const std::string& operation);
    void Log(const std::string& level, const std::string& message);

#ifdef HAVEL_ENABLE_LLVM
    void InitializeLLVM();
    void SetLLVMOptimizationLevel();
#endif
};

// 🎯 FACTORY FUNCTIONS FOR EASY CREATION 🎯

// Create engine for development (interpreter mode)
std::unique_ptr<Engine> CreateDevelopmentEngine();

// Create engine for production (JIT mode)
std::unique_ptr<Engine> CreateProductionEngine();

// Create engine for compilation (AOT mode)
std::unique_ptr<Engine> CreateCompilerEngine();

// Create engine for testing
std::unique_ptr<Engine> CreateTestEngine();

} // namespace havel::engine