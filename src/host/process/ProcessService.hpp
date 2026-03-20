/*
 * ProcessService.hpp
 *
 * Pure C++ process service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for process operations.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace havel { class ProcessManager; }  // Forward declaration

namespace havel::host {

/**
 * ProcessInfo - Information about a process
 */
struct ProcessInfo {
    int32_t pid = 0;
    int32_t ppid = 0;
    std::string name;
    std::string command;
    std::string user;
    double cpu_usage = 0.0;
    uint64_t memory_usage = 0;
};

/**
 * ProcessService - Pure process business logic
 *
 * Provides system-level process operations without any language runtime coupling.
 * All methods return simple C++ types (bool, int, string, vector, etc.)
 */
class ProcessService {
public:
    ProcessService() = default;
    ~ProcessService() = default;

    // =========================================================================
    // Process queries
    // =========================================================================

    /// Get list of all process PIDs
    /// @return vector of PIDs
    static std::vector<int32_t> listProcesses();

    /// Find processes by name
    /// @param name Process name or substring
    /// @return vector of PIDs
    static std::vector<int32_t> findProcesses(const std::string& name);

    /// Get process info
    /// @param pid Process ID
    /// @return ProcessInfo (empty if not found)
    static std::optional<ProcessInfo> getProcessInfo(int32_t pid);

    /// Get process name
    /// @param pid Process ID
    /// @return process name (empty if not found)
    static std::string getProcessName(int32_t pid);

    /// Check if process exists
    /// @param name Process name or substring
    /// @return true if process is running
    static bool processExists(const std::string& name);

    // =========================================================================
    // Process control
    // =========================================================================

    /// Check if process is alive
    /// @param pid Process ID
    /// @return true if process is running
    static bool isProcessAlive(int32_t pid);

    /// Send signal to process
    /// @param pid Process ID
    /// @param signal Signal number
    /// @return true on success
    static bool sendSignal(int32_t pid, int signal);

    /// Terminate process gracefully
    /// @param pid Process ID
    /// @param timeout_ms Timeout in milliseconds
    /// @return true if process terminated
    static bool terminateProcess(int32_t pid, int timeout_ms = 5000);

    // =========================================================================
    // Process metrics
    // =========================================================================

    /// Get CPU usage
    /// @param pid Process ID
    /// @return CPU usage percentage
    static double getCpuUsage(int32_t pid);

    /// Get memory usage
    /// @param pid Process ID
    /// @return memory usage in bytes
    static uint64_t getMemoryUsage(int32_t pid);

    /// Get thread count
    /// @param pid Process ID
    /// @return number of threads
    static int32_t getThreadCount(int32_t pid);

    // =========================================================================
    // Process properties
    // =========================================================================

    /// Get process command
    /// @param pid Process ID
    /// @return command line
    static std::string getProcessCommand(int32_t pid);

    /// Get process user
    /// @param pid Process ID
    /// @return user name
    static std::string getProcessUser(int32_t pid);

    /// Get process working directory
    /// @param pid Process ID
    /// @return working directory path
    static std::string getProcessWorkingDirectory(int32_t pid);

    /// Get process executable path
    /// @param pid Process ID
    /// @return executable path
    static std::string getProcessExecutablePath(int32_t pid);

    /// Get current process PID
    /// @return current PID
    static int32_t getCurrentPid();

    /// Get parent process PID
    /// @return parent PID
    static int32_t getParentPid();
};

} // namespace havel::host
