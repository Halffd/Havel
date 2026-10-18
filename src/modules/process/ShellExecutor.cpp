/*
 * ShellExecutor.cpp
 *
 * Shell command execution service.
 * Separates shell execution logic from evaluator.
 */
#include "ShellExecutor.hpp"
#include "core/process/Launcher.hpp"
#include "utils/Logger.hpp"
#include "havel_platform.h"
#include "havel-lang/compiler/platform/file.hpp"
#include "havel-lang/compiler/platform/platform_win.hpp"
#if HAVEL_PLATFORM_POSIX
#include <unistd.h>
#include <sys/wait.h>
#endif
#include <cstring>
#include <cerrno>
#include <vector>
#include <string>

namespace havel {

#if HAVEL_PLATFORM_WINDOWS
namespace {
// Win32 command-line quoting, matching what CommandLineToArgvW parses back.
// An argument is quoted when it is empty or contains whitespace or a double
// quote; a run of backslashes is doubled when it precedes a quote or the
// closing quote, and otherwise copied through.
//
// The conversion to UTF-16 happens first and the quoting is done on the wide
// string, so a multi-byte UTF-8 argument is never split mid-character.
std::wstring quoteForWindows(const std::string& arg) {
  const std::wstring w = platform::utf8_to_wide(arg);
  if (w.find_first_of(L" \t\n\v\f\"") == std::wstring::npos) return w;

  std::wstring out;
  out.push_back(L'"');
  size_t backslashes = 0;
  for (const wchar_t c : w) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
    } else {
      out.append(backslashes, L'\\');
    }
    out.push_back(c);
    backslashes = 0;
  }
  // Trailing backslashes precede the closing quote, so they must be doubled.
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

std::string lastErrorText(const char* what) {
  return std::string(what) + " failed (Win32 error " +
         std::to_string(static_cast<unsigned long>(::GetLastError())) + ")";
}
}  // namespace
#endif  // HAVEL_PLATFORM_WINDOWS

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
    shellResult.out = result.out;
    shellResult.err = result.err;
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
    
    #if HAVEL_PLATFORM_POSIX
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

            // Close remaining inherited fds (grabbed evdev devices, X11
            // socket, eventfds). Children holding the EVIOCGRAB devices open
            // caused "Device or resource busy" on restart and stale X11
            // sessions. stdio (dup2'd above) stays open.
            {
                int maxFd = static_cast<int>(sysconf(_SC_OPEN_MAX));
                for (int fd = 3; fd < (maxFd > 1024 ? 1024 : maxFd); ++fd) {
                    close(fd);
                }
            }

            // Own process group: group-directed kills aimed at the pipeline
            // (or at havel's group) must not cross over.
            setpgid(0, 0);
            
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
        result.out.append(buffer, bytesRead);
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
#else  // HAVEL_PLATFORM_POSIX
    // Windows pipeline: CreatePipe plus one CreateProcessW per command, with
    // the three std handles wired through STARTF_USESTDHANDLES. That is the
    // direct counterpart of pipe()/fork()/dup2()/execvp()/waitpid() above,
    // and it keeps the same semantics: only the last command's stdout and
    // stderr are captured, every earlier command's stderr stays on the
    // process's own stderr, and the exit code reported is the last one's.
    const int numCommands = static_cast<int>(commands.size());

    // Command i reads stdin from readHandle[i - 1] and writes stdout to
    // writeHandle[i]; the last command writes both stdout and stderr to the
    // capture pipe.
    std::vector<HANDLE> readHandle(numCommands, INVALID_HANDLE_VALUE);
    std::vector<HANDLE> writeHandle(numCommands, INVALID_HANDLE_VALUE);
    HANDLE outRead = INVALID_HANDLE_VALUE;
    HANDLE outWrite = INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    auto closeAll = [&]() {
        if (outRead != INVALID_HANDLE_VALUE) ::CloseHandle(outRead);
        if (outWrite != INVALID_HANDLE_VALUE) ::CloseHandle(outWrite);
        for (HANDLE h : readHandle) if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
        for (HANDLE h : writeHandle) if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
        outRead = outWrite = INVALID_HANDLE_VALUE;
        for (HANDLE& h : readHandle) h = INVALID_HANDLE_VALUE;
        for (HANDLE& h : writeHandle) h = INVALID_HANDLE_VALUE;
    };

    auto fail = [&](const char* what) {
        result.success = false;
        result.error = lastErrorText(what);
        result.exitCode = 1;
        closeAll();
    };

    if (!::CreatePipe(&outRead, &outWrite, &inheritable, 0)) { fail("CreatePipe"); return result; }
    // Nothing but this process reads the capture pipe. A child holding its read
    // end open would keep the write end from ever signalling EOF below.
    ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    for (int i = 0; i < numCommands - 1; ++i) {
        if (!::CreatePipe(&readHandle[i], &writeHandle[i], &inheritable, 0)) {
            fail("CreatePipe");
            return result;
        }
        // Only the command that consumes this pipe needs the read end.
        ::SetHandleInformation(readHandle[i], HANDLE_FLAG_INHERIT, 0);
    }

    // Inheritance is toggled per child rather than set once for every pipe.
    // Leaving all ends inheritable lets an early command hold open the write
    // end of a later pipe, so that pipe never reaches EOF and the read loop
    // below blocks until every child exits - which a long pipeline can
    // deadlock on.
    auto setInheritable = [](HANDLE h, bool on) {
        if (h == INVALID_HANDLE_VALUE || h == nullptr) return;
        ::SetHandleInformation(h, HANDLE_FLAG_INHERIT,
                               on ? HANDLE_FLAG_INHERIT : 0);
    };

    std::vector<HANDLE> processes;
    std::vector<HANDLE> threads;

    for (int i = 0; i < numCommands; ++i) {
        const auto args = parseCommandArgs(commands[i]);
        if (args.empty()) {
            fail("CreateProcess");
            result.exitCode = 127;
            return result;
        }

        std::wstring commandLine;
        for (size_t k = 0; k < args.size(); ++k) {
            if (k) commandLine.push_back(L' ');
            commandLine += quoteForWindows(args[k]);
        }
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        HANDLE hIn  = (i > 0)               ? readHandle[i - 1] : ::GetStdHandle(STD_INPUT_HANDLE);
        HANDLE hOut = (i < numCommands - 1) ? writeHandle[i]   : outWrite;
        HANDLE hErr = (i < numCommands - 1) ? ::GetStdHandle(STD_ERROR_HANDLE) : outWrite;

        setInheritable(hIn, true);
        setInheritable(hOut, true);
        setInheritable(hErr, true);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = hIn;
        startup.hStdOutput = hOut;
        startup.hStdError = hErr;

        PROCESS_INFORMATION pi{};
        // lpApplicationName is null so CreateProcessW searches PATH, which is
        // what execvp did. CREATE_NO_WINDOW keeps a console flash out of a
        // background service.
        const BOOL created = ::CreateProcessW(nullptr, mutableCommandLine.data(),
                                             nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW, nullptr, nullptr,
                                             &startup, &pi);

        setInheritable(hIn, false);
        setInheritable(hOut, false);
        setInheritable(hErr, false);

        if (!created) {
            const std::string why = lastErrorText("CreateProcess");
            // Reap whatever already started before reporting the failure.
            for (HANDLE p : processes) {
                ::WaitForSingleObject(p, 2000);
                ::CloseHandle(p);
            }
            for (HANDLE t : threads) ::CloseHandle(t);
            fail("CreateProcess");
            result.error = why;
            return result;
        }
        processes.push_back(pi.hProcess);
        threads.push_back(pi.hThread);
    }

    // The parent must drop every pipe end it no longer needs, or the capture
    // pipe never reaches EOF.
    for (HANDLE h : writeHandle) if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    for (HANDLE h : readHandle) if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    ::CloseHandle(outWrite);
    outWrite = INVALID_HANDLE_VALUE;

    char buffer[4096];
    DWORD bytesRead = 0;
    while (::ReadFile(outRead, buffer, sizeof(buffer), &bytesRead, nullptr) &&
           bytesRead > 0) {
        result.out.append(buffer, bytesRead);
    }
    ::CloseHandle(outRead);
    outRead = INVALID_HANDLE_VALUE;

    int lastExitCode = 0;
    for (size_t k = 0; k < processes.size(); ++k) {
        ::WaitForSingleObject(processes[k], INFINITE);
        DWORD code = 0;
        if (::GetExitCodeProcess(processes[k], &code)) {
            lastExitCode = static_cast<int>(code);
        }
        ::CloseHandle(processes[k]);
        ::CloseHandle(threads[k]);
    }

    result.exitCode = lastExitCode;
    result.success = (lastExitCode == 0);

    return result;
#endif  // HAVEL_PLATFORM_POSIX
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
        result.out = launcherResult.out;
        result.err = launcherResult.err;
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
        result.out = launcherResult.out;
        result.err = launcherResult.err;
        result.exitCode = launcherResult.exitCode;
        result.success = launcherResult.success;
        result.error = launcherResult.error;
    }
    
    return result;
}

} // namespace havel
