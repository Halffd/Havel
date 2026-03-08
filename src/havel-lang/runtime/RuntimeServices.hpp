/*
 * RuntimeServices.hpp
 *
 * Container for runtime services.
 * Owned by Interpreter, accessed by evaluators.
 */
#pragma once

#include "modules/process/ShellExecutor.hpp"
#include "modules/io/InputModule.hpp"
#include "modules/config/ConfigProcessor.hpp"

namespace havel {

// Forward declaration
class IO;
class Environment;

/**
 * RuntimeServices - Long-lived runtime service container
 * 
 * Services are created once by Interpreter and reused
 * for all AST node evaluations. This avoids object churn
 * and provides consistent service state.
 * 
 * Usage:
 *   Interpreter interpreter;
 *   interpreter.services.shell.executeShell(cmd);
 */
struct RuntimeServices {
    // Process execution
    ShellExecutor shell;
    
    // Input handling (requires IO pointer)
    InputModule* input;  // Non-owning, set by Interpreter
    
    // Configuration DSL processing
    ConfigProcessor config;
    
    RuntimeServices() : shell(), input(nullptr) {}
    
    void setInput(IO* io) {
        input = new InputModule(io);
    }
    
    ~RuntimeServices() {
        delete input;
    }
};

} // namespace havel
