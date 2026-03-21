/*
 * BrowserService.hpp
 *
 * Browser automation service.
 * Provides high-level browser automation via CDP (Chrome) and Marionette (Firefox).
 * 
 * Uses HTTP/WebSocket internally, but doesn't leak types to VM.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel { class BrowserModule; }

namespace havel::host {

// Forward declarations from BrowserModule
enum class BrowserType { Unknown, Chrome, Chromium, Firefox, Edge, Brave };

struct BrowserTab {
    int id = 0;
    std::string title;
    std::string url;
    std::string type;
};

struct BrowserWindow {
    int id = 0;
    int x = 0, y = 0;
    int width = 0, height = 0;
    bool maximized = false;
    bool minimized = false;
    bool fullscreen = false;
};

/**
 * BrowserService - Browser automation
 * 
 * Uses BrowserModule internally for CDP/Marionette communication.
 * Returns plain C++ types that HostBridge translates to VM types.
 */
class BrowserService {
public:
    BrowserService();
    ~BrowserService();

    // =========================================================================
    // Connection
    // =========================================================================

    /// Connect to browser via CDP (Chrome)
    /// @param browserUrl Browser debugger URL (default: http://localhost:9222)
    /// @return true if connected
    bool connect(const std::string& browserUrl = "http://localhost:9222");

    /// Connect to Firefox via Marionette
    /// @param port Marionette port (default: 2828)
    /// @return true if connected
    bool connectFirefox(int port = 2828);

    /// Disconnect from browser
    void disconnect();

    /// Check if connected
    bool isConnected() const;

    /// Get browser type
    BrowserType getBrowserType() const;

    // =========================================================================
    // Navigation
    // =========================================================================

    /// Open URL in current tab
    bool open(const std::string& url);

    /// Open URL in new tab
    bool newTab(const std::string& url = "");

    /// Navigate to URL
    bool gotoUrl(const std::string& url);

    /// Go back
    bool back();

    /// Go forward
    bool forward();

    /// Reload page
    bool reload(bool ignoreCache = false);

    // =========================================================================
    // Tab Management
    // =========================================================================

    /// List all tabs
    std::vector<BrowserTab> listTabs();

    /// Activate tab by ID
    bool activate(int tabId);

    /// Close tab by ID
    bool closeTab(int tabId);

    /// Close all tabs
    bool closeAll();

    /// Get current tab ID
    int getCurrentTabId() const;

    /// Get active tab info
    BrowserTab getActiveTab() const;

    /// Get active tab title
    std::string getActiveTabTitle() const;

    /// Get current URL
    std::string getCurrentUrl() const;

    /// Get page title
    std::string getTitle() const;

    // =========================================================================
    // Window Management
    // =========================================================================

    /// List all windows
    std::vector<BrowserWindow> listWindows();

    /// Set window size
    bool setWindowSize(int windowId, int width, int height);

    /// Set window position
    bool setWindowPosition(int windowId, int x, int y);

    /// Maximize window
    bool maximizeWindow(int windowId = -1);

    /// Minimize window
    bool minimizeWindow(int windowId = -1);

    // =========================================================================
    // Element Interaction
    // =========================================================================

    /// Click element by selector
    bool click(const std::string& selector);

    /// Type text into element
    bool type(const std::string& selector, const std::string& text);

    /// Focus element
    bool focus(const std::string& selector);

    // =========================================================================
    // Zoom Control
    // =========================================================================

    /// Set zoom level (0.5 to 3.0)
    bool setZoom(double level);

    /// Get current zoom level
    double getZoom() const;

    /// Reset zoom to 100%
    bool resetZoom();

    // =========================================================================
    // JavaScript Execution
    // =========================================================================

    /// Execute JavaScript and return result
    std::string eval(const std::string& js);

    // =========================================================================
    // Screenshot
    // =========================================================================

    /// Take screenshot and save to file
    bool screenshot(const std::string& path = "");

private:
    std::shared_ptr<havel::BrowserModule> m_browser;
};

} // namespace havel::host
