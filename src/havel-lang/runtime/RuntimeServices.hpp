/*
 * RuntimeServices.hpp
 *
 * Container for runtime services.
 * Owned by Interpreter, accessed by evaluators.
 */
#pragma once

#include <memory>
#include "modules/process/ShellExecutor.hpp"
#include "modules/io/InputModule.hpp"
#include "modules/config/ConfigProcessor.hpp"
#include "services/CallDispatcher.hpp"
#include "services/MemberResolver.hpp"

namespace havel {

// Forward declarations
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

    // Function call dispatch (requires Interpreter pointer)
    std::unique_ptr<CallDispatcher> callDispatcher;  // Created by Interpreter

    // Member access resolution (requires Interpreter pointer)
    std::unique_ptr<MemberResolver> memberResolver;  // Created by Interpreter

    RuntimeServices() : shell(), input(nullptr) {}

    void setInput(std::shared_ptr<IO> io) {
        input = new InputModule(io.get());
        ioRef = io;  // Keep shared_ptr alive
    }

    void createCallDispatcher(Interpreter* interp) {
        callDispatcher = std::make_unique<CallDispatcher>(interp);
    }

    void createMemberResolver(Interpreter* interp) {
        memberResolver = std::make_unique<MemberResolver>(interp);
    }

    ~RuntimeServices() {
        delete input;
        // callDispatcher and memberResolver are unique_ptr, auto-deleted
    }

private:
    std::shared_ptr<IO> ioRef;  // Keep IO alive
};

} // namespace havel
