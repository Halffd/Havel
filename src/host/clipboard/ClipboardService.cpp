/*
 * ClipboardService.cpp
 *
 * Clipboard service implementation using Qt.
 */
#include "ClipboardService.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>

namespace havel::host {

ClipboardService::ClipboardService() {
}

ClipboardService::~ClipboardService() {
}

std::string ClipboardService::getText() const {
    if (!QGuiApplication::instance()) {
        return "";
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text().toStdString() : "";
}

bool ClipboardService::setText(const std::string& text) {
    if (!QGuiApplication::instance()) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }
    clipboard->setText(QString::fromStdString(text));
    return true;
}

bool ClipboardService::clear() {
    if (!QGuiApplication::instance()) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }
    clipboard->clear();
    return true;
}

void ClipboardService::addToHistory(const std::string& text) {
    if (text.empty()) {
        return;
    }
    
    // Don't add duplicates at the top
    if (!history_.empty() && history_.front() == text) {
        return;
    }
    
    // Add to front (most recent)
    history_.insert(history_.begin(), text);
    
    // Trim to max size
    while (static_cast<int>(history_.size()) > maxHistorySize_) {
        history_.pop_back();
    }
}

std::string ClipboardService::getHistoryItem(int index) const {
    if (index < 0 || index >= static_cast<int>(history_.size())) {
        return "";
    }
    return history_[index];
}

int ClipboardService::getHistoryCount() const {
    return static_cast<int>(history_.size());
}

void ClipboardService::clearHistory() {
    history_.clear();
}

std::vector<std::string> ClipboardService::getHistory() const {
    return history_;
}

void ClipboardService::setMaxHistorySize(int size) {
    maxHistorySize_ = std::max(1, size);
    
    // Trim if necessary
    while (static_cast<int>(history_.size()) > maxHistorySize_) {
        history_.pop_back();
    }
}

int ClipboardService::getMaxHistorySize() const {
    return maxHistorySize_;
}

} // namespace havel::host
