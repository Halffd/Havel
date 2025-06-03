// src/havel-lang/engine/Engine.cpp
#include "Engine.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>

namespace havel::engine {

Engine::Engine(const EngineConfig& cfg) : config(cfg) {
    InitializeComponents();

#ifdef HAVEL_ENABLE_LLVM
    if (config.mode == ExecutionMode::JIT || config.mode == ExecutionMode::AOT) {
        InitializeLLVM();
    }
#endif

    if (config.verboseOutput) {
        std::cout << "🔥 Havel Engine initialized in "
                  << (config.mode == ExecutionMode::INTERPRETER ? "INTERPRETER" :
                      config.mode == ExecutionMode::JIT ? "JIT" : "AOT")
                  << " mode" << std::endl;
    }
}

void Engine::InitializeComponents() {
    // Always create parser and interpreter
    parser = std::make_unique<parser::Parser>();
    interpreter = std::make_unique<runtime::Interpreter>();

    if (config.verboseOutput) {
        std::cout << "✅ Parser and Interpreter initialized" << std::endl;
    }
}

#ifdef HAVEL_ENABLE_LLVM
void Engine::InitializeLLVM() {
    try {
        llvmCompiler = std::make_unique<compiler::Compiler>();
        jitEngine = std::make_unique<compiler::JIT>();

        SetLLVMOptimizationLevel();

        if (config.verboseOutput) {
            std::cout << "✅ LLVM Compiler and JIT Engine initialized" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Failed to initialize LLVM: " << e.what() << std::endl;
        throw;
    }
}

void Engine::SetLLVMOptimizationLevel() {
    // Configure LLVM optimization based on our optimization level
    switch (config.optimization) {
        case OptimizationLevel::NONE:
            // -O0 equivalent
            break;
        case OptimizationLevel::BASIC:
            // -O1 equivalent
            break;
        case OptimizationLevel::STANDARD:
            // -O2 equivalent
            break;
        case OptimizationLevel::AGGRESSIVE:
            // -O3 equivalent with aggressive optimizations
            break;
    }
}
#endif

// 🔥 MAIN EXECUTION METHODS 🔥

runtime::HavelValue Engine::RunScript(const std::string& filePath) {
    if (config.enableProfiler) StartProfiling();

    std::string sourceCode = ReadFile(filePath);
    auto result = ExecuteCode(sourceCode);

    if (config.enableProfiler) {
        StopProfiling();
        LogExecutionTime("RunScript(" + filePath + ")");
    }

    return result;
}

runtime::HavelValue Engine::ExecuteCode(const std::string& sourceCode) {
    if (config.enableProfiler) StartProfiling();

    try {
        switch (config.mode) {
            case ExecutionMode::INTERPRETER:
                return interpreter->Execute(sourceCode);

#ifdef HAVEL_ENABLE_LLVM
            case ExecutionMode::JIT:
                return ExecuteJIT(sourceCode);

            case ExecutionMode::AOT:
                throw std::runtime_error("AOT mode requires CompileToExecutable, not ExecuteCode");
#endif
            default:
                throw std::runtime_error("Unsupported execution mode");
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Execution error: " << e.what() << std::endl;
        throw;
    }

    if (config.enableProfiler) {
        StopProfiling();
        LogExecutionTime("ExecuteCode");
    }
}

#ifdef HAVEL_ENABLE_LLVM
runtime::HavelValue Engine::ExecuteJIT(const std::string& sourceCode) {
    if (config.verboseOutput) {
        std::cout << "🚀 JIT compiling Havel code..." << std::endl;
    }

    // Parse to AST
    auto program = parser->produceAST(sourceCode);

    if (config.dumpIR) {
        std::cout << "📋 AST:" << std::endl;
        parser->printAST(*program);
    }

    // JIT compile and execute
    jitEngine->CompileScript(*program);

    if (config.verboseOutput) {
        std::cout << "✅ JIT compilation complete, executing native code..." << std::endl;
    }

    // For now, return null - JIT execution handles hotkey registration
    return nullptr;
}

bool Engine::CompileToExecutable(const std::string& inputFile, const std::string& outputPath) {
    if (config.verboseOutput) {
        std::cout << "🔨 AOT compiling " << inputFile << " to " << outputPath << std::endl;
    }

    try {
        // Read source code
        std::string sourceCode = ReadFile(inputFile);

        // Parse to AST
        auto program = parser->produceAST(sourceCode);

        if (config.dumpIR) {
            std::cout << "📋 AST for AOT compilation:" << std::endl;
            parser->printAST(*program);
        }

        // Compile to object file first
        std::string objectPath = outputPath + ".o";
        if (!CompileToObject(inputFile, objectPath)) {
            return false;
        }

        // Link to create executable
        std::string linkCommand = "clang++ -o " + outputPath + " " + objectPath;

        if (config.verboseOutput) {
            std::cout << "🔗 Linking: " << linkCommand << std::endl;
        }

        int result = system(linkCommand.c_str());

        // Clean up object file
        remove(objectPath.c_str());

        if (result == 0) {
            std::cout << "✅ Successfully compiled to: " << outputPath << std::endl;
            return true;
        } else {
            std::cerr << "❌ Linking failed" << std::endl;
            return false;
        }

    } catch (const std::exception& e) {
        std::cerr << "❌ AOT compilation error: " << e.what() << std::endl;
        return false;
    }
}

bool Engine::CompileToObject(const std::string& inputFile, const std::string& objectPath) {
    try {
        std::string sourceCode = ReadFile(inputFile);
        auto program = parser->produceAST(sourceCode);

        // Generate LLVM IR and compile to object file
        auto compiledFunc = llvmCompiler->CompileProgram(*program);

        // TODO: Implement object file generation
        // This would involve using LLVM's TargetMachine to emit object code

        return true;
    } catch (const std::exception& e) {
        std::cerr << "❌ Object compilation error: " << e.what() << std::endl;
        return false;
    }
}

void Engine::PrecompileHotkeys(const std::string& sourceCode) {
    if (config.verboseOutput) {
        std::cout << "⚡ Pre-compiling hotkeys for maximum performance..." << std::endl;
    }

    auto program = parser->produceAST(sourceCode);
    jitEngine->CompileScript(*program);

    std::cout << "🔥 Hotkeys compiled to native machine code!" << std::endl;
}
#endif

// 🛠️ UTILITY METHODS 🛠️

void Engine::RegisterHotkeys(const std::string& filePath) {
    std::string sourceCode = ReadFile(filePath);
    RegisterHotkeysFromCode(sourceCode);
}

void Engine::RegisterHotkeysFromCode(const std::string& sourceCode) {
#ifdef HAVEL_ENABLE_LLVM
    if (config.mode == ExecutionMode::JIT) {
        PrecompileHotkeys(sourceCode);
        return;
    }
#endif

    // Fallback to interpreter
    interpreter->RegisterHotkeys(sourceCode);
}

void Engine::SetExecutionMode(ExecutionMode mode) {
    config.mode = mode;

#ifdef HAVEL_ENABLE_LLVM
    if (mode == ExecutionMode::JIT || mode == ExecutionMode::AOT) {
        if (!llvmCompiler) {
            InitializeLLVM();
        }
    }
#endif

    if (config.verboseOutput) {
        std::cout << "🔄 Switched to "
                  << (mode == ExecutionMode::INTERPRETER ? "INTERPRETER" :
                      mode == ExecutionMode::JIT ? "JIT" : "AOT")
                  << " mode" << std::endl;
    }
}

void Engine::StartProfiling() {
    startTime = std::chrono::high_resolution_clock::now();
}

void Engine::StopProfiling() {
    // Timing is handled in LogExecutionTime
}

void Engine::LogExecutionTime(const std::string& operation) {
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    std::cout << "⏱️  " << operation << " took " << duration.count() << " μs" << std::endl;
}

void Engine::DumpAST(const std::string& sourceCode) {
    auto program = parser->produceAST(sourceCode);
    std::cout << "📋 AST Dump:" << std::endl;
    parser->printAST(*program);
}

std::string Engine::ReadFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filePath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string Engine::GetVersionInfo() const {
    return "Havel Engine v1.0.0";
}

std::string Engine::GetBuildInfo() const {
    std::stringstream info;
    info << "Havel Engine Build Info:\n";
    info << "- Execution Mode: " << (config.mode == ExecutionMode::INTERPRETER ? "Interpreter" :
                                    config.mode == ExecutionMode::JIT ? "JIT" : "AOT") << "\n";
    info << "- Optimization: " << (config.optimization == OptimizationLevel::NONE ? "None" :
                                  config.optimization == OptimizationLevel::BASIC ? "Basic" :
                                  config.optimization == OptimizationLevel::STANDARD ? "Standard" : "Aggressive") << "\n";
    info << "- C++ Standard: C++23\n";
    info << "- Compiler: " <<
#ifdef __clang__
        "Clang " << __clang_major__ << "." << __clang_minor__ << "\n";
#elif defined(__GNUC__)
        "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "\n";
#else
        "Unknown\n";
#endif

#ifdef HAVEL_ENABLE_LLVM
    info << "- LLVM JIT: Enabled\n";
#else
    info << "- LLVM JIT: Disabled\n";
#endif

    info << "- Build Type: " <<
#ifdef DEBUG_BUILD
        "Debug\n";
#else
        "Release\n";
#endif

    return info.str();
}

bool Engine::IsLLVMEnabled() const {
#ifdef HAVEL_ENABLE_LLVM
    return true;
#else
    return false;
#endif
}

void Engine::ValidateScript(const std::string& filePath) {
    try {
        std::string sourceCode = ReadFile(filePath);
        auto program = parser->produceAST(sourceCode);

        std::cout << "✅ Script validation passed: " << filePath << std::endl;
        std::cout << "📊 Found " << program->body.size() << " top-level statements" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "❌ Script validation failed: " << e.what() << std::endl;
        throw;
    }
}

void Engine::PrintPerformanceStats() {
    std::cout << "\n🔥 HAVEL ENGINE PERFORMANCE STATS 🔥\n";
    std::cout << "======================================\n";
    std::cout << GetBuildInfo() << std::endl;

    // TODO: Add more detailed performance metrics
    // - Number of hotkeys registered
    // - Compilation times
    // - Memory usage
    // - JIT vs Interpreter performance comparison
}

void Engine::UpdateConfig(const EngineConfig& newConfig) {
    bool modeChanged = (config.mode != newConfig.mode);
    config = newConfig;

    if (modeChanged) {
        SetExecutionMode(config.mode);
    }

#ifdef HAVEL_ENABLE_LLVM
    if (llvmCompiler) {
        SetLLVMOptimizationLevel();
    }
#endif
}

// 🎯 FACTORY FUNCTIONS 🎯

std::unique_ptr<Engine> CreateDevelopmentEngine() {
    EngineConfig config;
    config.mode = ExecutionMode::INTERPRETER;
    config.optimization = OptimizationLevel::NONE;
    config.verboseOutput = true;
    config.enableProfiler = true;
    config.dumpIR = false;

    return std::make_unique<Engine>(config);
}

std::unique_ptr<Engine> CreateProductionEngine() {
    EngineConfig config;
#ifdef HAVEL_ENABLE_LLVM
    config.mode = ExecutionMode::JIT;
#else
    config.mode = ExecutionMode::INTERPRETER;
#endif
    config.optimization = OptimizationLevel::AGGRESSIVE;
    config.verboseOutput = false;
    config.enableProfiler = false;
    config.dumpIR = false;

    return std::make_unique<Engine>(config);
}

std::unique_ptr<Engine> CreateCompilerEngine() {
    EngineConfig config;
#ifdef HAVEL_ENABLE_LLVM
    config.mode = ExecutionMode::AOT;
#else
    throw std::runtime_error("AOT compilation requires LLVM support");
#endif
    config.optimization = OptimizationLevel::AGGRESSIVE;
    config.verboseOutput = true;
    config.enableProfiler = false;
    config.dumpIR = true;

    return std::make_unique<Engine>(config);
}

} // namespace havel::engine