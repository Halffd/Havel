#include "CrashHandler.hpp"
#include "Logger.hpp"
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <atomic>
#include <thread>
#include <cstdarg>
#include <iostream>

namespace havel {

static CrashHandlerConfig g_config;
static std::function<void(const CrashReport&)> g_crash_callback;
static std::atomic<bool> g_stop_requested{false};

void initCrashHandler(const CrashHandlerConfig& config) {
    g_config = config;
    
    if (g_config.generate_core_dump) {
        struct rlimit core_limit;
        core_limit.rlim_cur = RLIM_INFINITY;
        core_limit.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_CORE, &core_limit);
    }
}

std::string getTimestampISO8601() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << now_ms.count();
    return ss.str();
}

std::string captureStackTrace(int skip_frames) {
    constexpr int MAX_FRAMES = 64;
    void* callstack[MAX_FRAMES];
    int frames = backtrace(callstack, MAX_FRAMES);
    char** symbols = backtrace_symbols(callstack, frames);
    
    if (!symbols) {
        return "Failed to capture stack trace";
    }
    
    std::ostringstream ss;
    ss << "Stack trace:\n";
    for (int i = skip_frames + 1; i < frames; ++i) {
        ss << "  #" << (i - skip_frames - 1) << ": " << symbols[i] << "\n";
    }
    free(symbols);
    return ss.str();
}

std::string generateCrashReport(const CrashReport& report) {
    std::string filename = g_config.crash_report_dir + "/" + 
                           g_config.app_name + "_crash_" + 
                           report.timestamp + ".txt";
    
    std::ofstream file(filename);
    if (!file) {
        error("Failed to create crash report file: {}", filename);
        return "";
    }
    
    file << "=== CRASH REPORT ===" << "\n";
    file << "Application: " << g_config.app_name << "\n";
    file << "Timestamp: " << report.timestamp << "\n";
    file << "Signal: " << report.signal << " (" << strsignal(report.signal) << ")\n";
    file << "Thread ID: " << report.thread_id << "\n";
    file << "\n";
    file << "Message:\n" << report.message << "\n";
    file << "\n";
    file << report.stack_trace << "\n";
    file << "=== END REPORT ===" << "\n";
    
    file.close();
    info("Crash report written to: {}", filename);
    return filename;
}

void panic(const std::string& message, bool generate_core_dump) {
    CrashReport report;
    report.message = message;
    report.stack_trace = captureStackTrace(2);
    report.timestamp = getTimestampISO8601();
    report.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    report.signal = SIGABRT;
    
    if (g_crash_callback) {
        g_crash_callback(report);
    }
    
    if (g_config.log_to_stderr) {
        std::cerr << "\n=== PANIC ===" << "\n";
        std::cerr << "Message: " << message << "\n";
        std::cerr << report.stack_trace << "\n";
    }
    
    if (g_config.generate_crash_report) {
        generateCrashReport(report);
    }
    
    std::cerr << "\nExiting due to panic.\n";
    
    if (generate_core_dump && g_config.generate_core_dump) {
        std::cerr << "Generating core dump...\n";
        std::cerr.flush();
        abort();
    }
    
    _exit(134);
}

void panicf(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    panic(std::string(buffer), true);
}

void setCrashCallback(std::function<void(const CrashReport&)> callback) {
    g_crash_callback = std::move(callback);
}

bool requestStopAll() {
    bool was_running = g_stop_requested.exchange(true);
    if (!was_running) {
        info("Stop requested - signaling all threads/loops to terminate");
    }
    return !was_running;
}

bool isStopRequested() {
    return g_stop_requested.load();
}

void clearStopRequest() {
    g_stop_requested.store(false);
}

} // namespace havel
