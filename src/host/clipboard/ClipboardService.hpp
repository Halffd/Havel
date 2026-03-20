/*
 * ClipboardService.hpp
 *
 * Pure C++ clipboard service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for clipboard operations.
 * 
 * Note: Requires Qt GUI application to be running.
 */
#pragma once

#include <string>
#include <functional>

namespace havel::host {

/**
 * ClipboardService - Pure clipboard business logic
 *
 * Provides system-level clipboard operations without any language runtime coupling.
 * All methods return simple C++ types (bool, std::string, etc.)
 * 
 * Requires Qt GUI application to be running.
 */
class ClipboardService {
public:
    ClipboardService() = default;
    ~ClipboardService() = default;

    // =========================================================================
    // Clipboard text operations
    // =========================================================================

    /// Get clipboard text
    /// @return clipboard text (empty if unavailable)
    static std::string getText();

    /// Set clipboard text
    /// @param text Text to set
    /// @return true on success
    static bool setText(const std::string& text);

    /// Clear clipboard
    /// @return true on success
    static bool clear();

    /// Check if clipboard has text
    /// @return true if clipboard has text
    static bool hasText();

    // =========================================================================
    // Clipboard aliases (for convenience)
    // =========================================================================

    /// Alias for getText()
    static std::string get();

    /// Alias for getText()
    static std::string in();

    /// Alias for getText()
    static std::string out();

    // =========================================================================
    // Clipboard send (requires IO)
    // =========================================================================

    /// Send clipboard text via IO
    /// @param text Text to send (empty to use current clipboard)
    /// @param ioSend Function to send text
    /// @return true on success
    static bool send(const std::string& text, std::function<void(const std::string&)> ioSend);

    // =========================================================================
    // Clipboard manager operations (optional, requires ClipboardManager)
    // =========================================================================

    /// Show clipboard manager window
    /// @param manager ClipboardManager instance (can be nullptr)
    static void showManager(void* manager);

    /// Hide clipboard manager window
    /// @param manager ClipboardManager instance (can be nullptr)
    static void hideManager(void* manager);

    /// Get clipboard history
    /// @param manager ClipboardManager instance (can be nullptr)
    /// @return vector of history entries
    static std::vector<std::string> getHistory(void* manager);

    /// Clear clipboard history
    /// @param manager ClipboardManager instance (can be nullptr)
    static void clearHistory(void* manager);

    /// Paste history item
    /// @param manager ClipboardManager instance (can be nullptr)
    /// @param index History item index
    static void pasteHistoryItem(void* manager, int index);
};

} // namespace havel::host
