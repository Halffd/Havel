#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <shared_mutex>
#include <functional>
#include <optional>
#include "WindowManager.hpp"  // Your existing WindowManager

namespace havel {

// Window information structure for monitoring
struct MonitorWindowInfo {
    wID windowId{0};
    std::string title;
    std::string windowClass;
    std::string processName;
    pid_t pid{0};
    std::chrono::steady_clock::time_point lastUpdate;
    bool isValid{false};

    MonitorWindowInfo() = default;
    
    MonitorWindowInfo(const MonitorWindowInfo& other) {
        windowId = other.windowId;
        title = other.title;
        windowClass = other.windowClass;
        processName = other.processName;
        pid = other.pid;
        lastUpdate = other.lastUpdate;
        isValid = other.isValid;
    }

    MonitorWindowInfo& operator=(const MonitorWindowInfo& other) {
        if (this != &other) {
            windowId = other.windowId;
            title = other.title;
            windowClass = other.windowClass;
            processName = other.processName;
            pid = other.pid;
            lastUpdate = other.lastUpdate;
            isValid = other.isValid;
        }
        return *this;
    }
};

inline bool operator==(const MonitorWindowInfo& lhs, const MonitorWindowInfo& rhs) {
    return lhs.windowId == rhs.windowId;
}

class WindowMonitor {
public:
    using WindowCallback = std::function<void(const MonitorWindowInfo&)>;

    explicit WindowMonitor(std::chrono::milliseconds pollInterval = std::chrono::milliseconds(100));
    ~WindowMonitor();

    WindowMonitor(const WindowMonitor&) = delete;
    WindowMonitor& operator=(const WindowMonitor&) = delete;

    void Start();
    void Stop();

    // Get active window info
    std::optional<MonitorWindowInfo> GetActiveWindowInfo() const;

    // Get all tracked windows (if full window tracking is enabled)
    std::unordered_map<wID, MonitorWindowInfo> GetAllWindows() const;

    // Callbacks
    void SetActiveWindowCallback(WindowCallback callback);
    void SetWindowAddedCallback(WindowCallback callback);
    void SetWindowRemovedCallback(WindowCallback callback);

    void SetPollInterval(std::chrono::milliseconds interval);
    bool IsRunning() const noexcept { return running.load(std::memory_order_acquire); }

    // Convenience methods
    std::string GetActiveWindowExe() const;
    std::string GetActiveWindowClass() const;
    std::string GetActiveWindowTitle() const;
    pid_t GetActiveWindowPid() const;

private:
    void MonitorLoop();
    MonitorWindowInfo GetWindowInfo(wID windowId);
    void UpdateWindowMap();          // Full window tracking (optional)
    void CheckActiveWindow();        // Just track active window

    mutable std::shared_mutex dataMutex;
    mutable std::mutex callbackMutex;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    std::chrono::milliseconds interval;

    std::unique_ptr<std::thread> monitorThread;

    // Data
    MonitorWindowInfo activeWindow;
    std::unordered_map<wID, MonitorWindowInfo> windows;  // all windows (if tracking)

    // Callbacks
    std::shared_ptr<WindowCallback> activeWindowCallback;
    std::shared_ptr<WindowCallback> windowAddedCallback;
    std::shared_ptr<WindowCallback> windowRemovedCallback;

    // Simple cache for process names (to avoid hitting /proc too often)
    struct CacheEntry {
        std::string processName;
        std::chrono::steady_clock::time_point timestamp;
    };
    mutable std::shared_mutex cacheMutex;
    std::unordered_map<pid_t, CacheEntry> processNameCache;
    std::string GetProcessNameCached(pid_t pid);

    // Logging helpers
    static void LogInfo(const std::string& msg) {
        havel::info("[WindowMonitor] {}", msg);
    }
    static void LogWarning(const std::string& msg) {
        havel::warning("[WindowMonitor] {}", msg);
    }
    static void LogError(const std::string& msg) {
        havel::error("[WindowMonitor] {}", msg);
    }
};

} // namespace havel