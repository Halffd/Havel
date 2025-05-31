#include "WindowMonitor.hpp"
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <system_error>

namespace havel {

WindowMonitor::WindowMonitor(std::chrono::milliseconds pollInterval)
    : interval(pollInterval) {
    if (pollInterval.count() < 10) {
        throw WindowMonitorError("Poll interval too small (minimum 10ms)");
    }
}

WindowMonitor::~WindowMonitor() {
    Stop();
}

void WindowMonitor::Start() {
    if (running) {
        return;
    }
    
    running = true;
    stopRequested = false;
    monitorThread = std::make_unique<std::thread>(&WindowMonitor::MonitorLoop, this);
    LogInfo("Window monitor started");
}

void WindowMonitor::Stop() {
    try {
        if (!running) {
            return;
        }
        
        stopRequested = true;
        if (monitorThread && monitorThread->joinable()) {
            monitorThread->join();
        }
        running = false;
        LogInfo("Window monitor stopped");
    } catch (const std::exception& e) {
        LogError("Error stopping monitor: " + std::string(e.what()));
        throw;
    }
}

void WindowMonitor::MonitorLoop() {
    while (!stopRequested) {
        try {
            auto start = std::chrono::steady_clock::now();
            
            UpdateWindowMap();
            CheckForWindowChanges();
            
            auto end = std::chrono::steady_clock::now();
            auto duration = end - start;
            
            if (duration > interval) {
                LogWarning("Monitor loop took longer than interval: " + 
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()) + "ms");
            }
            
            std::this_thread::sleep_for(interval);
            
        } catch (const std::exception& e) {
            LogError("Error in monitor loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

WindowInfo WindowMonitor::GetWindowInfo(wID windowId) const {
    WindowInfo info;
    info.windowId = windowId;
    info.lastUpdate = std::chrono::steady_clock::now();
    
    WindowManager wm;
    if (wm.IsX11()) {
        // Use cached info if available
        if (auto entry = cache.Get(windowId)) {
            info.pid = entry->pid;
            info.processName = entry->processName;
        } else {
            // Get PID from window property
            // ... X11 specific code to get PID ...
            
            // Read process name from /proc
            std::ifstream cmdline("/proc/" + std::to_string(info.pid) + "/cmdline");
            if (cmdline) {
                std::getline(cmdline, info.processName, '\0');
                cache.Set(windowId, info.pid, info.processName);
            }
        }
    }
    
    return info;
}

void WindowMonitor::UpdateWindowMap() {
    WindowManager wm;
    // Temporary fix until ListWindows is implemented
    std::vector<wID> windows;
    #ifdef __linux__
    // Get window list from X11 or Wayland
    // TODO: Implement window listing
    #endif
    
    std::unordered_map<wID, WindowInfo> newWindows;
    
    for (const auto& windowId : windows) {
        auto info = GetWindowInfo(windowId);
        newWindows[windowId] = std::move(info);
    }
    
    // Update main window map with proper locks
    {
        std::unique_lock<std::shared_mutex> lock(windowsMutex);
        
        // Check for removed windows
        for (const auto& [id, info] : this->windows) {
            if (newWindows.find(id) == newWindows.end()) {
                std::lock_guard<std::mutex> cbLock(callbackMutex);
                if (auto cb = windowRemovedCallback) {
                    (*cb)(info);
                }
            }
        }
        
        // Check for new windows
        for (const auto& [id, info] : newWindows) {
            if (this->windows.find(id) == this->windows.end()) {
                std::lock_guard<std::mutex> cbLock(callbackMutex);
                if (auto cb = windowAddedCallback) {
                    (*cb)(info);
                }
            }
        }
        
        this->windows = std::move(newWindows);
    }
}

void WindowMonitor::CheckForWindowChanges() {
    try {
        WindowManager wm;
        auto activeWin = wm.GetActiveWindow();
        auto newInfo = GetWindowInfo(activeWin);
        
        if (!(newInfo == activeWindow)) {
            std::lock_guard<std::mutex> lock(callbackMutex);
            activeWindow = newInfo;
            if (auto cb = activeWindowCallback) {
                (*cb)(newInfo);
            }
        }
    } catch (const std::exception& e) {
        LogWarning("Failed to check active window: " + std::string(e.what()));
    }
}

void WindowMonitor::SetPollInterval(std::chrono::milliseconds newInterval) {
    interval = newInterval;
}

} // namespace H 