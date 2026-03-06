/*
 * ProcessModule.cpp
 * 
 * Async process execution for Havel standard library.
 * Provides spawn(), proc.kill(), proc.wait(), proc.stdout, etc.
 */
#include "ProcessModule.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <memory>

namespace havel::stdlib {

// Process handle - stored as void* in HavelObject
struct ProcessHandle {
    pid_t pid = -1;
    int exitCode = -1;
    bool completed = false;
    bool running = false;
    std::string stdoutBuffer;
    std::string stderrBuffer;
    int stdoutPipe = -1;
    int stderrPipe = -1;
    
    ~ProcessHandle() {
        if (stdoutPipe >= 0) close(stdoutPipe);
        if (stderrPipe >= 0) close(stderrPipe);
    }
};

void registerProcessModule(Environment* env) {
    // Create process namespace
    auto processObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper: convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                std::string s = oss.str();
                if (s.find('.') != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && s[last] == '.') {
                        s = s.substr(0, last);
                    } else if (last != std::string::npos) {
                        s = s.substr(0, last + 1);
                    }
                }
                return s;
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };
    
    // Helper: parse command array to argv
    auto parseCommand = [&valueToString](const HavelValue& cmdValue) -> std::vector<std::string> {
        std::vector<std::string> args;
        if (cmdValue.isArray()) {
            auto arr = cmdValue.asArray();
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    args.push_back(valueToString((*arr)[i]));
                }
            }
        } else {
            // String command - run through shell
            args.push_back("/bin/sh");
            args.push_back("-c");
            args.push_back(valueToString(cmdValue));
        }
        return args;
    };
    
    // ============================================================================
    // spawn(command) - Start async process
    // ============================================================================
    (*processObj)["spawn"] = BuiltinFunction([=](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("spawn() requires a command");
        }
        
        std::vector<std::string> cmd = parseCommand(args[0]);
        if (cmd.empty()) {
            return HavelRuntimeError("spawn(): empty command");
        }
        
        // Create pipes for stdout/stderr
        int stdoutPipe[2] = {-1, -1};
        int stderrPipe[2] = {-1, -1};
        
        if (pipe(stdoutPipe) == -1 || pipe(stderrPipe) == -1) {
            return HavelRuntimeError("spawn(): failed to create pipes");
        }
        
        pid_t pid = fork();
        if (pid == -1) {
            close(stdoutPipe[0]); close(stdoutPipe[1]);
            close(stderrPipe[0]); close(stderrPipe[1]);
            return HavelRuntimeError("spawn(): fork failed");
        }
        
        if (pid == 0) {
            // Child process
            close(stdoutPipe[0]);
            close(stderrPipe[0]);
            dup2(stdoutPipe[1], STDOUT_FILENO);
            dup2(stderrPipe[1], STDERR_FILENO);
            close(stdoutPipe[1]);
            close(stderrPipe[1]);
            
            // Prepare argv
            std::vector<char*> argv;
            for (auto& arg : cmd) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            
            execvp(argv[0], argv.data());
            _exit(127);
        }
        
        // Parent process
        close(stdoutPipe[1]);
        close(stderrPipe[1]);
        
        // Set non-blocking read
        fcntl(stdoutPipe[0], F_SETFL, O_NONBLOCK);
        fcntl(stderrPipe[0], F_SETFL, O_NONBLOCK);
        
        // Create process handle
        auto procHandle = std::make_shared<ProcessHandle>();
        procHandle->pid = pid;
        procHandle->running = true;
        procHandle->stdoutPipe = stdoutPipe[0];
        procHandle->stderrPipe = stderrPipe[0];
        
        // Create process object with closures that capture procHandle
        auto procObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        
        // pid property
        (*procObj)["pid"] = HavelValue(static_cast<double>(pid));
        
        // running property - check if process is still running
        (*procObj)["running"] = BuiltinFunction([procHandle](const std::vector<HavelValue>&) -> HavelResult {
            if (procHandle->completed) {
                return HavelValue(false);
            }
            int status;
            pid_t result = waitpid(procHandle->pid, &status, WNOHANG);
            if (result == -1) {
                procHandle->running = false;
                procHandle->completed = true;
                return HavelValue(false);
            } else if (result == 0) {
                return HavelValue(true);  // Still running
            } else {
                // Process completed
                procHandle->running = false;
                procHandle->completed = true;
                if (WIFEXITED(status)) {
                    procHandle->exitCode = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    procHandle->exitCode = -WTERMSIG(status);
                }
                return HavelValue(false);
            }
        });
        
        // wait() - wait for process to complete
        (*procObj)["wait"] = BuiltinFunction([procHandle](const std::vector<HavelValue>&) -> HavelResult {
            if (procHandle->completed) {
                return HavelValue(static_cast<double>(procHandle->exitCode));
            }
            
            int status;
            pid_t result = waitpid(procHandle->pid, &status, 0);
            if (result == -1) {
                return HavelRuntimeError("wait(): failed to wait for process");
            }
            
            procHandle->completed = true;
            procHandle->running = false;
            if (WIFEXITED(status)) {
                procHandle->exitCode = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                procHandle->exitCode = -WTERMSIG(status);
            }
            
            return HavelValue(static_cast<double>(procHandle->exitCode));
        });
        
        // kill() - terminate process
        (*procObj)["kill"] = BuiltinFunction([procHandle](const std::vector<HavelValue>& args) -> HavelResult {
            if (procHandle->completed) {
                return HavelRuntimeError("kill(): process already completed");
            }
            
            int signal = SIGTERM;
            if (!args.empty()) {
                signal = static_cast<int>(args[0].asNumber());
            }
            
            if (kill(procHandle->pid, signal) == -1) {
                return HavelRuntimeError("kill(): failed to send signal");
            }
            
            return HavelValue(true);
        });
        
        // stdout property - read stdout
        (*procObj)["stdout"] = BuiltinFunction([procHandle](const std::vector<HavelValue>&) -> HavelResult {
            char buffer[4096];
            ssize_t bytesRead;
            
            while ((bytesRead = read(procHandle->stdoutPipe, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesRead] = '\0';
                procHandle->stdoutBuffer += buffer;
            }
            
            return HavelValue(procHandle->stdoutBuffer);
        });
        
        // stderr property - read stderr
        (*procObj)["stderr"] = BuiltinFunction([procHandle](const std::vector<HavelValue>&) -> HavelResult {
            char buffer[4096];
            ssize_t bytesRead;
            
            while ((bytesRead = read(procHandle->stderrPipe, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytesRead] = '\0';
                procHandle->stderrBuffer += buffer;
            }
            
            return HavelValue(procHandle->stderrBuffer);
        });
        
        // exitCode property
        (*procObj)["exitCode"] = BuiltinFunction([procHandle](const std::vector<HavelValue>&) -> HavelResult {
            if (!procHandle->completed) {
                return HavelValue(nullptr);  // Not completed yet
            }
            return HavelValue(static_cast<double>(procHandle->exitCode));
        });
        
        return HavelValue(procObj);
    });
    
    // Register process namespace
    env->Define("process", HavelValue(processObj));
    
    // Also define spawn() at global level for convenience
    // Get spawn from the process object we just created
    auto spawnFunc = (*processObj)["spawn"];
    env->Define("spawn", spawnFunc);
}

} // namespace havel::stdlib
