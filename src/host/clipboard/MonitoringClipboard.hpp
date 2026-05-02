/*
 * MonitoringClipboard.hpp
 *
 * Clipboard with monitoring capabilities - extends HistoryClipboard.
 * Watches clipboard for changes and triggers callbacks.
 */
#pragma once

#include "HistoryClipboard.hpp"
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

namespace havel::host {

/**
 * MonitoringClipboard - Clipboard with automatic monitoring
 * 
 * Extends HistoryClipboard with:
 * - Automatic clipboard change detection
 * - Configurable monitoring interval
 * - Change callbacks
 * - Change timestamp tracking
 * 
 * Use when you need to react to clipboard changes automatically.
 */
class MonitoringClipboard : public HistoryClipboard {
public:
    MonitoringClipboard();
    ~MonitoringClipboard();

    // =========================================================================
    // Monitoring control
    // =========================================================================

    /// Start monitoring clipboard for changes
    void startMonitoring();

    /// Stop monitoring
    void stopMonitoring();

    /// Check if currently monitoring
    bool isMonitoring() const { return monitoring_; }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set monitor interval in milliseconds (default: 500)
    void setMonitorInterval(int intervalMs);

    /// Get current monitor interval
    int getMonitorInterval() const { return monitorIntervalMs_; }

    // =========================================================================
    // Callbacks
    // =========================================================================

    /// Set callback for clipboard changes (text content)
    void onClipboardChanged(const std::function<void(const std::string&)>& callback);

    // =========================================================================
    // State
    // =========================================================================

    /// Get timestamp of last clipboard change
    std::chrono::system_clock::time_point getLastChangeTime() const { return lastChangeTime_; }

private:
    void monitorLoop();
    void checkForChanges();

    std::atomic<bool> monitoring_{false};
    std::atomic<bool> shouldStop_{false};
    int monitorIntervalMs_ = 500;  // Check every 500ms by default
    
    std::thread monitorThread_;
    std::function<void(const std::string&)> onChangeCallback_;
    
    std::string lastContent_;
    std::chrono::system_clock::time_point lastChangeTime_;
    
    mutable std::mutex contentMutex_;
};

} // namespace havel::host
