/*
 * RuntimeContext.hpp
 *
 * Runtime context for evaluators.
 * 
 * This provides evaluators with access to necessary services
 * without depending on Interpreter internals.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace havel {

// Forward declarations
class Environment;
class IO;
struct ShellResult;  // From modules/process/ShellExecutor.hpp

/**
 * Runtime context - minimal interface for evaluators
 * 
 * Evaluators receive this instead of Interpreter*, breaking circular dependencies.
 */
struct RuntimeContext {
    // Environment for variable lookup
    Environment* env = nullptr;

    // IO for system operations (optional - can be null for pure mode)
    IO* io = nullptr;

    // Shell executor
    std::function<ShellResult(const std::string& command)> executeShell;

    // Error reporting
    std::function<void(const std::string&)> reportError;
    std::function<void(const std::string&)> reportWarning;
    std::function<void(const std::string&)> reportInfo;

    // Validation
    bool isValid() const { return env != nullptr; }
};

/**
 * Create a minimal runtime context for pure script execution
 */
RuntimeContext createPureContext(Environment* env);

/**
 * Create a full runtime context with IO access
 */
RuntimeContext createFullContext(Environment* env, IO* io);

} // namespace havel
