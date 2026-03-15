#include "ShellExecutor.hpp"
#include "Launcher.hpp"
#include <fstream>
#include <cstdlib>
#include <unistd.h>

namespace havel {

HavelValue ShellResult::toHavelValue() const {
    auto resultObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    (*resultObj)["stdout"] = HavelValue(stdout_);
    (*resultObj)["stderr"] = HavelValue(stderr_);
    (*resultObj)["exitCode"] = HavelValue(static_cast<double>(exitCode));
    (*resultObj)["success"] = HavelValue(success);
    return HavelValue(resultObj);
}

ShellResult ShellExecutor::executeShell(const std::string& command) {
    ProcessResult result = Launcher::runShell(command);
    return ShellResult(result.stdout, result.stderr, result.exitCode);
}

ShellResult ShellExecutor::executeChain(const std::vector<std::string>& commands) {
    if (commands.empty()) {
        return ShellResult("", "", 0);
    }
    
    if (commands.size() == 1) {
        return executeShell(commands[0]);
    }
    
    // TODO: Implement proper Unix pipes with pipe()/fork()/dup2()/exec()
    // Current implementation uses temp files (slow but works)
    
    std::string inputStdin;
    int pipeStage = 0;
    ProcessResult result;
    
    for (const auto& command : commands) {
        std::string fullCommand = command;
        
        // If there's stdin from previous command, pipe it
        if (!inputStdin.empty()) {
            // Write stdin to a temp file with unique name
            std::string tempFile = "/tmp/havel_pipe_" + std::to_string(getpid()) + "_" + std::to_string(pipeStage++);
            std::ofstream ofs(tempFile);
            ofs << inputStdin;
            ofs.close();
            
            fullCommand = "cat " + tempFile + " | " + fullCommand;
            result = Launcher::runShell(fullCommand);
            std::remove(tempFile.c_str());
        } else {
            result = Launcher::runShell(fullCommand);
        }
        
        // Prepare stdin for next command in chain
        inputStdin = result.stdout;
    }
    
    return ShellResult(result.stdout, result.stderr, result.exitCode);
}

std::vector<std::string> ShellExecutor::parseCommand(const std::string& command) {
    std::vector<std::string> args;
    std::string current;
    bool inQuotes = false;
    char quoteChar = 0;
    
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        
        // Handle escape sequences
        if (c == '\\' && i + 1 < command.size()) {
            current += command[++i];
            continue;
        }
        
        // Handle quotes
        if ((c == '"' || c == '\'') && !inQuotes) {
            inQuotes = true;
            quoteChar = c;
            continue;
        }
        
        if (inQuotes && c == quoteChar) {
            inQuotes = false;
            quoteChar = 0;
            continue;
        }
        
        // Handle spaces (only outside quotes)
        if (c == ' ' && !inQuotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
            continue;
        }
        
        current += c;
    }
    
    if (!current.empty()) {
        args.push_back(current);
    }
    
    return args;
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

} // namespace havel
