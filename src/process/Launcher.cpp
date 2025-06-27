#include "Launcher.hpp"
#include <sstream>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#pragma comment(lib, "shell32.lib")
#endif

namespace havel {

ProcessResult Launcher::run(const std::string& executable, 
                          const std::vector<std::string>& args,
                          const LaunchParams& params) {
    if (executable.empty()) {
        return {-1, -1, false, "Empty executable path"};
    }
    
    auto start = std::chrono::steady_clock::now();
    ProcessResult result;
    
    try {
        if (params.method == Method::Shell) {
            result = executeShell(buildCommandLine(executable, args), params);
        } else {
#ifdef _WIN32
            result = executeWindows(executable, args, params);
#else
            result = executeUnix(executable, args, params);
#endif
        }
    } catch (const std::exception& e) {
        result.error = e.what();
        result.success = false;
    }
    
    auto end = std::chrono::steady_clock::now();
    result.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    return result;
}

ProcessResult Launcher::run(const std::string& commandLine, const LaunchParams& params) {
    auto args = parseCommandLine(commandLine);
    if (args.empty()) {
        return {-1, -1, false, "Invalid command line"};
    }
    
    std::string executable = args[0];
    args.erase(args.begin());
    
    return run(executable, args, params);
}

ProcessResult Launcher::runSync(const std::string& cmd) {
    return run(cmd, {Method::Sync});
}

ProcessResult Launcher::runAsync(const std::string& cmd) {
    return run(cmd, {Method::Async});
}

ProcessResult Launcher::runHidden(const std::string& cmd) {
    return run(cmd, {Method::Sync, WindowState::Hidden});
}

ProcessResult Launcher::runShell(const std::string& cmd) {
    return run(cmd, {Method::Shell});
}

ProcessResult Launcher::terminal(const std::string& command, const std::string& terminalType) {
#ifdef _WIN32
    std::string term = terminalType.empty() ? "cmd" : terminalType;
    std::string fullCmd;
    
    if (term == "powershell" || term == "pwsh") {
        fullCmd = "powershell.exe";
        return run(fullCmd, {"-Command", command}, {Method::Sync});
    } else {
        fullCmd = "cmd.exe";
        return run(fullCmd, {"/c", command}, {Method::Sync});
    }
#else
    std::string term = terminalType;
    if (term.empty()) {
        // Auto-detect available terminal
        const std::vector<std::string> terminals = {
            "gnome-terminal", "konsole", "xfce4-terminal", "xterm", "lxterminal"
        };
        
        for (const auto& t : terminals) {
            if (system(("which " + t + " > /dev/null 2>&1").c_str()) == 0) {
                term = t;
                break;
            }
        }
        
        if (term.empty()) {
            return {-1, -1, false, "No terminal found"};
        }
    }
    
    if (term == "gnome-terminal") {
        return run(term, {"--", "sh", "-c", command});
    } else if (term == "konsole") {
        return run(term, {"-e", "sh", "-c", command});
    } else {
        return run(term, {"-e", "sh", "-c", command});
    }
#endif
}

#ifdef _WIN32
ProcessResult Launcher::executeWindows(const std::string& executable,
                                     const std::vector<std::string>& args,
                                     const LaunchParams& params) {
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    
    applyWindowsStartupInfo(si, params.windowState);
    DWORD creationFlags = getWindowsCreationFlags(params);
    
    std::string cmdLine = buildCommandLine(executable, args);
    std::string workDir = params.workingDir.empty() ? std::string() : params.workingDir;
    
    BOOL success = CreateProcessA(
        executable.c_str(),
        cmdLine.empty() ? nullptr : &cmdLine[0],
        nullptr, nullptr,
        params.inheritHandles,
        creationFlags,
        nullptr,
        workDir.empty() ? nullptr : workDir.c_str(),
        &si, &pi
    );
    
    if (!success) {
        DWORD error = GetLastError();
        return {-1, -1, false, "CreateProcess failed: " + std::to_string(error)};
    }
    
    ProcessResult result;
    result.pid = pi.dwProcessId;
    result.success = true;
    
    // Set priority if specified
    if (params.priority != Priority::Normal) {
        SetPriorityClass(pi.hProcess, getWindowsPriorityClass(params.priority));
    }
    
    if (params.method == Method::Sync) {
        if (params.timeoutMs > 0) {
            DWORD waitResult = WaitForSingleObject(pi.hProcess, params.timeoutMs);
            if (waitResult == WAIT_TIMEOUT) {
                TerminateProcess(pi.hProcess, -1);
                result.error = "Process timed out";
                result.success = false;
            }
        } else {
            WaitForSingleObject(pi.hProcess, INFINITE);
        }
        
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        result.exitCode = exitCode;
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return result;
}

void Launcher::applyWindowsStartupInfo(STARTUPINFOA& si, WindowState state) {
    si.dwFlags = STARTF_USESHOWWINDOW;
    
    switch (state) {
        case WindowState::Hidden:
            si.wShowWindow = SW_HIDE;
            break;
        case WindowState::Minimized:
            si.wShowWindow = SW_SHOWMINIMIZED;
            break;
        case WindowState::Maximized:
            si.wShowWindow = SW_SHOWMAXIMIZED;
            break;
        case WindowState::Unfocused:
            si.wShowWindow = SW_SHOWNOACTIVATE;
            break;
        default:
            si.wShowWindow = SW_SHOWNORMAL;
            break;
    }
}

DWORD Launcher::getWindowsCreationFlags(const LaunchParams& params) {
    DWORD flags = 0;
    
    if (params.windowState == WindowState::Hidden) {
        flags |= CREATE_NO_WINDOW;
    } else {
        flags |= CREATE_NEW_CONSOLE;
    }
    
    return flags;
}

DWORD Launcher::getWindowsPriorityClass(Priority priority) {
    switch (priority) {
        case Priority::Idle: return IDLE_PRIORITY_CLASS;
        case Priority::BelowNormal: return BELOW_NORMAL_PRIORITY_CLASS;
        case Priority::Normal: return NORMAL_PRIORITY_CLASS;
        case Priority::AboveNormal: return ABOVE_NORMAL_PRIORITY_CLASS;
        case Priority::High: return HIGH_PRIORITY_CLASS;
        case Priority::Realtime: return REALTIME_PRIORITY_CLASS;
        default: return NORMAL_PRIORITY_CLASS;
    }
}

#else // Unix implementation

ProcessResult Launcher::executeUnix(const std::string& executable,
                                  const std::vector<std::string>& args,
                                  const LaunchParams& params) {
    pid_t pid = fork();
    
    if (pid == -1) {
        return {-1, -1, false, "Fork failed"};
    }
    
    if (pid == 0) {
        // Child process
        setupUnixEnvironment(params);
        
        // Set priority
        if (params.priority != Priority::Normal) {
            setpriority(PRIO_PROCESS, 0, getUnixNiceValue(params.priority));
        }
        
        // Change working directory
        if (!params.workingDir.empty()) {
            chdir(params.workingDir.c_str());
        }
        
        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(executable.c_str(), argv.data());
        _exit(127); // execvp failed
    }
    
    // Parent process
    ProcessResult result;
    result.pid = pid;
    result.success = true;
    
    if (params.method == Method::Sync) {
        int status;
        pid_t waitResult;
        
        if (params.timeoutMs > 0) {
            // Implement timeout using signals (simplified)
            waitResult = waitpid(pid, &status, WNOHANG);
            if (waitResult == 0) {
                // Still running, kill after timeout
                std::this_thread::sleep_for(std::chrono::milliseconds(params.timeoutMs));
                kill(pid, SIGTERM);
                waitpid(pid, &status, 0);
                result.error = "Process timed out";
            }
        } else {
            waitResult = waitpid(pid, &status, 0);
        }
        
        if (waitResult > 0) {
            if (WIFEXITED(status)) {
                result.exitCode = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exitCode = -WTERMSIG(status);
            }
        }
    }
    
    return result;
}

int Launcher::getUnixNiceValue(Priority priority) {
    switch (priority) {
        case Priority::Idle: return 19;
        case Priority::BelowNormal: return 10;
        case Priority::Normal: return 0;
        case Priority::AboveNormal: return -10;
        case Priority::High: return -20;
        case Priority::Realtime: return -20; // Best we can do without real-time scheduling
        default: return 0;
    }
}

void Launcher::setupUnixEnvironment(const LaunchParams& params) {
    for (const auto& env : params.environment) {
        size_t pos = env.find('=');
        if (pos != std::string::npos) {
            std::string name = env.substr(0, pos);
            std::string value = env.substr(pos + 1);
            setenv(name.c_str(), value.c_str(), 1);
        }
    }
}

#endif

ProcessResult Launcher::executeShell(const std::string& command, const LaunchParams& params) {
#ifdef _WIN32
    if (params.method == Method::Async) {
        system(command.c_str());
        return {0, 0, true, ""};
    }
    
    int exitCode = system(command.c_str());
    return {0, exitCode, exitCode != -1, exitCode == -1 ? "System call failed" : ""};
#else
    if (params.method == Method::Async) {
        system(command.c_str());
        return {0, 0, true, ""};
    }
    
    int status = system(command.c_str());
    if (status == -1) {
        return {0, -1, false, "System call failed"};
    }
    
    return {0, WEXITSTATUS(status), true, ""};
#endif
}

bool Launcher::kill(int64_t pid, bool force) {
    if (pid <= 0) return false;
    
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!hProcess) return false;
    
    BOOL result = TerminateProcess(hProcess, force ? 1 : 0);
    CloseHandle(hProcess);
    return result != FALSE;
#else
    int signal = force ? SIGKILL : SIGTERM;
    return ::kill(static_cast<pid_t>(pid), signal) == 0;
#endif
}

bool Launcher::isRunning(int64_t pid) {
    if (pid <= 0) return false;
    
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!hProcess) return false;
    
    DWORD exitCode;
    BOOL result = GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);
    
    return result && exitCode == STILL_ACTIVE;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

bool Launcher::setPriority(int64_t pid, Priority priority) {
    if (pid <= 0) return false;
    
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!hProcess) return false;
    
    BOOL result = SetPriorityClass(hProcess, getWindowsPriorityClass(priority));
    CloseHandle(hProcess);
    return result != FALSE;
#else
    return setpriority(PRIO_PROCESS, static_cast<pid_t>(pid), getUnixNiceValue(priority)) == 0;
#endif
}

std::vector<int64_t> Launcher::findByName(const std::string& name) {
    std::vector<int64_t> pids;
    
#ifdef _WIN32
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return pids;
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (Process32First(hSnapshot, &pe32)) {
        do {
            std::string processName = pe32.szExeFile;
            if (processName.find(name) != std::string::npos) {
                pids.push_back(pe32.th32ProcessID);
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
#else
    // Simple implementation using pgrep
    std::string command = "pgrep " + escapeArgument(name);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return pids;
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        int64_t pid = std::stoll(buffer);
        if (pid > 0) pids.push_back(pid);
    }
    
    pclose(pipe);
#endif
    
    return pids;
}

std::string Launcher::getLastError() {
#ifdef _WIN32
    DWORD error = GetLastError();
    if (error == 0) return "";
    
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer, 0, nullptr);
    
    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
#else
    return std::string(strerror(errno));
#endif
}

std::string Launcher::escapeArgument(const std::string& arg) {
    if (arg.find(' ') == std::string::npos && 
        arg.find('\t') == std::string::npos && 
        arg.find('"') == std::string::npos) {
        return arg;
    }
    
#ifdef _WIN32
    std::string escaped = "\"";
    for (size_t i = 0; i < arg.length(); ++i) {
        if (arg[i] == '"') {
            escaped += "\\\"";
        } else if (arg[i] == '\\' && i + 1 < arg.length() && arg[i + 1] == '"') {
            escaped += "\\\\";
        } else {
            escaped += arg[i];
        }
    }
    escaped += "\"";
    return escaped;
#else
    std::string escaped = "'";
    for (char c : arg) {
        if (c == '\'') {
            escaped += "'\"'\"'";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
#endif
}

std::vector<std::string> Launcher::parseCommandLine(const std::string& cmdLine) {
    std::vector<std::string> args;
    std::string current;
    bool inQuotes = false;
    bool escapeNext = false;
    
    for (size_t i = 0; i < cmdLine.length(); ++i) {
        char c = cmdLine[i];
        
        if (escapeNext) {
            current += c;
            escapeNext = false;
            continue;
        }
        
        switch (c) {
            case '\\':
                escapeNext = true;
                break;
            case '"':
                inQuotes = !inQuotes;
                break;
            case ' ':
            case '\t':
                if (inQuotes) {
                    current += c;
                } else if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
                break;
            default:
                current += c;
                break;
        }
    }
    
    if (!current.empty()) {
        args.push_back(current);
    }
    
    return args;
}

std::string Launcher::buildCommandLine(const std::string& executable, 
                                     const std::vector<std::string>& args) {
    std::string cmdLine = escapeArgument(executable);
    
    for (const auto& arg : args) {
        cmdLine += " " + escapeArgument(arg);
    }
    
    return cmdLine;
}

} // namespace havel