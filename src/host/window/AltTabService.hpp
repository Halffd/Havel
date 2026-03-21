/*
 * AltTabService.hpp
 *
 * Alt-Tab window switcher service.
 * Provides programmatic control over the Alt-Tab window switcher.
 * 
 * Uses AltTabWindow internally (Qt GUI), but provides a simple service interface.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace havel::gui { class AltTabWindow; }

namespace havel::host {

/**
 * AltTabInfo - Information about a window in the Alt-Tab list
 */
struct AltTabInfo {
    std::string title;
    std::string className;
    std::string processName;
    int64_t windowId = 0;
    bool active = false;
};

/**
 * AltTabService - Alt-Tab window switcher
 * 
 * Provides programmatic control over the Alt-Tab UI:
 * - Show/hide Alt-Tab dialog
 * - Navigate windows (next, previous)
 * - Select window
 * - Refresh window list
 * - Configure appearance
 */
class AltTabService {
public:
    AltTabService();
    ~AltTabService();

    // =========================================================================
    // Show/Hide
    // =========================================================================

    /// Show Alt-Tab dialog
    void show();

    /// Hide Alt-Tab dialog
    void hide();

    /// Toggle Alt-Tab visibility
    void toggle();

    // =========================================================================
    // Navigation
    // =========================================================================

    /// Select next window
    void next();

    /// Select previous window
    void previous();

    /// Select current highlighted window
    void select();

    // =========================================================================
    // Window List
    // =========================================================================

    /// Refresh window list
    void refresh();

    /// Get list of windows
    std::vector<AltTabInfo> getWindows() const;

    /// Get number of windows
    int getWindowCount() const;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set thumbnail size
    void setThumbnailSize(int width, int height);

    /// Get thumbnail width
    int getThumbnailWidth() const;

    /// Get thumbnail height
    int getThumbnailHeight() const;

    /// Set maximum visible windows
    void setMaxVisibleWindows(int count);

    /// Get maximum visible windows
    int getMaxVisibleWindows() const;

    /// Enable/disable animations
    void setAnimationsEnabled(bool enabled);

    /// Check if animations are enabled
    bool isAnimationsEnabled() const;

private:
    std::shared_ptr<havel::gui::AltTabWindow> m_altTab;
};

} // namespace havel::host
