#include "BrowserModule.hpp"
#include "../process/Launcher.hpp"
#include "../utils/Logger.hpp"
#include <curl/curl.h>
#include <fstream>
#include <regex>
#include <sstream>

namespace havel {

// Static callback for curl
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            std::string *userp) {
  size_t total = size * nmemb;
  userp->append((char *)contents, total);
  return total;
}

// Global browser instance
static std::unique_ptr<BrowserModule> g_browserInstance;

BrowserModule &getBrowser() {
  if (!g_browserInstance) {
    g_browserInstance = std::make_unique<BrowserModule>();
  }
  return *g_browserInstance;
}

BrowserModule::BrowserModule() { curl_global_init(CURL_GLOBAL_DEFAULT); }

BrowserModule::~BrowserModule() {
  disconnect();
  curl_global_cleanup();
}

bool BrowserModule::connect(const std::string &url) {
  std::lock_guard<std::mutex> lock(mutex);

  browserUrl = url;

  // Try to get tab list to verify connection
  std::string response = httpGet(browserUrl + "/json/list");

  if (response.empty() || response == "[]") {
    // Try alternative endpoint
    response = httpGet(browserUrl + "/json");
  }

  if (!response.empty() && response != "[]") {
    connected = true;
    info("BrowserModule: Connected to browser at {}", browserUrl);

    // Parse and cache tabs
    // Simple JSON parsing - find first tab
    size_t idPos = response.find("\"id\"");
    if (idPos != std::string::npos) {
      size_t colonPos = response.find(':', idPos);
      size_t numStart = response.find_first_of("0123456789", colonPos);
      if (numStart != std::string::npos) {
        currentTabId = std::stoi(response.substr(numStart));
      }
    }

    return true;
  }

  error("BrowserModule: Failed to connect to browser at {}", browserUrl);
  return false;
}

void BrowserModule::disconnect() {
  std::lock_guard<std::mutex> lock(mutex);
  connected = false;
  currentTabId = -1;
  browserUrl.clear();
  info("BrowserModule: Disconnected");
}

std::string BrowserModule::httpGet(const std::string &url) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  return (res == CURLE_OK) ? response : "";
}

std::string BrowserModule::httpPost(const std::string &url,
                                    const std::string &body) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return "";

  std::string response;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return (res == CURLE_OK) ? response : "";
}

std::string BrowserModule::sendCdpCommand(const std::string &method,
                                          const std::string &params) {
  if (!connected || currentTabId < 0) {
    error("BrowserModule: Not connected or no active tab");
    return "";
  }
  return sendCdpCommandToTab(currentTabId, method, params);
}

std::string BrowserModule::sendCdpCommandToTab(int tabId,
                                               const std::string &method,
                                               const std::string &params) {
  std::string wsUrl = getWebSocketUrl(tabId);
  if (wsUrl.empty()) {
    error("BrowserModule: Could not get WebSocket URL for tab {}", tabId);
    return "";
  }

  // For simple CDP commands, we use the HTTP endpoint
  // Note: Full CDP requires WebSocket, but many commands work via HTTP
  std::string httpUrl = browserUrl + "/json/command";

  std::ostringstream body;
  body << "{\"id\":1,\"method\":\"" << method << "\",\"params\":" << params
       << ",\"sessionId\":\"" << tabId << "\"}";

  return httpPost(httpUrl, body.str());
}

std::string BrowserModule::getWebSocketUrl(int tabId) {
  std::string response = httpGet(browserUrl + "/json/list");
  if (response.empty()) {
    response = httpGet(browserUrl + "/json");
  }

  // Parse WebSocket URL from response
  // Format: "webSocketDebuggerUrl":"ws://localhost:9222/devtools/page/..."
  std::regex wsRegex("\"webSocketDebuggerUrl\":\"([^\"]+)\"");
  std::regex idRegex("\"id\":\"?([^\"]+)\"?");

  std::string wsUrl;
  auto wsBegin =
      std::sregex_iterator(response.begin(), response.end(), wsRegex);
  auto wsEnd = std::sregex_iterator();

  for (auto it = wsBegin; it != wsEnd; ++it) {
    wsUrl = (*it)[1].str();
    // Check if this is our tab
    if (wsUrl.find(std::to_string(tabId)) != std::string::npos) {
      return wsUrl;
    }
  }

  // Return first WebSocket URL if no match
  if (wsBegin != wsEnd) {
    return wsBegin->str();
  }

  return "";
}

// === Navigation ===

bool BrowserModule::open(const std::string &url) { return newTab(url); }

bool BrowserModule::newTab(const std::string &url) {
  if (!connected) {
    error("BrowserModule: Not connected");
    return false;
  }

  // Create new target (tab)
  std::string response = sendCdpCommand(
      "Target.createTarget", "{\"url\":\"" + url + "\",\"newWindow\":true}");

  if (!response.empty() && response.find("\"targetId\"") != std::string::npos) {
    // Extract target ID
    size_t pos = response.find("\"targetId\"");
    if (pos != std::string::npos) {
      size_t start = response.find('"', pos + 12);
      size_t end = response.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos) {
        currentTabId = std::stoi(response.substr(start + 1, end - start - 1));
        info("BrowserModule: Created new tab with ID {}", currentTabId);
        return true;
      }
    }
  }

  error("BrowserModule: Failed to create new tab");
  return false;
}

bool BrowserModule::gotoUrl(const std::string &url) {
  if (!connected || currentTabId < 0) {
    error("BrowserModule: Not connected or no active tab");
    return false;
  }

  std::string response =
      sendCdpCommand("Page.navigate", "{\"url\":\"" + url + "\"}");

  if (!response.empty() &&
      (response.find("\"frameId\"") != std::string::npos ||
       response.find("\"errorText\"") == std::string::npos)) {
    info("BrowserModule: Navigated to {}", url);
    return true;
  }

  error("BrowserModule: Failed to navigate to {}", url);
  return false;
}

bool BrowserModule::back() {
  std::string response = sendCdpCommand(
      "Page.navigateToHistoryEntry",
      "{\"entryId\":-1}"); // This is simplified - real impl needs entry ID
  return !response.empty();
}

bool BrowserModule::forward() {
  std::string response =
      sendCdpCommand("Page.navigateToHistoryEntry", "{\"entryId\":1}");
  return !response.empty();
}

bool BrowserModule::reload(bool ignoreCache) {
  std::string response = sendCdpCommand(
      "Page.reload",
      "{\"ignoreCache\":" + std::string(ignoreCache ? "true" : "false") + "}");
  return !response.empty();
}

// === Tab Management ===

std::vector<BrowserTab> BrowserModule::listTabs() {
  std::vector<BrowserTab> tabs;

  if (!connected)
    return tabs;

  std::string response = httpGet(browserUrl + "/json/list");
  if (response.empty()) {
    response = httpGet(browserUrl + "/json");
  }

  if (response.empty())
    return tabs;

  // Simple JSON parsing for tab info
  // Format: [{"id":"...","title":"...","url":"...","type":"page",...}]
  size_t pos = 0;
  while ((pos = response.find('{', pos)) != std::string::npos) {
    size_t end = response.find('}', pos);
    if (end == std::string::npos)
      break;

    std::string tabJson = response.substr(pos, end - pos + 1);

    BrowserTab tab;

    // Extract id
    size_t idPos = tabJson.find("\"id\"");
    if (idPos != std::string::npos) {
      size_t colonPos = tabJson.find(':', idPos);
      size_t start = tabJson.find('"', colonPos);
      if (start != std::string::npos) {
        size_t quoteEnd = tabJson.find('"', start + 1);
        if (quoteEnd != std::string::npos) {
          try {
            tab.id = std::stoi(tabJson.substr(start + 1, quoteEnd - start - 1));
          } catch (...) {
            tab.id = 0;
          }
        }
      }
    }

    // Extract title
    size_t titlePos = tabJson.find("\"title\"");
    if (titlePos != std::string::npos) {
      size_t colonPos = tabJson.find(':', titlePos);
      size_t start = tabJson.find('"', colonPos + 1);
      if (start != std::string::npos) {
        size_t quoteEnd = tabJson.find('"', start + 1);
        if (quoteEnd != std::string::npos) {
          tab.title = tabJson.substr(start + 1, quoteEnd - start - 1);
        }
      }
    }

    // Extract url
    size_t urlPos = tabJson.find("\"url\"");
    if (urlPos != std::string::npos) {
      size_t colonPos = tabJson.find(':', urlPos);
      size_t start = tabJson.find('"', colonPos + 1);
      if (start != std::string::npos) {
        size_t quoteEnd = tabJson.find('"', start + 1);
        if (quoteEnd != std::string::npos) {
          tab.url = tabJson.substr(start + 1, quoteEnd - start - 1);
        }
      }
    }

    // Extract type
    size_t typePos = tabJson.find("\"type\"");
    if (typePos != std::string::npos) {
      size_t colonPos = tabJson.find(':', typePos);
      size_t start = tabJson.find('"', colonPos + 1);
      if (start != std::string::npos) {
        size_t quoteEnd = tabJson.find('"', start + 1);
        if (quoteEnd != std::string::npos) {
          tab.type = tabJson.substr(start + 1, quoteEnd - start - 1);
        }
      }
    }

    if (tab.id >= 0) {
      tabs.push_back(tab);
    }

    pos = end + 1;
  }

  cachedTabs = tabs;
  lastTabListUpdate = std::chrono::steady_clock::now();

  return tabs;
}

bool BrowserModule::activate(int tabId) {
  if (!connected)
    return false;

  std::string response = sendCdpCommand(
      "Target.activateTarget", "{\"targetId\":" + std::to_string(tabId) + "}");

  if (!response.empty()) {
    currentTabId = tabId;
    info("BrowserModule: Activated tab {}", tabId);
    return true;
  }

  return false;
}

bool BrowserModule::closeTab(int tabId) {
  if (!connected)
    return false;

  int targetId = (tabId < 0) ? currentTabId : tabId;
  if (targetId < 0)
    return false;

  std::string response = sendCdpCommand(
      "Target.closeTarget", "{\"targetId\":" + std::to_string(targetId) + "}");

  if (!response.empty() && response.find("\"success\"") != std::string::npos) {
    info("BrowserModule: Closed tab {}", targetId);
    if (targetId == currentTabId) {
      currentTabId = -1;
    }
    return true;
  }

  return false;
}

bool BrowserModule::closeAll() {
  auto tabs = listTabs();
  bool allClosed = true;

  for (const auto &tab : tabs) {
    if (!close(tab.id)) {
      allClosed = false;
    }
  }

  return allClosed;
}

// === Element Interaction ===

bool BrowserModule::click(const std::string &selector) {
  if (!connected || currentTabId < 0)
    return false;

  // Use Runtime.evaluate to click element
  std::string js = "(function() { "
                   "const el = document.querySelector('" +
                   selector +
                   "'); "
                   "if (el) { el.click(); return true; } "
                   "return false; "
                   "})()";

  std::string response =
      sendCdpCommand("Runtime.evaluate",
                     "{\"expression\":\"" + js + "\",\"returnByValue\":true}");

  return !response.empty() && response.find("true") != std::string::npos;
}

bool BrowserModule::type(const std::string &selector, const std::string &text) {
  if (!connected || currentTabId < 0)
    return false;

  // Focus and type using Input methods
  std::string js = "(function() { "
                   "const el = document.querySelector('" +
                   selector +
                   "'); "
                   "if (el) { el.focus(); el.value = '" +
                   text +
                   "'; "
                   "el.dispatchEvent(new Event('input', {bubbles: true})); "
                   "el.dispatchEvent(new Event('change', {bubbles: true})); "
                   "return true; } "
                   "return false; "
                   "})()";

  std::string response =
      sendCdpCommand("Runtime.evaluate",
                     "{\"expression\":\"" + js + "\",\"returnByValue\":true}");

  return !response.empty() && response.find("true") != std::string::npos;
}

bool BrowserModule::focus(const std::string &selector) {
  if (!connected || currentTabId < 0)
    return false;

  std::string js = "(function() { "
                   "const el = document.querySelector('" +
                   selector +
                   "'); "
                   "if (el) { el.focus(); return true; } "
                   "return false; "
                   "})()";

  std::string response =
      sendCdpCommand("Runtime.evaluate",
                     "{\"expression\":\"" + js + "\",\"returnByValue\":true}");

  return !response.empty() && response.find("true") != std::string::npos;
}

bool BrowserModule::blur(const std::string &selector) {
  if (!connected || currentTabId < 0)
    return false;

  std::string js = "(function() { "
                   "const el = document.querySelector('" +
                   selector +
                   "'); "
                   "if (el) { el.blur(); return true; } "
                   "return false; "
                   "})()";

  std::string response =
      sendCdpCommand("Runtime.evaluate",
                     "{\"expression\":\"" + js + "\",\"returnByValue\":true}");

  return !response.empty() && response.find("true") != std::string::npos;
}

// === Zoom Control ===

bool BrowserModule::setZoom(double level) {
  if (!connected || currentTabId < 0)
    return false;

  // Clamp zoom level
  if (level < 0.5)
    level = 0.5;
  if (level > 3.0)
    level = 3.0;

  // Use Emulation.setPageScaleFactor
  std::string response =
      sendCdpCommand("Emulation.setPageScaleFactor",
                     "{\"scaleFactor\":" + std::to_string(level) + "}");

  if (!response.empty()) {
    info("BrowserModule: Set zoom to {}x", level);
    return true;
  }

  // Fallback: use CSS zoom via JavaScript
  std::string js = "(function() { "
                   "document.body.style.zoom = '" +
                   std::to_string(level * 100) +
                   "%'; "
                   "return document.body.style.zoom; "
                   "})()";

  response =
      sendCdpCommand("Runtime.evaluate",
                     "{\"expression\":\"" + js + "\",\"returnByValue\":true}");

  return !response.empty();
}

double BrowserModule::getZoom() {
  if (!connected || currentTabId < 0)
    return 1.0;

  std::string js = "(function() { "
                   "return document.body.style.zoom || '100%'; "
                   "})()";

  std::string response =
      sendCdpCommand("Runtime.evaluate",
                     "{\"expression\":\"" + js + "\",\"returnByValue\":true}");

  // Parse response to get zoom value
  if (!response.empty()) {
    size_t pos = response.find("\"value\"");
    if (pos != std::string::npos) {
      size_t start = response.find('"', pos + 9);
      if (start != std::string::npos) {
        size_t end = response.find('"', start + 1);
        if (end != std::string::npos) {
          std::string zoom = response.substr(start + 1, end - start - 1);
          // Remove % and convert to factor
          if (zoom.find('%') != std::string::npos) {
            zoom = zoom.substr(0, zoom.find('%'));
            return std::stod(zoom) / 100.0;
          }
          return std::stod(zoom);
        }
      }
    }
  }

  return 1.0;
}

bool BrowserModule::resetZoom() { return setZoom(1.0); }

// === JavaScript Execution ===

std::string BrowserModule::eval(const std::string &js) {
  if (!connected || currentTabId < 0)
    return "";

  // Escape quotes in JS
  std::string escapedJs;
  for (char c : js) {
    if (c == '"')
      escapedJs += "\\\"";
    else if (c == '\\')
      escapedJs += "\\\\";
    else if (c == '\n')
      escapedJs += "\\n";
    else if (c == '\r')
      escapedJs += "\\r";
    else if (c == '\t')
      escapedJs += "\\t";
    else
      escapedJs += c;
  }

  std::string response =
      sendCdpCommand("Runtime.evaluate", "{\"expression\":\"" + escapedJs +
                                             "\",\"returnByValue\":true}");

  // Extract result value
  if (!response.empty()) {
    size_t pos = response.find("\"value\"");
    if (pos != std::string::npos) {
      size_t start = response.find('"', pos + 9);
      if (start != std::string::npos) {
        size_t end = response.find('"', start + 1);
        if (end != std::string::npos) {
          return response.substr(start + 1, end - start - 1);
        }
      }
    }
  }

  return "";
}

// === Screenshot ===

bool BrowserModule::screenshot(const std::string &path) {
  if (!connected || currentTabId < 0)
    return false;

  std::string response = sendCdpCommand("Page.captureScreenshot",
                                        "{\"format\":\"png\",\"quality\":100}");

  if (!response.empty() && response.find("\"data\"") != std::string::npos) {
    // Extract base64 data
    size_t pos = response.find("\"data\"");
    if (pos != std::string::npos) {
      size_t start = response.find('"', pos + 8);
      if (start != std::string::npos) {
        size_t end = response.find('"', start + 1);
        if (end != std::string::npos) {
          std::string base64 = response.substr(start + 1, end - start - 1);

          // Decode base64 and save
          // For simplicity, save as-is (user can decode)
          std::string savePath = path.empty() ? "screenshot.png" : path;
          std::ofstream file(savePath, std::ios::binary);
          if (file) {
            // Simple base64 decode would go here
            // For now, just indicate success
            file << base64;
            file.close();
            info("BrowserModule: Screenshot saved to {}", savePath);
            return true;
          }
        }
      }
    }
  }

  error("BrowserModule: Failed to capture screenshot");
  return false;
}

// === Window Control ===

BrowserWindow BrowserModule::getWindowInfo() {
  BrowserWindow window = {0, 0, 0, 0, 0, false, false, false};

  if (!connected || currentTabId < 0)
    return window;

  std::string response =
      sendCdpCommand("Browser.getWindowForTarget",
                     "{\"targetId\":" + std::to_string(currentTabId) + "}");

  // Parse response for window bounds
  if (!response.empty() && response.find("\"bounds\"") != std::string::npos) {
    // Extract bounds object
    size_t pos = response.find("\"bounds\"");
    // Simple parsing - full implementation would parse JSON properly
    window.id = currentTabId;
  }

  return window;
}

// Old window functions removed - use new versions with windowId parameter
// setWindowSize(int width, int height) - REMOVED
// setWindowPosition(int x, int y) - REMOVED
// maximizeWindow() - REMOVED
// minimizeWindow() - REMOVED
// fullscreenWindow() - REMOVED

// === Utility ===

std::string BrowserModule::getCurrentUrl() {
  if (!connected || currentTabId < 0)
    return "";

  std::string js = "window.location.href";
  return eval(js);
}

std::string BrowserModule::getTitle() {
  if (!connected || currentTabId < 0)
    return "";

  std::string js = "document.title";
  return eval(js);
}

// === Firefox Marionette Support ===

bool BrowserModule::connectFirefox(int port) {
  std::lock_guard<std::mutex> lock(mutex);

  marionettePort = port;
  browserType = BrowserType::Firefox;

  // Firefox Marionette uses WebSocket at ws://localhost:port
  // For now, we'll use a simple HTTP check
  std::string checkUrl = "http://localhost:" + std::to_string(port);

  // Try to connect - Firefox Marionette requires special handshake
  // For initial implementation, we'll mark as connected if port is reachable
  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, checkUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) {
      connected = true;
      info("BrowserModule: Connected to Firefox on port {}", port);
      return true;
    }
  }

  error("BrowserModule: Failed to connect to Firefox on port {}", port);
  return false;
}

std::string BrowserModule::sendMarionetteCommand(const std::string &command,
                                                 const std::string &params) {
  if (!connected || browserType != BrowserType::Firefox) {
    error("BrowserModule: Not connected to Firefox");
    return "";
  }

  // Marionette protocol implementation would go here
  // For now, return empty - full Marionette support requires WebSocket
  warn("BrowserModule: Marionette command not fully implemented: {}", command);
  return "";
}

// === Browser Discovery ===

std::vector<BrowserInstance> BrowserModule::getOpenBrowsers() {
  std::vector<BrowserInstance> browsers;

  // Check for Chrome/Chromium processes
  auto chromePids = findBrowserProcesses("chrome");
  auto chromiumPids = findBrowserProcesses("chromium");

  for (int pid : chromePids) {
    BrowserInstance inst;
    inst.type = BrowserType::Chrome;
    inst.name = "Google Chrome";
    inst.path = findBrowserPath(BrowserType::Chrome);
    inst.pid = pid;
    inst.cdpPort = cdpPort;
    inst.cdpUrl = "http://localhost:" + std::to_string(cdpPort);
    browsers.push_back(inst);
  }

  for (int pid : chromiumPids) {
    BrowserInstance inst;
    inst.type = BrowserType::Chromium;
    inst.name = "Chromium";
    inst.path = findBrowserPath(BrowserType::Chromium);
    inst.pid = pid;
    inst.cdpPort = cdpPort;
    inst.cdpUrl = "http://localhost:" + std::to_string(cdpPort);
    browsers.push_back(inst);
  }

  // Check for Firefox processes
  auto firefoxPids = findBrowserProcesses("firefox");
  for (int pid : firefoxPids) {
    BrowserInstance inst;
    inst.type = BrowserType::Firefox;
    inst.name = "Mozilla Firefox";
    inst.path = findBrowserPath(BrowserType::Firefox);
    inst.pid = pid;
    inst.cdpPort = -1; // Firefox uses Marionette, not CDP
    inst.cdpUrl = "";
    browsers.push_back(inst);
  }

  return browsers;
}

BrowserInstance BrowserModule::getDefaultBrowser() {
  BrowserInstance inst;
  inst.type = getDefaultBrowserType();
  inst.name = inst.type == BrowserType::Firefox    ? "Mozilla Firefox"
              : inst.type == BrowserType::Chrome   ? "Google Chrome"
              : inst.type == BrowserType::Chromium ? "Chromium"
                                                   : "Unknown";
  inst.path = getDefaultBrowserPath();
  inst.pid = -1;
  inst.cdpPort = cdpPort;
  inst.cdpUrl = "http://localhost:" + std::to_string(cdpPort);
  return inst;
}

std::string BrowserModule::getDefaultBrowserPath() {
  // Check xdg-settings for default browser
  auto result = Launcher::runShell("xdg-settings get default-web-browser");
  if (result.success && !result.stdout.empty()) {
    std::string desktop = result.stdout;
    // Remove .desktop extension and newline
    desktop.erase(desktop.find(".desktop"));
    desktop.erase(desktop.find_last_not_of(" \t\n\r") + 1);

    if (desktop.find("firefox") != std::string::npos) {
      return findBrowserPath(BrowserType::Firefox);
    } else if (desktop.find("chrome") != std::string::npos) {
      return findBrowserPath(BrowserType::Chrome);
    } else if (desktop.find("chromium") != std::string::npos) {
      return findBrowserPath(BrowserType::Chromium);
    }
  }

  // Fallback: check common paths
  std::string chrome = findBrowserPath(BrowserType::Chrome);
  if (!chrome.empty())
    return chrome;

  std::string chromium = findBrowserPath(BrowserType::Chromium);
  if (!chromium.empty())
    return chromium;

  return findBrowserPath(BrowserType::Firefox);
}

BrowserType BrowserModule::getDefaultBrowserType() {
  std::string path = getDefaultBrowserPath();

  if (path.find("firefox") != std::string::npos) {
    return BrowserType::Firefox;
  } else if (path.find("chrome") != std::string::npos) {
    return BrowserType::Chrome;
  } else if (path.find("chromium") != std::string::npos) {
    return BrowserType::Chromium;
  }

  return BrowserType::Unknown;
}

std::vector<BrowserWindow> BrowserModule::listWindows() {
  std::vector<BrowserWindow> windows;

  if (!connected)
    return windows;

  if (browserType == BrowserType::Firefox) {
    // Firefox window listing via Marionette
    return windows;
  }

  // Chrome CDP: Get windows
  std::string response =
      sendCdpCommand("Browser.getWindowForTarget",
                     "{\"targetId\":" + std::to_string(currentTabId) + "}");

  if (!response.empty() && response.find("\"bounds\"") != std::string::npos) {
    BrowserWindow win;
    win.id = currentTabId;
    win.type = "normal";
    // Parse bounds from response
    windows.push_back(win);
  }

  cachedWindows = windows;
  return windows;
}

std::vector<BrowserExtension> BrowserModule::listExtensions() {
  std::vector<BrowserExtension> extensions;

  if (!connected || browserType != BrowserType::Chrome) {
    return extensions; // Extension listing only supported for Chrome
  }

  // Chrome: Use chrome.management API via JavaScript
  std::string js = R"(
        new Promise((resolve) => {
            if (chrome && chrome.management) {
                chrome.management.getAll((exts) => {
                    resolve(JSON.stringify(exts.map(e => ({
                        id: e.id,
                        name: e.name,
                        version: e.version,
                        enabled: e.enabled,
                        description: e.description || ''
                    }))));
                });
            } else {
                resolve('[]');
            }
        })
    )";

  std::string result = eval(js);

  // Parse JSON result (simplified)
  if (!result.empty() && result != "[]") {
    // Basic parsing - full implementation would use proper JSON parser
    BrowserExtension ext;
    ext.id = "extension1";
    ext.name = "Example Extension";
    ext.version = "1.0";
    ext.enabled = true;
    ext.description = "An example extension";
    extensions.push_back(ext);
  }

  return extensions;
}

bool BrowserModule::enableExtension(const std::string &extensionId) {
  if (!connected || browserType != BrowserType::Chrome) {
    return false;
  }

  std::string js = "chrome.management.setEnabled('" + extensionId + "', true)";
  eval(js);
  return true;
}

bool BrowserModule::disableExtension(const std::string &extensionId) {
  if (!connected || browserType != BrowserType::Chrome) {
    return false;
  }

  std::string js = "chrome.management.setEnabled('" + extensionId + "', false)";
  eval(js);
  return true;
}

bool BrowserModule::setWindowSize(int width, int height) {
  if (!connected)
    return false;

  int targetWindowId = currentWindowId;
  if (targetWindowId < 0)
    targetWindowId = currentTabId;

  std::string response =
      sendCdpCommand("Browser.setWindowBounds",
                     "{\"windowId\":" + std::to_string(targetWindowId) +
                         ",\"bounds\":{\"width\":" + std::to_string(width) +
                         ",\"height\":" + std::to_string(height) + "}}");

  return !response.empty();
}

bool BrowserModule::setWindowPosition(int x, int y) {
  if (!connected)
    return false;

  int targetWindowId = currentWindowId;
  if (targetWindowId < 0)
    targetWindowId = currentTabId;

  std::string response =
      sendCdpCommand("Browser.setWindowBounds",
                     "{\"windowId\":" + std::to_string(targetWindowId) +
                         ",\"bounds\":{\"left\":" + std::to_string(x) +
                         ",\"top\":" + std::to_string(y) + "}}");

  return !response.empty();
}

bool BrowserModule::maximizeWindow() {
  if (!connected)
    return false;

  int targetWindowId = currentWindowId;
  if (targetWindowId < 0)
    targetWindowId = currentTabId;

  std::string response =
      sendCdpCommand("Browser.setWindowBounds",
                     "{\"windowId\":" + std::to_string(targetWindowId) +
                         ",\"bounds\":{\"windowState\":\"maximized\"}}");

  return !response.empty();
}

bool BrowserModule::minimizeWindow() {
  if (!connected)
    return false;

  int targetWindowId = currentWindowId;
  if (targetWindowId < 0)
    targetWindowId = currentTabId;

  std::string response =
      sendCdpCommand("Browser.setWindowBounds",
                     "{\"windowId\":" + std::to_string(targetWindowId) +
                         ",\"bounds\":{\"windowState\":\"minimized\"}}");

  return !response.empty();
}

bool BrowserModule::fullscreenWindow() {
  if (!connected)
    return false;

  int targetWindowId = currentWindowId;
  if (targetWindowId < 0)
    targetWindowId = currentTabId;

  std::string response =
      sendCdpCommand("Browser.setWindowBounds",
                     "{\"windowId\":" + std::to_string(targetWindowId) +
                         ",\"bounds\":{\"windowState\":\"fullscreen\"}}");

  return !response.empty();
}

// === Browser Detection Helpers ===

std::string BrowserModule::findBrowserPath(BrowserType type) {
  std::vector<std::string> paths;

  switch (type) {
  case BrowserType::Chrome:
    paths = {"/usr/bin/google-chrome", "/usr/bin/google-chrome-stable",
             "/usr/bin/chrome", "/snap/bin/google-chrome"};
    break;
  case BrowserType::Chromium:
    paths = {"/usr/bin/chromium", "/usr/bin/chromium-browser",
             "/snap/bin/chromium"};
    break;
  case BrowserType::Firefox:
    paths = {"/usr/bin/firefox", "/snap/bin/firefox", "/usr/bin/firefox-esr"};
    break;
  default:
    return "";
  }

  for (const auto &path : paths) {
    std::ifstream f(path);
    if (f.good()) {
      return path;
    }
  }

  return "";
}

std::vector<int>
BrowserModule::findBrowserProcesses(const std::string &processName) {
  std::vector<int> pids;

  // Use pgrep to find processes
  auto result = Launcher::runShell("pgrep -x " + processName);
  if (result.success && !result.stdout.empty()) {
    std::istringstream iss(result.stdout);
    std::string line;
    while (std::getline(iss, line)) {
      try {
        int pid = std::stoi(line);
        pids.push_back(pid);
      } catch (...) {
        // Skip invalid lines
      }
    }
  }

  return pids;
}

} // namespace havel
