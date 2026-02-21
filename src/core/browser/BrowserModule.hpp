#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace havel {

// Browser types
enum class BrowserType { Unknown, Chrome, Chromium, Firefox, Edge, Brave };

// Browser tab/window information
struct BrowserTab {
  int id;
  std::string title;
  std::string url;
  std::string type; // "page", "background_page", "service_worker", etc.
  std::string windowId;
};

// Browser window information
struct BrowserWindow {
  int id;
  int x, y;
  int width, height;
  bool maximized;
  bool minimized;
  bool fullscreen;
  std::string type; // "normal", "popup", "panel"
};

// Browser instance information
struct BrowserInstance {
  BrowserType type;
  std::string name;
  std::string path;
  int pid;
  int cdpPort; // -1 if not using CDP
  std::string cdpUrl;
};

// Browser extension information
struct BrowserExtension {
  std::string id;
  std::string name;
  std::string version;
  bool enabled;
  std::string description;
};

/**
 * BrowserModule - Browser automation via CDP (Chrome) and Marionette (Firefox)
 *
 * Provides high-level browser automation:
 * - Connection: connect(), connectFirefox(), setPort(), getDefaultBrowser()
 * - Discovery: getOpenBrowsers(), listTabs(), listWindows(), listExtensions()
 * - Navigation: open(), goto(), back(), forward(), reload()
 * - Tab/Window: newTab(), closeTab(), activate(), setWindowSize()
 * - Interaction: click(), type(), eval()
 * - Zoom: setZoom(), getZoom(), resetZoom()
 * - Screenshots: screenshot()
 */
class BrowserModule {
public:
  BrowserModule();
  ~BrowserModule();

  // === Connection ===
  bool connect(const std::string &browserUrl = "http://localhost:9222");
  bool connectFirefox(int port = 2828); // Firefox Marionette
  void disconnect();
  bool isConnected() const { return connected; }
  BrowserType getBrowserType() const { return browserType; }

  // Port configuration
  void setPort(int port) { cdpPort = port; }
  int getPort() const { return cdpPort; }
  std::string getDefaultBrowserPath();
  BrowserType getDefaultBrowserType();

  // === Browser Discovery ===
  std::vector<BrowserInstance> getOpenBrowsers();
  BrowserInstance getDefaultBrowser();

  // === Navigation ===
  bool open(const std::string &url);
  bool newTab(const std::string &url = "");
  bool gotoUrl(const std::string &url);
  bool back();
  bool forward();
  bool reload(bool ignoreCache = false);

  // === Tab Management ===
  std::vector<BrowserTab> listTabs();
  bool activate(int tabId);
  bool closeTab(int tabId);  // Renamed from close() to avoid conflicts
  bool closeAll();
  int getCurrentTabId() const { return currentTabId; }

  // === Window Management ===
  std::vector<BrowserWindow> listWindows();
  BrowserWindow getWindowInfo();
  bool setWindowSize(int windowId, int width, int height);
  bool setWindowPosition(int windowId, int x, int y);
  bool maximizeWindow(int windowId = -1);
  bool minimizeWindow(int windowId = -1);
  bool fullscreenWindow(int windowId = -1);

  // === Extension Management ===
  std::vector<BrowserExtension> listExtensions();
  bool enableExtension(const std::string &extensionId);
  bool disableExtension(const std::string &extensionId);

  // === Element Interaction ===
  bool click(const std::string &selector);
  bool type(const std::string &selector, const std::string &text);
  bool focus(const std::string &selector);
  bool blur(const std::string &selector);

  // === Zoom Control ===
  bool setZoom(double level); // 0.5 to 3.0
  double getZoom();
  bool resetZoom();

  // === JavaScript Execution ===
  std::string eval(const std::string &js);

  // === Screenshot ===
  bool screenshot(const std::string &path = "");

  // === Info ===
  std::string getCurrentUrl();
  std::string getTitle();

private:
  // CDP helper methods (Chrome)
  std::string sendCdpCommand(const std::string &method,
                             const std::string &params = "{}");
  std::string sendCdpCommandToTab(int tabId, const std::string &method,
                                  const std::string &params = "{}");
  std::string getWebSocketUrl(int tabId);

  // Marionette helper methods (Firefox)
  std::string sendMarionetteCommand(const std::string &command,
                                    const std::string &params = "{}");

  // HTTP helpers
  std::string httpGet(const std::string &url);
  std::string httpPost(const std::string &url, const std::string &body);

  // Browser detection
  std::string findBrowserPath(BrowserType type);
  std::vector<int> findBrowserProcesses(const std::string &processName);

  // State
  bool connected = false;
  BrowserType browserType = BrowserType::Unknown;
  std::string browserUrl; // Base URL for CDP (e.g., http://localhost:9222)
  int cdpPort = 9222;
  int marionettePort = 2828;
  int currentTabId = -1;
  int currentWindowId = -1;
  std::mutex mutex;

  // Cached data
  std::vector<BrowserTab> cachedTabs;
  std::vector<BrowserWindow> cachedWindows;
  std::chrono::steady_clock::time_point lastTabListUpdate;
};

// Global browser instance (singleton pattern for interpreter access)
BrowserModule &getBrowser();

} // namespace havel
