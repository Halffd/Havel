/*
 * MonitoringClipboard.cpp
 *
 * Clipboard with monitoring capabilities implementation.
 */
#include "MonitoringClipboard.hpp"

#include <QApplication>
#include <QClipboard>

namespace havel::host {

MonitoringClipboard::MonitoringClipboard() : HistoryClipboard() {}

MonitoringClipboard::~MonitoringClipboard() {
    stopMonitoring();
}

void MonitoringClipboard::startMonitoring() {
    if (monitoring_) {
        return;  // Already monitoring
    }
    
    monitoring_ = true;
    shouldStop_ = false;
    
    // Capture initial state
    lastContent_ = getText();
    lastChangeTime_ = std::chrono::system_clock::now();
    
    // Start monitor thread
    monitorThread_ = std::thread(&MonitoringClipboard::monitorLoop, this);
}

void MonitoringClipboard::stopMonitoring() {
    if (!monitoring_) {
        return;
    }
    
    shouldStop_ = true;
    monitoring_ = false;
    
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void MonitoringClipboard::setMonitorInterval(int intervalMs) {
    monitorIntervalMs_ = std::max(100, intervalMs);  // Minimum 100ms
}

void MonitoringClipboard::onClipboardChanged(
    const std::function<void(const std::string&)>& callback) {
    onChangeCallback_ = callback;
}

void MonitoringClipboard::monitorLoop() {
    while (!shouldStop_) {
        checkForChanges();
        
        // Sleep for interval (with early exit check)
        for (int i = 0; i < monitorIntervalMs_ && !shouldStop_; i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void MonitoringClipboard::checkForChanges() {
    std::lock_guard<std::mutex> lock(contentMutex_);
    
    // Get current clipboard content
    std::string currentContent;
    if (QApplication::instance()) {
        QClipboard* clipboard = QApplication::clipboard();
        if (clipboard) {
            currentContent = clipboard->text().toStdString();
        }
    }
    
    // Check if changed
    if (currentContent != lastContent_) {
        lastContent_ = currentContent;
        lastChangeTime_ = std::chrono::system_clock::now();
        
        // Add to history
        if (!currentContent.empty()) {
            addToHistory(currentContent);
        }
        
        // Trigger callback
        if (onChangeCallback_) {
            onChangeCallback_(currentContent);
        }
    }
}

} // namespace havel::host
