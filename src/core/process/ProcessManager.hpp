#pragma once
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace havel {

class ProcessManager {
public:
    enum class ProcessState {
        RUNNING,
        SLEEPING,
        ZOMBIE,
        STOPPED,
        NOT_FOUND,
        NO_PERMISSION
    };

    struct ProcessInfo {
        int32_t pid = 0;
        int32_t ppid = 0;
        std::string name;
        std::string command;
        std::string user;
        double cpu_usage = 0.0;
        uint64_t memory_usage = 0;
        std::chrono::system_clock::time_point start_time;
    };

private:
    struct CpuSample {
        uint64_t total_time = 0;
        std::chrono::steady_clock::time_point timestamp;
    };
    static std::unordered_map<int32_t, CpuSample> cpu_samples_;

    // Helper methods
    static uint64_t readProcessCpuTime(int32_t pid);
    static std::vector<std::string> splitStatLine(const std::string& line);
    static std::string readFile(const std::string& path);
    static std::string readFirstLine(const std::string& path);
    static std::string getUserName(uid_t uid);
    static bool parseProcStat(int32_t pid, ProcessInfo& info);

public:
    // Core functionality
    static bool isProcessAlive(pid_t pid);
    static bool sendSignal(pid_t pid, int signal);
    static ProcessState getProcessState(pid_t pid);
    static bool terminateProcess(pid_t pid, int timeout_ms = 5000);
    static bool isZombie(pid_t pid);
    static bool getExitStatus(pid_t pid, int& exit_status);

    // Process enumeration
    static std::vector<ProcessInfo> listProcesses();
    static std::vector<ProcessInfo> findProcesses(const std::string& name);
    static std::optional<ProcessInfo> getProcessInfo(int32_t pid);

    // Process metrics
    static double getCpuUsage(int32_t pid);
    static uint64_t getMemoryUsage(int32_t pid);
    static int32_t getThreadCount(int32_t pid);

    // Process properties
    static std::string getProcessName(int32_t pid);
    static std::string getProcessUser(int32_t pid);
    static std::string getProcessCommand(int32_t pid);
    static std::string getProcessWorkingDirectory(int32_t pid);
    static std::string getProcessExecutablePath(int32_t pid);
    static std::chrono::system_clock::time_point getProcessStartTime(int32_t pid);

    // Utility
    static int32_t getCurrentPid() { return getpid(); }
    static int32_t getParentPid() { return getppid(); }
    static bool isProcessRunning(int32_t pid) { return isProcessAlive(pid); }
};

} // namespace havel