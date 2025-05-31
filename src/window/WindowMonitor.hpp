#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <shared_mutex> // For read-write lock
#include <exception>
#include <optional>
#include <functional>
#include <iostream> // For basic logging
#include "Window.hpp"
#include "WindowManager.hpp"

namespace havel {

// Custom exceptions
class WindowMonitorError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct WindowInfo {
    std::string title;
    std::string windowClass;
    std::string processName;
    pid_t pid{0};
    unsigned long windowId{0};
    std::chrono::steady_clock::time_point lastUpdate;
    bool isValid{false}; // Validity flag
    
    // Thread-safe copy constructor
    WindowInfo(const WindowInfo& other) {
        std::atomic_thread_fence(std::memory_order_acquire);
        title = other.title;
        windowClass = other.windowClass;
        processName = other.processName;
        pid = other.pid;
        windowId = other.windowId;
        lastUpdate = other.lastUpdate;
        isValid = other.isValid;
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    WindowInfo& operator=(const WindowInfo& other) {
        if (this != &other) {
            WindowInfo temp(other);
            std::swap(*this, temp);
        }
        return *this;
    }
    
    WindowInfo() = default;
    WindowInfo(WindowInfo&&) noexcept = default;
    WindowInfo& operator=(WindowInfo&&) noexcept = default;
};

// Add operator== for WindowInfo
inline bool operator==(const WindowInfo& lhs, const WindowInfo& rhs) {
    return lhs.windowId == rhs.windowId &&
           lhs.pid == rhs.pid &&
           lhs.title == rhs.title &&
           lhs.windowClass == rhs.windowClass &&
           lhs.processName == rhs.processName;
}

class WindowMonitor {
public:
    // Define callback type before using it
    using WindowCallback = std::function<void(const WindowInfo&)>;

    explicit WindowMonitor(std::chrono::milliseconds pollInterval = std::chrono::milliseconds(100));
    ~WindowMonitor();
    
    // Non-copyable, movable
    WindowMonitor(const WindowMonitor&) = delete;
    WindowMonitor& operator=(const WindowMonitor&) = delete;
    WindowMonitor(WindowMonitor&&) noexcept = default;
    WindowMonitor& operator=(WindowMonitor&&) noexcept = default;
    
    // Start/Stop monitoring with error handling
    void Start();
    void Stop();
    
    // Getters with shared mutex for better performance
    std::optional<WindowInfo> GetActiveWindowInfo() const {
        std::shared_lock<std::shared_mutex> lock(windowsMutex);
        return activeWindow;
    }
    
    std::unordered_map<wID, WindowInfo> GetAllWindows() const {
        std::shared_lock<std::shared_mutex> lock(windowsMutex);
        return windows;
    }
    
    // Callbacks defined in header to avoid redefinition
    void SetActiveWindowCallback(WindowCallback callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        activeWindowCallback = std::make_shared<WindowCallback>(std::move(callback));
    }
    
    void SetWindowAddedCallback(WindowCallback callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        windowAddedCallback = std::make_shared<WindowCallback>(std::move(callback));
    }
    
    void SetWindowRemovedCallback(WindowCallback callback) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        windowRemovedCallback = std::make_shared<WindowCallback>(std::move(callback));
    }
    
    // Settings
    void SetPollInterval(std::chrono::milliseconds interval);
    bool IsRunning() const noexcept { return running.load(std::memory_order_acquire); }
    
    // Performance monitoring
    struct Stats {
        std::atomic<uint64_t> windowsTracked{0};
        std::atomic<uint64_t> windowsAdded{0};
        std::atomic<int64_t> windowsRemoved{0};
        std::atomic<int64_t> activeWindowChanges{0};
        
        // Delete copy operations
        Stats() = default;
        Stats(const Stats&) = delete;
        Stats& operator=(const Stats&) = delete;
        // Delete move operations since atomic types aren't moveable
        Stats(Stats&&) = delete;
        Stats& operator=(Stats&&) = delete;
    };

    // Return stats by reference to avoid copy/move issues
    const Stats& GetStats() const noexcept { return stats; }

private:
    void MonitorLoop();
    WindowInfo GetWindowInfo(wID windowId) const;
    void UpdateWindowMap();
    void CheckForWindowChanges();
    
    // Thread synchronization
    mutable std::shared_mutex windowsMutex;
    mutable std::mutex callbackMutex;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    std::chrono::milliseconds interval;
    
    // Resource management
    std::unique_ptr<std::thread> monitorThread;
    std::unordered_map<wID, WindowInfo> windows;
    WindowInfo activeWindow;
    Stats stats;
    
    // Callbacks stored as shared_ptr instead of weak_ptr
    std::shared_ptr<WindowCallback> activeWindowCallback;
    std::shared_ptr<WindowCallback> windowAddedCallback;
    std::shared_ptr<WindowCallback> windowRemovedCallback;
    
    // Thread-safe cache
    class Cache {
    public:
        struct CacheEntry {
            pid_t pid;
            std::string processName;
        };

        void Set(wID windowId, pid_t pid, const std::string& procName = "") {
            std::unique_lock<std::shared_mutex> lock(mutex);
            pidCache[windowId] = CacheEntry{pid, procName};
        }

        std::optional<CacheEntry> Get(wID windowId) const {
            std::shared_lock<std::shared_mutex> lock(mutex);
            auto it = pidCache.find(windowId);
            if (it != pidCache.end()) {
                return it->second;
            }
            return std::nullopt;
        }

    private:
        mutable std::shared_mutex mutex;
        std::unordered_map<wID, CacheEntry> pidCache;
    };
    mutable Cache cache;

    // Logging helpers defined inline to avoid redefinition
    static void LogInfo(const std::string& msg) {
        std::cout << "[INFO] " << msg << std::endl;
    }
    
    static void LogWarning(const std::string& msg) {
        std::cerr << "[WARNING] " << msg << std::endl;
    }
    
    static void LogError(const std::string& msg) {
        std::cerr << "[ERROR] " << msg << std::endl;
    }
};

} // namespace havel 