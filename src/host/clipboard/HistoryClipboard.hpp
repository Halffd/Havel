/*
 * HistoryClipboard.hpp
 *
 * Clipboard with history management - extends core Clipboard.
 * Only pay for history if you use it.
 */
#pragma once

#include "Clipboard.hpp"
#include <vector>
#include <string>
#include <algorithm>

namespace havel::host {

/**
 * HistoryClipboard - Clipboard with history
 * 
 * Extends core Clipboard with:
 * - History tracking
 * - Configurable max size
 * - Recent item access
 * 
 * No monitoring, no callbacks - just history.
 */
class HistoryClipboard : public Clipboard {
public:
    HistoryClipboard();
    ~HistoryClipboard();

    // =========================================================================
    // History management - opt-in overhead
    // =========================================================================

    /// Add text to history (called by user or monitoring)
    void addToHistory(const std::string& text);

    /// Get history item by index (0 = most recent)
    std::string getHistoryItem(int index) const;

    /// Get number of history items
    int getHistoryCount() const;

    /// Clear history
    void clearHistory();

    /// Get all history items
    const std::vector<std::string>& getHistory() const;

    /// Get last item (most recent)
    std::string getLast() const;

    /// Get recent N items
    std::vector<std::string> getRecent(int count) const;

    /// Get history range [start, end)
    std::vector<std::string> getHistoryRange(int start, int end) const;

    /// Filter history by pattern (substring match)
    std::vector<std::string> filter(const std::string& pattern) const;

    /// Find history items matching pattern
    std::vector<std::string> find(const std::string& pattern) const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set maximum history size
    void setMaxHistorySize(int size);

    /// Get maximum history size
    int getMaxHistorySize() const;

protected:
    std::vector<std::string> history_;
    int maxHistorySize_ = 100;  // Default: keep last 100 items
};

} // namespace havel::host
