/*
 * ShellExecutor.cpp
 *
 * Shell command execution service.
 * Separates shell execution logic from evaluator.
 */
#include "ShellExecutor.hpp"
#include "process/Launcher.hpp"
#include "utils/Logger.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>

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
    
    if (commands.empty()) {
        result.success = false;
        result.error = "Empty command chain";
        result.exitCode = 1;
        return result;
    }
    
    if (commands.size() == 1) {
        return executeSingle(commands[0], true);
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
            for (int fd : pipes) {
                close(fd);
            }
            close(outputPipe[0]);
            close(outputPipe[1]);
            
            // Wait for any started processes
            for (pid_t startedPid : pids) {
                waitpid(startedPid, nullptr, 0);
            }
            
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
            for (int fd : pipes) {
                close(fd);
            }
            close(outputPipe[0]);
            close(outputPipe[1]);
            
            // Execute command through shell
            execl("/bin/sh", "sh", "-c", commands[i].c_str(), nullptr);
            
            // If exec fails
            _exit(127);
        }
        
        // Parent process
        pids.push_back(pid);
    }
    
    // Parent: close all pipe ends (child has duplicates)
    for (int fd : pipes) {
        close(fd);
    }
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
