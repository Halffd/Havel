#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include "../havel-lang/runtime/Interpreter.hpp"

namespace havel {

/**
 * ShellResult - Structured shell command result
 * 
 * More efficient than unordered_map for shell operations.
 * Wrapped in HavelValue for interpreter access.
 */
struct ShellResult {
    std::string stdout_;
    std::string stderr_;
    int exitCode;
    bool success;
    
    ShellResult() : exitCode(0), success(true) {}
    ShellResult(const std::string& out, const std::string& err, int code)
        : stdout_(out), stderr_(err), exitCode(code), success(code == 0) {}
    
    // Convert to HavelValue object
    HavelValue toHavelValue() const;
};

/**
 * ShellExecutor - Execute shell commands and pipes
 * 
 * Current implementation uses temp files for pipes (slow but simple).
 * TODO: Implement proper Unix pipes with pipe()/fork()/dup2()/exec()
 */
class ShellExecutor {
public:
    // Execute single command
    static ShellResult executeShell(const std::string& command);
    
    // Execute command chain with pipes (current: temp file based)
    static ShellResult executeChain(const std::vector<std::string>& commands);
    
    // Parse command string into arguments (handles quotes)
    static std::vector<std::string> parseCommand(const std::string& command);
    
    // Split command by pipe character (respects quotes)
    static std::vector<std::string> splitPipes(const std::string& command);
};

} // namespace havel
