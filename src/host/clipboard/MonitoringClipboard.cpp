/*
 * MonitoringClipboard.cpp
 *
 * Clipboard with monitoring capabilities implementation.
 */
#include "MonitoringClipboard.hpp"

#ifdef HAVE_QT_EXTENSION
#include "Clipboard.hpp"
#include <QApplication>
#include <QClipboard>
#endif

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
  std::lock_guard<std::mutex> lock(callback_mutex_);
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
  std::string currentContent;

  {
    std::lock_guard<std::mutex> lock(contentMutex_);

    if (QApplication::instance()) {
      QClipboard* clipboard = QApplication::clipboard();
      if (clipboard) {
        currentContent = clipboard->text().toStdString();
      }
    }

    if (currentContent != lastContent_) {
      lastContent_ = currentContent;
      lastChangeTime_ = std::chrono::system_clock::now();

      if (!currentContent.empty()) {
        addToHistory(currentContent);
      }
    } else {
      return;
    }
  }

  std::function<void(const std::string&)> cb;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    cb = onChangeCallback_;
  }
  if (cb) {
    cb(currentContent);
  }
}

} // namespace havel::host
