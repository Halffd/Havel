/*
 * ClipboardService.hpp
 *
 * Pure C++ clipboard service - no VM, no interpreter, no HavelValue.
 * Provides basic clipboard operations and history management.
 * 
 * Uses Qt internally (QClipboard), but doesn't leak Qt types to VM.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * ClipboardService - Clipboard operations with history
 * 
 * Uses Qt internally for clipboard access.
 * Returns plain C++ types that HostBridge translates to VM types.
 */
class ClipboardService {
public:
    ClipboardService();
    ~ClipboardService();

    // =========================================================================
    // Basic clipboard operations
    // =========================================================================

    /// Get clipboard text
    std::string getText() const;

    /// Set clipboard text
    bool setText(const std::string& text);

    /// Clear clipboard
    bool clear();

    // Aliases for convenience
    std::string get() const { return getText(); }
    std::string in() const { return getText(); }
    std::string out() const { return getText(); }

    // =========================================================================
    // Clipboard history
    // =========================================================================

    /// Add text to history
    void addToHistory(const std::string& text);

    /// Get history item by index
    /// @param index Item index (0 = most recent)
    /// @return Text content or empty if invalid index
    std::string getHistoryItem(int index) const;

    /// Get number of history items
    int getHistoryCount() const;

    /// Clear history
    void clearHistory();

    /// Get all history items
    std::vector<std::string> getHistory() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set maximum history size
    void setMaxHistorySize(int size);

    /// Get maximum history size
    int getMaxHistorySize() const;

private:
    std::vector<std::string> history_;
    int maxHistorySize_ = 100;  // Default: keep last 100 items
};

} // namespace havel::host
