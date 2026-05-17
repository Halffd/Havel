#pragma once

#include "types.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace havel {

class CompositorStrategy;

class CompositorBridge {
public:
    enum class CompositorType {
        Unknown,
        KWin,
        Sway,
        Hyprland,
        River,
        Wayfire
    };

    struct WindowInfo {
        std::string title;
        std::string appId;
        pid_t pid = 0;
        bool valid = false;
    };

    CompositorBridge();
    ~CompositorBridge();

    CompositorBridge(const CompositorBridge&) = delete;
    CompositorBridge& operator=(const CompositorBridge&) = delete;

    void Start();
    void Stop();

    WindowInfo GetActiveWindow() const;
    CompositorType GetCompositorType() const;
    bool IsAvailable() const;

    static bool IsKDERunning();
    static bool SendKWinZoomCommand(const std::string& command);
    static std::string SendKWinZoomCommandWithOutput(const std::string& command);

private:
    void MonitoringLoop();

    std::unique_ptr<CompositorStrategy> strategy_;
    std::thread monitorThread;
    std::atomic<bool> running{false};
    mutable std::mutex cacheMutex;
    WindowInfo cachedWindowInfo;
    CompositorType compositorType{CompositorType::Unknown};
    std::chrono::milliseconds pollInterval{500};
};

} // namespace havel
