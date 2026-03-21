/*
 * Clipboard.hpp
 *
 * Core clipboard operations - NO history, NO monitoring.
 * Just get/set/clear with minimal overhead.
 * 
 * Uses Qt internally but doesn't leak Qt types.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * Clipboard - Minimal clipboard access
 * 
 * No history, no monitoring, no callbacks.
 * Just the basics with zero overhead.
 */
class Clipboard {
public:
    Clipboard();
    ~Clipboard();

    // =========================================================================
    // Core operations - minimal overhead
    // =========================================================================

    /// Get clipboard text
    std::string getText() const;

    /// Set clipboard text
    bool setText(const std::string& text);

    /// Clear clipboard
    bool clear();

    /// Check if clipboard has text
    bool hasText() const;

    // Aliases for convenience
    std::string get() const { return getText(); }
    std::string in() const { return getText(); }
    std::string out() const { return getText(); }

private:
    // Cached pointer - no ownership
    void* clipboard_ = nullptr;
};

} // namespace havel::host
