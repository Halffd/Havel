/*
 * ShellExecutor.cpp
 *
 * Shell command execution service.
 * Separates shell execution logic from evaluator.
 */
#include "ShellExecutor.hpp"
#include "core/process/Launcher.hpp"
#include "utils/Logger.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>

namespace havel {

// Whitelist of allowed commands for shell execution
static const std::vector<std::string> ALLOWED_COMMANDS = {
    "ls", "cat", "echo", "grep", "find", "wc", "head", "tail",
    "mkdir", "rmdir", "cp", "mv", "rm", "touch", "stat",
    "ps", "df", "du", "free", "uptime", "whoami", "id",
    "date", "cal", "sleep", "sort", "uniq", "cut", "tr",
    "awk", "sed", "tee", "xargs", "which", "whereis",
    "git", "cargo", "npm", "make", "cmake", "clang", "gcc",
    "python3", "python", "node", "deno", "bun",
    "ssh", "scp", "rsync", "curl", "wget", "ping", "dig",
    "tar", "gzip", "gunzip", "zip", "unzip", "bzip2", "bunzip2"
};

// Check if a command is in the allowlist
static bool isCommandAllowed(const std::string& command) {
    // Extract the first word (command name)
    std::string cmd;
    size_t pos = command.find_first_not_of(" \t");
    if (pos != std::string::npos) {
        size_t end = command.find_first_of(" \t", pos);
        cmd = command.substr(pos, end - pos);
    }
    
    // Get basename of command (in case full path provided)
    size_t slashPos = cmd.rfind('/');
    if (slashPos != std::string::npos) {
        cmd = cmd.substr(slashPos + 1);
    }
    
    for (const auto& allowed : ALLOWED_COMMANDS) {
        if (allowed == cmd) return true;
    }
    return false;
}

// Safer argument parsing that avoids shell metacharacters
static std::vector<std::string> parseCommandArgs(const std::string& command) {
    std::vector<std::string> args;
    std::string current;
    bool inDQuote = false, inSQuote = false;
    bool escape = false;
    
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (escape) {
            current += c;
            escape = false;
            continue;
        }
        if (c == '\\' && !inSQuote) {
            escape = true;
            continue;
        }
        if (c == '"' && !inSQuote) {
            inDQuote = !inDQuote;
            continue;
        }
        if (c == '\'' && !inDQuote) {
            inSQuote = !inSQuote;
            continue;
        }
        if (!inDQuote && !inSQuote && std::isspace(c)) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

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
    
    if (commands.empty()) {
        result.success = false;
        result.error = "Empty command chain";
        result.exitCode = 1;
        return result;
    }
    
    if (commands.size() == 1) {
        return executeSingle(commands[0], true);
    }
    
    // Validate all commands in chain
    for (const auto& cmd : commands) {
        if (!isCommandAllowed(cmd)) {
            result.success = false;
            result.error = "Command not allowed: " + cmd;
            result.exitCode = 126;
            return result;
        }
    }
    
    // Implement proper Unix pipes with pipe()/fork()/dup2()/exec()
    // Pipeline: cmd1 | cmd2 | cmd3 | ... | cmdN
    
    int numCommands = commands.size();
    std::vector<int> pipes;
    
    // Create pipes: need (n-1) pipes for n commands
    for (int i = 0; i < numCommands - 1; ++i) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            result.success = false;
            result.error = std::string("pipe() failed: ") + strerror(errno);
            result.exitCode = 1;
            return result;
        }
        pipes.push_back(pipefd[0]);  // read end
        pipes.push_back(pipefd[1]);  // write end
    }
    
    // Create pipe for capturing final stdout/stderr
    int outputPipe[2];
    if (pipe(outputPipe) == -1) {
        result.success = false;
        result.error = std::string("pipe() failed: ") + strerror(errno);
        result.exitCode = 1;
        return result;
    }
    
    // Fork processes for each command
    std::vector<pid_t> pids;
    for (int i = 0; i < numCommands; ++i) {
        pid_t pid = fork();
        
        if (pid == -1) {
            // Fork failed - cleanup
            result.success = false;
            result.error = std::string("fork() failed: ") + strerror(errno);
            result.exitCode = 1;
            
            // Close all pipe ends
            for (int fd : pipes) close(fd);
            close(outputPipe[0]);
            close(outputPipe[1]);
            
            // Wait for any started processes
            for (pid_t startedPid : pids) waitpid(startedPid, nullptr, 0);
            return result;
        }
        
        if (pid == 0) {
            // Child process
            
            // Setup stdin from previous pipe (except first command)
            if (i > 0) {
                int readFd = pipes[(i - 1) * 2];
                dup2(readFd, STDIN_FILENO);
            }
            
            // Setup stdout: last command goes to output pipe, others to next pipe
            if (i < numCommands - 1) {
                int writeFd = pipes[i * 2 + 1];
                dup2(writeFd, STDOUT_FILENO);
            } else {
                // Last command: stdout to output pipe
                dup2(outputPipe[1], STDOUT_FILENO);
                dup2(outputPipe[1], STDERR_FILENO);  // Also capture stderr
            }
            
            // Close all pipe ends in child
            for (int fd : pipes) close(fd);
            close(outputPipe[0]);
            close(outputPipe[1]);
            
            // Parse command and execute directly (NO SHELL)
            auto args = parseCommandArgs(commands[i]);
            if (args.empty()) _exit(127);
            
            // Validate command is allowed
            if (!isCommandAllowed(commands[i])) _exit(126);
            
            // Execute directly using execvp
            std::vector<char*> cargv;
            cargv.reserve(args.size() + 1);
            for (const auto& a : args) {
                cargv.push_back(const_cast<char*>(a.c_str()));
            }
            cargv.push_back(nullptr);
            
            execvp(cargv[0], cargv.data());
            _exit(127);
        }
        
        // Parent process
        pids.push_back(pid);
    }
    
    // Parent: close all pipe ends (children have duplicates)
    for (int fd : pipes) close(fd);
    close(outputPipe[1]);  // Close write end, only read
    
    // Read stdout/stderr from last command
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(outputPipe[0], buffer, sizeof(buffer))) > 0) {
        result.stdout.append(buffer, bytesRead);
    }
    close(outputPipe[0]);
    
    // Wait for all children and collect exit codes
    int lastExitCode = 0;
    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            lastExitCode = WEXITSTATUS(status);
        }
    }
    
    result.exitCode = lastExitCode;
    result.success = (lastExitCode == 0);
    
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
    
    // Validate command is allowed
    if (!isCommandAllowed(command)) {
        result.success = false;
        result.error = "Command not allowed: " + command;
        result.exitCode = 126;
        return result;
    }
    
    if (useShell) {
        auto launcherResult = Launcher::runShell(command);
        result.stdout = launcherResult.stdout;
        result.stderr = launcherResult.stderr;
        result.exitCode = launcherResult.exitCode;
        result.success = launcherResult.success;
        result.error = launcherResult.error;
    } else {
        // Parse command and execute directly without shell
        auto args = parseCommandArgs(command);
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
