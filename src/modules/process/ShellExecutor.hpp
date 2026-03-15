/*
 * ShellExecutor.hpp
 *
 * Shell command execution service.
 * Separates shell execution logic from evaluator.
 */
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace havel {

// Shell execution result
struct ShellResult {
    std::string stdout;
    std::string stderr;
    int exitCode;
    bool success;
    std::string error;
};

/**
 * ShellExecutor - Execute shell commands
 * 
 * Provides shell command execution with:
 * - Direct execution (no shell)
 * - Shell execution (with pipe support)
 * - Command chaining (pipes)
 * - Result capture (stdout, stderr, exit code)
 */
class ShellExecutor {
public:
    ShellExecutor() = default;
    ~ShellExecutor() = default;

    /**
     * Execute command through shell
     * @param command Command string (may contain pipes)
     * @return ShellResult with stdout, stderr, exit code
     */
    ShellResult executeShell(const std::string& command);

    /**
     * Execute command directly (no shell)
     * @param executable Executable path
     * @param args Arguments
     * @return ShellResult with stdout, stderr, exit code
     */
    ShellResult execute(const std::string& executable, 
                        const std::vector<std::string>& args);

    /**
     * Execute command chain (pipe support)
     * @param commands Vector of command strings
     * @return ShellResult from final command
     */
    ShellResult executeChain(const std::vector<std::string>& commands);
    
    /**
     * Split command string by pipe character (respects quotes)
     * @param command Command string potentially containing pipes
     * @return Vector of command strings
     */
    static std::vector<std::string> splitPipes(const std::string& command);

private:
    // Internal execution helpers
    ShellResult executeSingle(const std::string& command, bool useShell);
};

} // namespace havel
