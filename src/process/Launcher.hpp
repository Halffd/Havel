#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <future>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#endif

namespace havel {

enum class Method {
    Sync,           // Wait for completion
    Async,          // Fire and forget
    Detached,       // Return immediately, don't track
    Shell,          // Use shell (ShellExecute/system)
    Direct          // Direct execution (CreateProcess/execv)
};

enum class WindowState {
    Normal,
    Hidden,
    Minimized,
    Maximized,
    Unfocused
};

enum class Priority {
    Idle = 0,
    BelowNormal = 1,
    Normal = 2,
    AboveNormal = 3,
    High = 4,
    Realtime = 5
};

struct LaunchParams {
    Method method = Method::Sync;
    WindowState windowState = WindowState::Normal;
    Priority priority = Priority::Normal;
    std::string workingDir;
    std::vector<std::string> environment;
    bool inheritHandles = false;
    uint32_t timeoutMs = 0;
};

struct ProcessResult {
    int64_t pid = -1;
    int32_t exitCode = -1;
    bool success = false;
    std::string error;
    uint32_t executionTimeMs = 0;
};

class Launcher {
public:
    // Core launching
    static ProcessResult run(const std::string& executable, 
                           const std::vector<std::string>& args = {},
                           const LaunchParams& params = {});
    
    static ProcessResult run(const std::string& commandLine,
                           const LaunchParams& params = {});
    
    // Convenience methods
    static ProcessResult runSync(const std::string& cmd);
    static ProcessResult runAsync(const std::string& cmd);
    static ProcessResult runHidden(const std::string& cmd);
    static ProcessResult runShell(const std::string& cmd);
    
    // Terminal operations
    static ProcessResult terminal(const std::string& command,
                                const std::string& terminalType = "");
    
    // Process management
    static bool kill(int64_t pid, bool force = false);
    static bool isRunning(int64_t pid);
    static bool setPriority(int64_t pid, Priority priority);
    static std::vector<int64_t> findByName(const std::string& name);
    
    // Utility
    static std::string getLastError();
    static std::string escapeArgument(const std::string& arg);
    static std::vector<std::string> parseCommandLine(const std::string& cmdLine);

private:
    static ProcessResult executeWindows(const std::string& executable,
                                      const std::vector<std::string>& args,
                                      const LaunchParams& params);
    
    static ProcessResult executeUnix(const std::string& executable,
                                   const std::vector<std::string>& args,
                                   const LaunchParams& params);
    
    static ProcessResult executeShell(const std::string& command,
                                    const LaunchParams& params);
    
    static std::string buildCommandLine(const std::string& executable,
                                      const std::vector<std::string>& args);
    #ifdef _WIN32
    static void applyWindowsStartupInfo(STARTUPINFOA& si, WindowState state);
    static DWORD getWindowsCreationFlags(const LaunchParams& params);
    static DWORD getWindowsPriorityClass(Priority priority);
    #endif
    
    static int getUnixNiceValue(Priority priority);
    static void setupUnixEnvironment(const LaunchParams& params);
};

} // namespace Process