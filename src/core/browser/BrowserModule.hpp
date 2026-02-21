#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>

namespace havel {

// Browser tab/window information
struct BrowserTab {
    int id;
    std::string title;
    std::string url;
    std::string type; // "page", "background_page", "service_worker", etc.
};

// Browser window information  
struct BrowserWindow {
    int id;
    int x, y;
    int width, height;
    bool maximized;
    bool minimized;
    bool fullscreen;
};

/**
 * BrowserModule - Browser automation via Chrome DevTools Protocol (CDP)
 * 
 * Provides high-level browser automation:
 * - browser.open(url) - Open URL in new tab
 * - browser.newTab() - Create new tab
 * - browser.goto(url) - Navigate to URL
 * - browser.click(selector) - Click element
 * - browser.setZoom(level) - Set page zoom (0.5 - 3.0)
 * - browser.getZoom() - Get current zoom level
 * - browser.close() - Close current tab
 * - browser.closeAll() - Close all tabs
 * - browser.listTabs() - List all open tabs
 * - browser.activate(tabId) - Activate tab
 * - browser.screenshot(path) - Take screenshot
 * - browser.eval(js) - Execute JavaScript
 */
class BrowserModule {
public:
    BrowserModule();
    ~BrowserModule();

    // Initialize connection to browser (must be called first)
    bool connect(const std::string& browserUrl = "http://localhost:9222");
    void disconnect();
    bool isConnected() const { return connected; }

    // Navigation
    bool open(const std::string& url);
    bool newTab(const std::string& url = "");
    bool gotoUrl(const std::string& url);
    bool back();
    bool forward();
    bool reload(bool ignoreCache = false);

    // Tab management
    std::vector<BrowserTab> listTabs();
    bool activate(int tabId);
    bool close(int tabId = -1); // -1 = current tab
    bool closeAll();
    int getCurrentTabId() const { return currentTabId; }

    // Element interaction
    bool click(const std::string& selector);
    bool type(const std::string& selector, const std::string& text);
    bool focus(const std::string& selector);
    bool blur(const std::string& selector);

    // Zoom control
    bool setZoom(double level); // 0.5 to 3.0
    double getZoom();
    bool resetZoom();

    // JavaScript execution
    std::string eval(const std::string& js);
    
    // Screenshot
    bool screenshot(const std::string& path = "");

    // Window control
    BrowserWindow getWindowInfo();
    bool setWindowSize(int width, int height);
    bool setWindowPosition(int x, int y);
    bool maximizeWindow();
    bool minimizeWindow();
    bool fullscreenWindow();

    // Get current URL
    std::string getCurrentUrl();
    
    // Get page title
    std::string getTitle();

private:
    // CDP helper methods
    std::string sendCdpCommand(const std::string& method, 
                                const std::string& params = "{}");
    std::string sendCdpCommandToTab(int tabId, const std::string& method,
                                     const std::string& params = "{}");
    std::string getWebSocketUrl(int tabId);
    std::string httpGet(const std::string& url);
    std::string httpPost(const std::string& url, const std::string& body);

    // State
    bool connected = false;
    std::string browserUrl; // Base URL for CDP (e.g., http://localhost:9222)
    int currentTabId = -1;
    std::mutex mutex;

    // Cached tab list
    std::vector<BrowserTab> cachedTabs;
    std::chrono::steady_clock::time_point lastTabListUpdate;
};

// Global browser instance (singleton pattern for interpreter access)
 BrowserModule& getBrowser();

} // namespace havel
