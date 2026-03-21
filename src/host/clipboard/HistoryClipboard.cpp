/*
 * HistoryClipboard.cpp
 *
 * Clipboard with history management implementation.
 */
#include "HistoryClipboard.hpp"

namespace havel::host {

HistoryClipboard::HistoryClipboard() {
}

HistoryClipboard::~HistoryClipboard() {
}

void HistoryClipboard::addToHistory(const std::string& text) {
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

std::string HistoryClipboard::getHistoryItem(int index) const {
    if (index < 0 || index >= static_cast<int>(history_.size())) {
        return "";
    }
    return history_[index];
}

int HistoryClipboard::getHistoryCount() const {
    return static_cast<int>(history_.size());
}

void HistoryClipboard::clearHistory() {
    history_.clear();
}

const std::vector<std::string>& HistoryClipboard::getHistory() const {
    return history_;
}

std::string HistoryClipboard::getLast() const {
    return history_.empty() ? "" : history_.front();
}

std::vector<std::string> HistoryClipboard::getRecent(int count) const {
    if (count <= 0 || history_.empty()) {
        return {};
    }
    
    int actualCount = std::min(count, static_cast<int>(history_.size()));
    return std::vector<std::string>(history_.begin(), history_.begin() + actualCount);
}

std::vector<std::string> HistoryClipboard::getHistoryRange(int start, int end) const {
    if (start < 0 || start >= static_cast<int>(history_.size()) || end <= start) {
        return {};
    }
    
    int actualEnd = std::min(end, static_cast<int>(history_.size()));
    return std::vector<std::string>(history_.begin() + start, history_.begin() + actualEnd);
}

std::vector<std::string> HistoryClipboard::filter(const std::string& pattern) const {
    std::vector<std::string> result;
    for (const auto& item : history_) {
        if (item.find(pattern) != std::string::npos) {
            result.push_back(item);
        }
    }
    return result;
}

std::vector<std::string> HistoryClipboard::find(const std::string& pattern) const {
    // Could implement more sophisticated search (regex, fuzzy, etc.)
    // For now, same as filter
    return filter(pattern);
}

void HistoryClipboard::setMaxHistorySize(int size) {
    maxHistorySize_ = std::max(1, size);
    
    // Trim if necessary
    while (static_cast<int>(history_.size()) > maxHistorySize_) {
        history_.pop_back();
    }
}

int HistoryClipboard::getMaxHistorySize() const {
    return maxHistorySize_;
}

} // namespace havel::host
