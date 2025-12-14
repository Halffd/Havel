#pragma once

#include "types.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace havel {

/**
 * @brief Non-blocking bridge to Wayland compositor-specific APIs
 * 
 * Provides async window information retrieval for KWin and wlroots compositors.
 * Falls back gracefully to process-based detection on unsupported compositors.
 */
class CompositorBridge {
public:
    enum class CompositorType {
        Unknown,
        KWin,       // KDE Plasma (Wayland)
        Sway,       // wlroots-based
        Hyprland,   // wlroots-based
        River,      // wlroots-based
        Wayfire     // wlroots-based
    };

    struct WindowInfo {
        std::string title;
        std::string appId;      // Wayland app_id (similar to X11 class)
        pid_t pid = 0;
        bool valid = false;
    };

    CompositorBridge();
    ~CompositorBridge();

    // Non-copyable
    CompositorBridge(const CompositorBridge&) = delete;
    CompositorBridge& operator=(const CompositorBridge&) = delete;

    /**
     * @brief Start the background monitoring thread
     * 
     * Polls compositor for active window info every 500ms.
     * Non-blocking, updates cached data asynchronously.
     */
    void Start();

    /**
     * @brief Stop the background monitoring thread
     */
    void Stop();

    /**
     * @brief Get cached active window info (instant, non-blocking)
     * 
     * @return WindowInfo Current cached window info
     */
    WindowInfo GetActiveWindow() const;

    /**
     * @brief Get detected compositor type
     */
    CompositorType GetCompositorType() const { return compositorType; }

    /**
     * @brief Check if compositor bridge is available
     *
     * @return true if running on supported Wayland compositor
     */
    bool IsAvailable() const {
        return compositorType != CompositorType::Unknown;
    }

    // Static methods for qdbus communication
    static bool IsKDERunning();
    static bool SendKWinZoomCommand(const std::string& command);
    static std::string SendKWinZoomCommandWithOutput(const std::string& command);

private:
    CompositorType DetectCompositor();
    void MonitoringLoop();
    
    // Compositor-specific implementations
    WindowInfo QueryKWin();
    WindowInfo QueryWlroots();
    
    // Background monitoring
    std::thread monitorThread;
    std::atomic<bool> running{false};
    mutable std::mutex cacheMutex;
    WindowInfo cachedWindowInfo;
    
    CompositorType compositorType{CompositorType::Unknown};
    std::chrono::milliseconds pollInterval{500};
};

} // namespace havel