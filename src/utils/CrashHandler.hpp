#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace havel {

/**
 * CrashHandler - Crash report generation and core dump handling
 *
 * Provides:
 * - panic() function for deliberate crashes with logs/core dumps
 * - Stack trace capture
 * - Crash report generation
 * - Core dump creation (via SIGQUIT handler)
 */

/**
 * CrashReport - Information captured during a crash
 */
struct CrashReport {
    std::string message;
    std::string stack_trace;
    std::string timestamp;
    std::string version;
    int signal = 0;
    uint64_t thread_id = 0;
};

/**
 * CrashHandler configuration
 */
struct CrashHandlerConfig {
    bool generate_core_dump = true;      // Create core dump on crash
    bool generate_crash_report = true;   // Write crash report file
    bool log_to_stderr = true;           // Print crash info to stderr
    std::string crash_report_dir = ".";  // Directory for crash reports
    std::string app_name = "havel";      // Application name for reports
};

/**
 * Initialize crash handler
 * Should be called early in application startup
 */
void initCrashHandler(const CrashHandlerConfig& config = {});

/**
 * Panic - Deliberately crash with a message
 * Generates crash report, stack trace, and optionally core dump
 *
 * @param message Panic message
 * @param generate_core_dump If true, triggers SIGABRT for core dump
 */
[[noreturn]] void panic(const std::string& message, bool generate_core_dump = true);

/**
 * Panic with formatted message
 */
[[noreturn]] void panicf(const char* format, ...);

/**
 * Capture current stack trace
 * @param skip_frames Number of frames to skip from the top
 * @return Stack trace as formatted string
 */
std::string captureStackTrace(int skip_frames = 0);

/**
 * Generate crash report file
 * @param report Crash report data
 * @return Path to generated report file
 */
std::string generateCrashReport(const CrashReport& report);

/**
 * Get current timestamp as ISO 8601 string
 */
std::string getTimestampISO8601();

/**
 * Set custom crash callback
 * Called before generating crash report/core dump
 */
void setCrashCallback(std::function<void(const CrashReport&)> callback);

/**
 * Request stop all threads and loops (called on SIGINT)
 * Returns true if there were threads/loops to stop
 */
bool requestStopAll();

/**
 * Check if a stop was requested
 */
bool isStopRequested();

/**
 * Clear stop request flag
 */
void clearStopRequest();

} // namespace havel
