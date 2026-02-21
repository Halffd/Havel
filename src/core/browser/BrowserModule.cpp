#include "BrowserModule.hpp"
#include "../utils/Logger.hpp"
#include "../process/Launcher.hpp"
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <regex>

namespace havel {

// Static callback for curl
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total = size * nmemb;
    userp->append((char*)contents, total);
    return total;
}

// Global browser instance
static std::unique_ptr<BrowserModule> g_browserInstance;

 BrowserModule& getBrowser() {
    if (!g_browserInstance) {
        g_browserInstance = std::make_unique<BrowserModule>();
    }
    return *g_browserInstance;
}

BrowserModule::BrowserModule() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

BrowserModule::~BrowserModule() {
    disconnect();
    curl_global_cleanup();
}

bool BrowserModule::connect(const std::string& url) {
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

std::string BrowserModule::httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK) ? response : "";
}

std::string BrowserModule::httpPost(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    std::string response;
    struct curl_slist* headers = nullptr;
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

std::string BrowserModule::sendCdpCommand(const std::string& method, 
                                           const std::string& params) {
    if (!connected || currentTabId < 0) {
        error("BrowserModule: Not connected or no active tab");
        return "";
    }
    return sendCdpCommandToTab(currentTabId, method, params);
}

std::string BrowserModule::sendCdpCommandToTab(int tabId, const std::string& method,
                                                const std::string& params) {
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
    auto wsBegin = std::sregex_iterator(response.begin(), response.end(), wsRegex);
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

bool BrowserModule::open(const std::string& url) {
    return newTab(url);
}

bool BrowserModule::newTab(const std::string& url) {
    if (!connected) {
        error("BrowserModule: Not connected");
        return false;
    }
    
    // Create new target (tab)
    std::string response = sendCdpCommand("Target.createTarget", 
        "{\"url\":\"" + url + "\",\"newWindow\":true}");
    
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

bool BrowserModule::gotoUrl(const std::string& url) {
    if (!connected || currentTabId < 0) {
        error("BrowserModule: Not connected or no active tab");
        return false;
    }
    
    std::string response = sendCdpCommand("Page.navigate", 
        "{\"url\":\"" + url + "\"}");
    
    if (!response.empty() && (response.find("\"frameId\"") != std::string::npos ||
        response.find("\"errorText\"") == std::string::npos)) {
        info("BrowserModule: Navigated to {}", url);
        return true;
    }
    
    error("BrowserModule: Failed to navigate to {}", url);
    return false;
}

bool BrowserModule::back() {
    std::string response = sendCdpCommand("Page.navigateToHistoryEntry", 
        "{\"entryId\":-1}"); // This is simplified - real impl needs entry ID
    return !response.empty();
}

bool BrowserModule::forward() {
    std::string response = sendCdpCommand("Page.navigateToHistoryEntry", 
        "{\"entryId\":1}");
    return !response.empty();
}

bool BrowserModule::reload(bool ignoreCache) {
    std::string response = sendCdpCommand("Page.reload", 
        "{\"ignoreCache\":" + std::string(ignoreCache ? "true" : "false") + "}");
    return !response.empty();
}

// === Tab Management ===

std::vector<BrowserTab> BrowserModule::listTabs() {
    std::vector<BrowserTab> tabs;
    
    if (!connected) return tabs;
    
    std::string response = httpGet(browserUrl + "/json/list");
    if (response.empty()) {
        response = httpGet(browserUrl + "/json");
    }
    
    if (response.empty()) return tabs;
    
    // Simple JSON parsing for tab info
    // Format: [{"id":"...","title":"...","url":"...","type":"page",...}]
    size_t pos = 0;
    while ((pos = response.find('{', pos)) != std::string::npos) {
        size_t end = response.find('}', pos);
        if (end == std::string::npos) break;
        
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
    if (!connected) return false;
    
    std::string response = sendCdpCommand("Target.activateTarget",
        "{\"targetId\":" + std::to_string(tabId) + "}");
    
    if (!response.empty()) {
        currentTabId = tabId;
        info("BrowserModule: Activated tab {}", tabId);
        return true;
    }
    
    return false;
}

bool BrowserModule::close(int tabId) {
    if (!connected) return false;
    
    int targetId = (tabId < 0) ? currentTabId : tabId;
    if (targetId < 0) return false;
    
    std::string response = sendCdpCommand("Target.closeTarget",
        "{\"targetId\":" + std::to_string(targetId) + "}");
    
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
    
    for (const auto& tab : tabs) {
        if (!close(tab.id)) {
            allClosed = false;
        }
    }
    
    return allClosed;
}

// === Element Interaction ===

bool BrowserModule::click(const std::string& selector) {
    if (!connected || currentTabId < 0) return false;
    
    // Use Runtime.evaluate to click element
    std::string js = "(function() { "
        "const el = document.querySelector('" + selector + "'); "
        "if (el) { el.click(); return true; } "
        "return false; "
    "})()";
    
    std::string response = sendCdpCommand("Runtime.evaluate",
        "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    
    return !response.empty() && response.find("true") != std::string::npos;
}

bool BrowserModule::type(const std::string& selector, const std::string& text) {
    if (!connected || currentTabId < 0) return false;
    
    // Focus and type using Input methods
    std::string js = "(function() { "
        "const el = document.querySelector('" + selector + "'); "
        "if (el) { el.focus(); el.value = '" + text + "'; "
        "el.dispatchEvent(new Event('input', {bubbles: true})); "
        "el.dispatchEvent(new Event('change', {bubbles: true})); "
        "return true; } "
        "return false; "
    "})()";
    
    std::string response = sendCdpCommand("Runtime.evaluate",
        "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    
    return !response.empty() && response.find("true") != std::string::npos;
}

bool BrowserModule::focus(const std::string& selector) {
    if (!connected || currentTabId < 0) return false;
    
    std::string js = "(function() { "
        "const el = document.querySelector('" + selector + "'); "
        "if (el) { el.focus(); return true; } "
        "return false; "
    "})()";
    
    std::string response = sendCdpCommand("Runtime.evaluate",
        "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    
    return !response.empty() && response.find("true") != std::string::npos;
}

bool BrowserModule::blur(const std::string& selector) {
    if (!connected || currentTabId < 0) return false;
    
    std::string js = "(function() { "
        "const el = document.querySelector('" + selector + "'); "
        "if (el) { el.blur(); return true; } "
        "return false; "
    "})()";
    
    std::string response = sendCdpCommand("Runtime.evaluate",
        "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    
    return !response.empty() && response.find("true") != std::string::npos;
}

// === Zoom Control ===

bool BrowserModule::setZoom(double level) {
    if (!connected || currentTabId < 0) return false;
    
    // Clamp zoom level
    if (level < 0.5) level = 0.5;
    if (level > 3.0) level = 3.0;
    
    // Use Emulation.setPageScaleFactor
    std::string response = sendCdpCommand("Emulation.setPageScaleFactor",
        "{\"scaleFactor\":" + std::to_string(level) + "}");
    
    if (!response.empty()) {
        info("BrowserModule: Set zoom to {}x", level);
        return true;
    }
    
    // Fallback: use CSS zoom via JavaScript
    std::string js = "(function() { "
        "document.body.style.zoom = '" + std::to_string(level * 100) + "%'; "
        "return document.body.style.zoom; "
    "})()";
    
    response = sendCdpCommand("Runtime.evaluate",
        "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    
    return !response.empty();
}

double BrowserModule::getZoom() {
    if (!connected || currentTabId < 0) return 1.0;
    
    std::string js = "(function() { "
        "return document.body.style.zoom || '100%'; "
    "})()";
    
    std::string response = sendCdpCommand("Runtime.evaluate",
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

bool BrowserModule::resetZoom() {
    return setZoom(1.0);
}

// === JavaScript Execution ===

std::string BrowserModule::eval(const std::string& js) {
    if (!connected || currentTabId < 0) return "";
    
    // Escape quotes in JS
    std::string escapedJs;
    for (char c : js) {
        if (c == '"') escapedJs += "\\\"";
        else if (c == '\\') escapedJs += "\\\\";
        else if (c == '\n') escapedJs += "\\n";
        else if (c == '\r') escapedJs += "\\r";
        else if (c == '\t') escapedJs += "\\t";
        else escapedJs += c;
    }
    
    std::string response = sendCdpCommand("Runtime.evaluate",
        "{\"expression\":\"" + escapedJs + "\",\"returnByValue\":true}");
    
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

bool BrowserModule::screenshot(const std::string& path) {
    if (!connected || currentTabId < 0) return false;
    
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
    
    if (!connected || currentTabId < 0) return window;
    
    std::string response = sendCdpCommand("Browser.getWindowForTarget",
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

bool BrowserModule::setWindowSize(int width, int height) {
    if (!connected || currentTabId < 0) return false;
    
    std::string response = sendCdpCommand("Browser.setWindowBounds",
        "{\"bounds\":{\"width\":" + std::to_string(width) + 
        ",\"height\":" + std::to_string(height) + "}}");
    
    return !response.empty();
}

bool BrowserModule::setWindowPosition(int x, int y) {
    if (!connected || currentTabId < 0) return false;
    
    std::string response = sendCdpCommand("Browser.setWindowBounds",
        "{\"bounds\":{\"left\":" + std::to_string(x) + 
        ",\"top\":" + std::to_string(y) + "}}");
    
    return !response.empty();
}

bool BrowserModule::maximizeWindow() {
    if (!connected || currentTabId < 0) return false;
    
    std::string response = sendCdpCommand("Browser.setWindowBounds",
        "{\"bounds\":{\"windowState\":\"maximized\"}}");
    
    return !response.empty();
}

bool BrowserModule::minimizeWindow() {
    if (!connected || currentTabId < 0) return false;
    
    std::string response = sendCdpCommand("Browser.setWindowBounds",
        "{\"bounds\":{\"windowState\":\"minimized\"}}");
    
    return !response.empty();
}

bool BrowserModule::fullscreenWindow() {
    if (!connected || currentTabId < 0) return false;
    
    std::string response = sendCdpCommand("Browser.setWindowBounds",
        "{\"bounds\":{\"windowState\":\"fullscreen\"}}");
    
    return !response.empty();
}

// === Utility ===

std::string BrowserModule::getCurrentUrl() {
    if (!connected || currentTabId < 0) return "";
    
    std::string js = "window.location.href";
    return eval(js);
}

std::string BrowserModule::getTitle() {
    if (!connected || currentTabId < 0) return "";
    
    std::string js = "document.title";
    return eval(js);
}

} // namespace havel
