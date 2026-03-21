/*
 * ClipboardService.hpp
 *
 * Clipboard service - combines core + history.
 * 
 * Uses Clipboard for minimal get/set (no overhead).
 * Uses HistoryClipboard for history management (opt-in).
 */
#pragma once

#include "HistoryClipboard.hpp"

namespace havel::host {

/**
 * ClipboardService - Full clipboard functionality
 * 
 * Combines:
 * - Clipboard: Core get/set/clear (minimal overhead)
 * - HistoryClipboard: History management (opt-in)
 */
class ClipboardService : public HistoryClipboard {
public:
    ClipboardService() = default;
    ~ClipboardService() = default;

    // Inherits all methods from Clipboard and HistoryClipboard
    // No additional methods needed - composition is complete
};

} // namespace havel::host
