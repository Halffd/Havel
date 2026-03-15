/*
 * ShellExecutor.cpp
 *
 * Shell command execution service.
 * Separates shell execution logic from evaluator.
 */
#include "ShellExecutor.hpp"
#include "process/Launcher.hpp"
#include "utils/Logger.hpp"
#include <fstream>
#include <unistd.h>

namespace havel {

ShellResult ShellExecutor::executeShell(const std::string& command) {
    return executeSingle(command, true);
}

ShellResult ShellExecutor::execute(const std::string& executable, 
                                    const std::vector<std::string>& args) {
    // Build command string for Launcher
    auto result = Launcher::run(executable, args);
    
    ShellResult shellResult;
    shellResult.stdout = result.stdout;
    shellResult.stderr = result.stderr;
    shellResult.exitCode = result.exitCode;
    shellResult.success = result.success;
    shellResult.error = result.error;
    
    return shellResult;
}

ShellResult ShellExecutor::executeChain(const std::vector<std::string>& commands) {
    ShellResult result;
    std::string inputStdin;
    int pipeStage = 0;

    for (const auto& command : commands) {
        // Execute command
        if (!inputStdin.empty()) {
            // Pipe stdin from previous command
            std::string tempFile = "/tmp/havel_pipe_" + 
                                   std::to_string(getpid()) + "_" + 
                                   std::to_string(pipeStage++);
            std::ofstream ofs(tempFile);
            ofs << inputStdin;
            ofs.close();

            std::string pipedCommand = "cat " + tempFile + " | " + command;
            result = executeSingle(pipedCommand, true);
            std::remove(tempFile.c_str());
        } else {
            result = executeSingle(command, true);
        }

        // Pass stdout to next command
        inputStdin = result.stdout;

        // Stop on error
        if (!result.success) {
            break;
        }
    }

    return result;
}

std::vector<std::string> ShellExecutor::splitPipes(const std::string& command) {
    std::vector<std::string> parts;
    std::string current;
    bool inQuotes = false;
    char quoteChar = 0;
    
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        
        // Handle quotes
        if ((c == '"' || c == '\'') && !inQuotes) {
            inQuotes = true;
            quoteChar = c;
            current += c;
            continue;
        }
        
        if (inQuotes && c == quoteChar) {
            inQuotes = false;
            quoteChar = 0;
            current += c;
            continue;
        }
        
        // Handle pipe (only outside quotes)
        if (c == '|' && !inQuotes) {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        
        current += c;
    }
    
    if (!current.empty()) {
        parts.push_back(current);
    }
    
    return parts;
}

ShellResult ShellExecutor::executeSingle(const std::string& command, bool useShell) {
    ShellResult result;
    
    if (useShell) {
        auto launcherResult = Launcher::runShell(command);
        result.stdout = launcherResult.stdout;
        result.stderr = launcherResult.stderr;
        result.exitCode = launcherResult.exitCode;
        result.success = launcherResult.success;
        result.error = launcherResult.error;
    } else {
        // Parse command and args
        std::vector<std::string> args;
        std::string current;
        bool inQuotes = false;
        
        for (char c : command) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ' ' && !inQuotes) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            args.push_back(current);
        }
        
        if (args.empty()) {
            result.success = false;
            result.error = "Empty command";
            result.exitCode = 1;
            return result;
        }
        
        auto launcherResult = Launcher::run(args[0], 
                                            std::vector<std::string>(args.begin() + 1, args.end()));
        result.stdout = launcherResult.stdout;
        result.stderr = launcherResult.stderr;
        result.exitCode = launcherResult.exitCode;
        result.success = launcherResult.success;
        result.error = launcherResult.error;
    }
    
    return result;
}

} // namespace havel
