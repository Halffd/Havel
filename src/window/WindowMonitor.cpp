#include "WindowMonitor.hpp"
#include "WindowManager.hpp"
#include <fstream>

namespace havel {

WindowMonitor::WindowMonitor(std::chrono::milliseconds pollInterval)
    : interval(pollInterval) {
    if (pollInterval.count() < 10) {
        throw std::runtime_error("Poll interval too small (minimum 10ms)");
    }
}

WindowMonitor::~WindowMonitor() { Stop(); }

void WindowMonitor::Start() {
    if (running) return;
    running = true;
    stopRequested = false;
    monitorThread = std::make_unique<std::thread>(&WindowMonitor::MonitorLoop, this);
}

void WindowMonitor::Stop() {
    if (!running) return;
    stopRequested = true;
    if (monitorThread && monitorThread->joinable()) monitorThread->join();
    running = false;
}

std::string WindowMonitor::GetProcessNameCached(pid_t pid) {
    if (pid <= 0) return "";
    {
        std::shared_lock lock(cacheMutex);
        auto it = processNameCache.find(pid);
        if (it != processNameCache.end()) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - it->second.timestamp).count();
            if (age < 5) return it->second.processName;
        }
    }

    std::string result;
    std::string procPath = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream cmdline(procPath);
    if (cmdline) {
        std::getline(cmdline, result, '\0');
        size_t pos = result.rfind('/');
        if (pos != std::string::npos) result = result.substr(pos + 1);
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

    auto wi = WindowManager::getWindowInfo(static_cast<uint64_t>(windowId));
    info.title = wi.title;
    info.windowClass = wi.windowClass;
    info.pid = wi.pid;
    info.processName = wi.exe;

    if (info.pid == 0) {
        info.pid = WindowManager::GetWindowPID(windowId);
    }
    if (info.processName.empty() && info.pid != 0) {
        info.processName = WindowManager::getProcessName(info.pid);
    }

    if (!info.title.empty() || info.pid != 0) info.isValid = true;
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
            if (activeWindowCallback) (*activeWindowCallback)(newInfo);
        }
    }
}

void WindowMonitor::UpdateWindowMap() {
    auto allWindows = WindowManager::getAllWindows();
    std::unordered_map<wID, MonitorWindowInfo> newMap;

    for (const auto &wi : allWindows) {
        MonitorWindowInfo mi;
        mi.windowId = static_cast<wID>(wi.id);
        mi.title = wi.title;
        mi.windowClass = wi.windowClass;
        mi.processName = wi.exe;
        mi.pid = wi.pid;
        mi.lastUpdate = std::chrono::steady_clock::now();
        mi.isValid = wi.valid;
        newMap[mi.windowId] = mi;
    }

    {
        std::unique_lock lock(dataMutex);
        for (const auto& [id, info] : windows) {
            if (newMap.find(id) == newMap.end()) {
                std::lock_guard cbLock(callbackMutex);
                if (windowRemovedCallback) (*windowRemovedCallback)(info);
            }
        }
        for (const auto& [id, info] : newMap) {
            if (windows.find(id) == windows.end()) {
                std::lock_guard cbLock(callbackMutex);
                if (windowAddedCallback) (*windowAddedCallback)(info);
            }
        }
        windows = std::move(newMap);
    }
}

void WindowMonitor::MonitorLoop() {
    while (!stopRequested) {
        auto start = std::chrono::steady_clock::now();
        try {
            CheckActiveWindow();
            if (windowAddedCallback || windowRemovedCallback) UpdateWindowMap();
        } catch (const std::exception& e) {
            havel::error("[WindowMonitor] Exception: {}", e.what());
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto sleepTime = interval - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (sleepTime.count() > 0 && !stopRequested)
            std::this_thread::sleep_for(sleepTime);
    }
}

std::optional<MonitorWindowInfo> WindowMonitor::GetActiveWindowInfo() const {
    std::shared_lock lock(dataMutex);
    if (activeWindow.isValid) return activeWindow;
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
