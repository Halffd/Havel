#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <chrono>

namespace havel {

/**
 * @brief Class for managing system processes
 */
class ProcessManager {
public:
    /**
     * @brief Process information structure
     */
    struct ProcessInfo {
        int32_t pid;                    // Process ID
        int32_t ppid;                   // Parent Process ID
        std::string name;               // Process name
        std::string command;            // Full command line
        std::string user;               // Username of process owner
        double cpu_usage;               // CPU usage percentage
        uint64_t memory_usage;          // Memory usage in KB
        std::chrono::system_clock::time_point start_time;  // Process start time
    };

    /**
     * @brief Get information about all running processes
     * @return Vector of ProcessInfo objects
     */
    static std::vector<ProcessInfo> listProcesses();

    /**
     * @brief Find processes by name
     * @param name Process name or part of the name to search for
     * @return Vector of matching ProcessInfo objects
     */
    static std::vector<ProcessInfo> findProcesses(const std::string& name);

    /**
     * @brief Get process information by PID
     * @param pid Process ID
     * @return ProcessInfo if found, std::nullopt otherwise
     */
    static std::optional<ProcessInfo> getProcessInfo(int32_t pid);

    /**
     * @brief Kill a process by PID
     * @param pid Process ID
     * @param force Use SIGKILL if true, SIGTERM otherwise
     * @return true if successful, false otherwise
     */
    static bool killProcess(int32_t pid, bool force = false);

    /**
     * @brief Send a signal to a process
     * @param pid Process ID
     * @param signal Signal number (e.g., SIGTERM, SIGKILL)
     * @return true if successful, false otherwise
     */
    static bool sendSignal(int32_t pid, int signal);

    /**
     * @brief Get process CPU usage percentage
     * @param pid Process ID
     * @return CPU usage percentage or -1 on error
     */
    static double getCpuUsage(int32_t pid);

    /**
     * @brief Get process memory usage
     * @param pid Process ID
     * @return Memory usage in KB or 0 on error
     */
    static uint64_t getMemoryUsage(int32_t pid);

    /**
     * @brief Check if a process is running
     * @param pid Process ID
     * @return true if process exists and is running, false otherwise
     */
    static bool isProcessRunning(int32_t pid);

    /**
     * @brief Get the current process ID
     * @return Current process ID
     */
    static int32_t getCurrentPid();

    /**
     * @brief Get the parent process ID of the current process
     * @return Parent process ID
     */
    static int32_t getParentPid();

    /**
     * @brief Get the process name by PID
     * @param pid Process ID
     * @return Process name or empty string if not found
     */
    static std::string getProcessName(int32_t pid);

    /**
     * @brief Get the username of the process owner
     * @param pid Process ID
     * @return Username or empty string if not found
     */
    static std::string getProcessUser(int32_t pid);

    /**
     * @brief Get the command line of a process
     * @param pid Process ID
     * @return Command line or empty string if not found
     */
    static std::string getProcessCommand(int32_t pid);

    /**
     * @brief Get the start time of a process
     * @param pid Process ID
     * @return Start time or time_point::min() if not found
     */
    static std::chrono::system_clock::time_point getProcessStartTime(int32_t pid);

    /**
     * @brief Get the number of threads in a process
     * @param pid Process ID
     * @return Number of threads or -1 on error
     */
    static int32_t getThreadCount(int32_t pid);

    /**
     * @brief Get the working directory of a process
     * @param pid Process ID
     * @return Working directory path or empty string if not found
     */
    static std::string getProcessWorkingDirectory(int32_t pid);

    /**
     * @brief Get the executable path of a process
     * @param pid Process ID
     * @return Executable path or empty string if not found
     */
    static std::string getProcessExecutablePath(int32_t pid);

private:
    // Private constructor to prevent instantiation
    ProcessManager() = delete;
    ~ProcessManager() = delete;

    // Disable copy and move
    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;
    ProcessManager(ProcessManager&&) = delete;
    ProcessManager& operator=(ProcessManager&&) = delete;
};

} // namespace havel
