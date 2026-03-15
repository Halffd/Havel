#include "WindowMonitor.hpp"
#include "Window.hpp"
#include <fstream>
#include <algorithm>

namespace havel {

WindowMonitor::WindowMonitor(std::chrono::milliseconds pollInterval)
    : interval(pollInterval) {
    if (pollInterval.count() < 10) {
        throw std::runtime_error("Poll interval too small (minimum 10ms)");
    }
}

WindowMonitor::~WindowMonitor() {
    Stop();
}

void WindowMonitor::Start() {
    if (running) return;
    running = true;
    stopRequested = false;
    monitorThread = std::make_unique<std::thread>(&WindowMonitor::MonitorLoop, this);
    LogInfo("Started");
}

void WindowMonitor::Stop() {
    if (!running) return;
    stopRequested = true;
    if (monitorThread && monitorThread->joinable()) {
        monitorThread->join();
    }
    running = false;
    LogInfo("Stopped");
}

std::string WindowMonitor::GetProcessNameCached(pid_t pid) {
    if (pid <= 0) return "";

    {
        std::shared_lock lock(cacheMutex);
        auto it = processNameCache.find(pid);
        if (it != processNameCache.end()) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - it->second.timestamp).count();
            if (age < 5) { // 5 second cache
                return it->second.processName;
            }
        }
    }

    // Read from /proc
    std::string result;
    std::string procPath = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream cmdline(procPath);
    if (cmdline) {
        std::getline(cmdline, result, '\0');
        // Extract basename
        size_t pos = result.rfind('/');
        if (pos != std::string::npos) {
            result = result.substr(pos + 1);
        }
    }

    {
        std::unique_lock lock(cacheMutex);
        processNameCache[pid] = {result, std::chrono::steady_clock::now()};
    }
    return result;
}

MonitorWindowInfo WindowMonitor::GetWindowInfo(wID windowId) {
    MonitorWindowInfo info;
    info.windowId = windowId;
    info.lastUpdate = std::chrono::steady_clock::now();
    info.isValid = false;

    // Get window info using Window class methods
    info.title = Window::Title(windowId);
    info.windowClass = Window::Class(windowId);
    info.pid = Window::PID(windowId);
    
    // Get process name from PID
    if (info.pid != 0) {
        info.processName = WindowManager::getProcessName(info.pid);
    }
    
    // Mark as valid if we got any info
    if (!info.title.empty() || info.pid != 0) {
        info.isValid = true;
    }
    
    return info;
}

void WindowMonitor::CheckActiveWindow() {
    wID activeId = WindowManager::GetActiveWindow();
    if (activeId == 0) return;

    auto newInfo = GetWindowInfo(activeId);

    {
        std::unique_lock lock(dataMutex);
        if (!(newInfo == activeWindow)) {
            activeWindow = newInfo;
            std::lock_guard cbLock(callbackMutex);
            if (activeWindowCallback) {
                (*activeWindowCallback)(newInfo);
            }
        }
    }
}

void WindowMonitor::UpdateWindowMap() {
    // This requires WindowManager to have a way to list all windows.
    // If not, you can either skip full tracking or implement X11/Wayland enumeration here.
    // For now, we'll just track active window. Uncomment below if you add GetAllWindows().
    /*
    auto allIds = WindowManager::GetAllWindows();
    std::unordered_map<wID, MonitorWindowInfo> newMap;

    for (auto id : allIds) {
        newMap[id] = GetWindowInfo(id);
    }

    {
        std::unique_lock lock(dataMutex);
        // Detect removed windows
        for (const auto& [id, info] : windows) {
            if (newMap.find(id) == newMap.end()) {
                std::lock_guard cbLock(callbackMutex);
                if (windowRemovedCallback) (*windowRemovedCallback)(info);
            }
        }
        // Detect new windows
        for (const auto& [id, info] : newMap) {
            if (windows.find(id) == windows.end()) {
                std::lock_guard cbLock(callbackMutex);
                if (windowAddedCallback) (*windowAddedCallback)(info);
            }
        }
        windows = std::move(newMap);
    }
    */
}

void WindowMonitor::MonitorLoop() {
    LogInfo("Monitor loop started");
    while (!stopRequested) {
        auto start = std::chrono::steady_clock::now();
        try {
            CheckActiveWindow();
            // UpdateWindowMap(); // enable if full tracking is implemented
        } catch (const std::exception& e) {
            LogError(std::string("Exception: ") + e.what());
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto sleepTime = interval - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleepTime.count() > 0 && !stopRequested) {
            std::this_thread::sleep_for(sleepTime);
        }
    }
    LogInfo("Monitor loop stopped");
}

std::optional<MonitorWindowInfo> WindowMonitor::GetActiveWindowInfo() const {
    std::shared_lock lock(dataMutex);
    if (activeWindow.isValid) {
        return activeWindow;
    }
    return std::nullopt;
}

std::unordered_map<wID, MonitorWindowInfo> WindowMonitor::GetAllWindows() const {
    std::shared_lock lock(dataMutex);
    return windows;
}

void WindowMonitor::SetActiveWindowCallback(WindowCallback callback) {
    std::lock_guard lock(callbackMutex);
    activeWindowCallback = std::make_shared<WindowCallback>(std::move(callback));
}

void WindowMonitor::SetWindowAddedCallback(WindowCallback callback) {
    std::lock_guard lock(callbackMutex);
    windowAddedCallback = std::make_shared<WindowCallback>(std::move(callback));
}

void WindowMonitor::SetWindowRemovedCallback(WindowCallback callback) {
    std::lock_guard lock(callbackMutex);
    windowRemovedCallback = std::make_shared<WindowCallback>(std::move(callback));
}

void WindowMonitor::SetPollInterval(std::chrono::milliseconds newInterval) {
    interval = newInterval;
}

std::string WindowMonitor::GetActiveWindowExe() const {
    std::shared_lock lock(dataMutex);
    return activeWindow.processName;
}

std::string WindowMonitor::GetActiveWindowClass() const {
    std::shared_lock lock(dataMutex);
    return activeWindow.windowClass;
}

std::string WindowMonitor::GetActiveWindowTitle() const {
    std::shared_lock lock(dataMutex);
    return activeWindow.title;
}

pid_t WindowMonitor::GetActiveWindowPid() const {
    std::shared_lock lock(dataMutex);
    return activeWindow.pid;
}

} // namespace havel