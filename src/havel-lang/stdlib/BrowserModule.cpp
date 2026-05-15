#include "BrowserModule.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <regex>
#include <set>
#include <cstdlib>
#include <array>

using havel::compiler::Value;
using havel::compiler::VMApi;
using json = nlohmann::json;

namespace havel::stdlib {

static size_t curlWriteCb(void *ptr, size_t size, size_t nmemb, std::string *s) {
  s->append((char *)ptr, size * nmemb);
  return size * nmemb;
}

static bool curlInitialized = false;
static void ensureCurl() {
  if (!curlInitialized) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curlInitialized = true;
  }
}

static std::string httpGet(const std::string &url, int timeoutMs = 2000) {
  ensureCurl();
  CURL *curl = curl_easy_init();
  if (!curl) return "";
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs / 2);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  return (res == CURLE_OK) ? response : "";
}

static std::string httpPost(const std::string &url, const std::string &body, int timeoutMs = 5000) {
  ensureCurl();
  CURL *curl = curl_easy_init();
  if (!curl) return "";
  std::string response;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return (res == CURLE_OK) ? response : "";
}

static std::string runShell(const std::string &cmd) {
  std::array<char, 4096> buf;
  std::string result;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) return "";
  while (fgets(buf.data(), buf.size(), pipe)) result += buf.data();
  pclose(pipe);
  return result;
}

struct CdpState {
  std::mutex mutex;
  bool connected = false;
  std::string browserUrl = "http://localhost:9222";
  int cdpPort = 9222;
  int currentTabIndex = 0;
  std::vector<json> cachedTabs;
  std::chrono::steady_clock::time_point lastTabRefresh;
};

static CdpState &cdpState() {
  static CdpState state;
  return state;
}

static std::vector<json> fetchTabs() {
  auto &st = cdpState();
  std::string response = httpGet(st.browserUrl + "/json/list");
  if (response.empty()) response = httpGet(st.browserUrl + "/json");
  if (response.empty()) return {};
  try {
    return json::parse(response);
  } catch (...) {
    return {};
  }
}

static std::vector<json> getTabs(bool forceRefresh = false) {
  auto &st = cdpState();
  auto now = std::chrono::steady_clock::now();
  if (!forceRefresh && !st.cachedTabs.empty() &&
      std::chrono::duration_cast<std::chrono::seconds>(now - st.lastTabRefresh).count() < 5) {
    return st.cachedTabs;
  }
  auto tabs = fetchTabs();
  std::lock_guard<std::mutex> lock(st.mutex);
  st.cachedTabs = tabs;
  st.lastTabRefresh = now;
  return tabs;
}

static std::string getWsUrl(int tabIndex) {
  auto tabs = getTabs();
  if (tabs.empty()) return "";
  int idx = (tabIndex < 0 || tabIndex >= (int)tabs.size()) ? 0 : tabIndex;
  const auto &tab = tabs[idx];
  if (tab.contains("webSocketDebuggerUrl") && tab["webSocketDebuggerUrl"].is_string()) {
    return tab["webSocketDebuggerUrl"].get<std::string>();
  }
  if (tab.contains("id") && tab["id"].is_string()) {
    std::string id = tab["id"].get<std::string>();
    return "ws://localhost:" + std::to_string(cdpState().cdpPort) + "/devtools/page/" + id;
  }
  return "";
}

static std::atomic<int> nextCdpId{1};

static std::string sendCdpWs(const std::string &wsUrl, const std::string &method, const std::string &params = "{}") {
  int msgId = nextCdpId++;
  json msg = {{"id", msgId}, {"method", method}};
  try { msg["params"] = json::parse(params); } catch (...) { msg["params"] = json::object(); }
  std::string message = msg.dump();

  std::string tempFile = "/tmp/cdp_msg_" + std::to_string(msgId) + ".json";
  {
    std::ofstream file(tempFile);
    if (!file) return "";
    file << message;
  }

  std::string cmd = "websocat --one-message '" + wsUrl + "' < '" + tempFile + "' 2>/dev/null";
  auto result = runShell(cmd);
  std::remove(tempFile.c_str());
  return result;
}

static std::string sendCdp(const std::string &method, const std::string &params = "{}") {
  auto &st = cdpState();
  if (!st.connected) return "";
  std::string wsUrl = getWsUrl(st.currentTabIndex);
  if (wsUrl.empty()) return "";
  return sendCdpWs(wsUrl, method, params);
}

static std::string escapeJs(const std::string &js) {
  std::string out;
  out.reserve(js.size());
  for (char c : js) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

static std::string findBrowserPath() {
  for (auto &p : {"/usr/bin/google-chrome", "/usr/bin/google-chrome-stable",
                   "/usr/bin/chromium", "/usr/bin/chromium-browser",
                   "/usr/bin/firefox", "/snap/bin/chromium"}) {
    std::ifstream f(p);
    if (f.good()) return p;
  }
  return "";
}

static Value makeTabObj(const json &tab, const VMApi &api) {
  auto obj = api.makeObject();
  std::string id, title, url, type, wsUrl;
  if (tab.contains("id") && tab["id"].is_string()) id = tab["id"].get<std::string>();
  if (tab.contains("title") && tab["title"].is_string()) title = tab["title"].get<std::string>();
  if (tab.contains("url") && tab["url"].is_string()) url = tab["url"].get<std::string>();
  if (tab.contains("type") && tab["type"].is_string()) type = tab["type"].get<std::string>();
  if (tab.contains("webSocketDebuggerUrl") && tab["webSocketDebuggerUrl"].is_string())
    wsUrl = tab["webSocketDebuggerUrl"].get<std::string>();
  api.setField(obj, "id", api.makeString(id));
  api.setField(obj, "title", api.makeString(title));
  api.setField(obj, "url", api.makeString(url));
  api.setField(obj, "type", api.makeString(type));
  api.setField(obj, "wsUrl", api.makeString(wsUrl));
  return obj;
}

void registerBrowserModule(const VMApi &api) {
  api.registerFunction("browser.connect", [api](const std::vector<Value> &args) {
    auto &st = cdpState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.browserUrl = "http://localhost:9222";
    if (!args.empty()) st.browserUrl = api.toString(args[0]);
    auto tabs = fetchTabs();
    if (tabs.empty()) {
      st.connected = false;
      return Value::makeBool(false);
    }
    st.connected = true;
    st.cachedTabs = tabs;
    st.lastTabRefresh = std::chrono::steady_clock::now();
    st.currentTabIndex = 0;
    return Value::makeBool(true);
  });

  api.registerFunction("browser.disconnect", [](const std::vector<Value> &) {
    auto &st = cdpState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.connected = false;
    st.currentTabIndex = 0;
    st.cachedTabs.clear();
    return Value::makeBool(true);
  });

  api.registerFunction("browser.isConnected", [](const std::vector<Value> &) {
    return Value::makeBool(cdpState().connected);
  });

  api.registerFunction("browser.goto", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.goto requires a url");
    std::string url = api.toString(args[0]);
    std::string response = sendCdp("Page.navigate", "{\"url\":\"" + url + "\"}");
    return Value::makeBool(!response.empty() && response.find("\"errorText\"") == std::string::npos);
  });

  api.registerFunction("browser.open", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.open requires a url");
    std::string url = api.toString(args[0]);
    std::string response = sendCdp("Target.createTarget", "{\"url\":\"" + url + "\",\"newWindow\":true}");
    return Value::makeBool(!response.empty() && response.find("\"targetId\"") != std::string::npos);
  });

  api.registerFunction("browser.newTab", [api](const std::vector<Value> &args) {
    std::string url;
    if (!args.empty()) url = api.toString(args[0]);
    std::string response = sendCdp("Target.createTarget", "{\"url\":\"" + url + "\",\"newWindow\":true}");
    return Value::makeBool(!response.empty() && response.find("\"targetId\"") != std::string::npos);
  });

  api.registerFunction("browser.back", [](const std::vector<Value> &) {
    std::string response = sendCdp("Page.navigateToHistoryEntry", "{\"entryId\":-1}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.forward", [](const std::vector<Value> &) {
    std::string response = sendCdp("Page.navigateToHistoryEntry", "{\"entryId\":1}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.reload", [](const std::vector<Value> &args) {
    bool ignoreCache = false;
    if (!args.empty() && args[0].isBool()) ignoreCache = args[0].asBool();
    std::string response = sendCdp("Page.reload",
      "{\"ignoreCache\":" + std::string(ignoreCache ? "true" : "false") + "}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.listTabs", [api](const std::vector<Value> &) {
    auto tabs = getTabs(true);
    auto arr = api.makeArray();
    for (auto &tab : tabs) {
      api.push(arr, makeTabObj(tab, api));
    }
    return arr;
  });

  api.registerFunction("browser.activate", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.activate requires a tab id or index");
    auto &st = cdpState();
    int idx = 0;
    if (args[0].isInt()) {
      idx = static_cast<int>(args[0].asInt());
    } else {
      std::string idStr = api.toString(args[0]);
      auto tabs = getTabs();
      for (int i = 0; i < (int)tabs.size(); i++) {
        if (tabs[i].contains("id") && tabs[i]["id"].is_string() &&
            tabs[i]["id"].get<std::string>() == idStr) {
          idx = i;
          break;
        }
      }
    }
    std::lock_guard<std::mutex> lock(st.mutex);
    st.currentTabIndex = idx;
    std::string targetId;
    auto tabs = getTabs();
    if (idx >= 0 && idx < (int)tabs.size() && tabs[idx].contains("id"))
      targetId = tabs[idx]["id"].get<std::string>();
    if (!targetId.empty()) {
      sendCdp("Target.activateTarget", "{\"targetId\":\"" + targetId + "\"}");
    }
    return Value::makeBool(true);
  });

  api.registerFunction("browser.closeTab", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.closeTab requires a tab id");
    std::string targetId;
    if (args[0].isStringId()) {
      targetId = api.toString(args[0]);
    } else {
      int idx = static_cast<int>(args[0].asInt());
      auto tabs = getTabs();
      if (idx >= 0 && idx < (int)tabs.size() && tabs[idx].contains("id"))
        targetId = tabs[idx]["id"].get<std::string>();
    }
    if (targetId.empty()) return Value::makeBool(false);
    std::string response = sendCdp("Target.closeTarget", "{\"targetId\":\"" + targetId + "\"}");
    return Value::makeBool(!response.empty() && response.find("\"success\":true") != std::string::npos);
  });

  api.registerFunction("browser.closeAll", [](const std::vector<Value> &) {
    auto tabs = getTabs(true);
    bool allOk = true;
    for (auto &tab : tabs) {
      if (tab.contains("id") && tab["id"].is_string()) {
        std::string targetId = tab["id"].get<std::string>();
        std::string response = sendCdp("Target.closeTarget", "{\"targetId\":\"" + targetId + "\"}");
        if (response.empty() || response.find("\"success\":true") == std::string::npos)
          allOk = false;
      }
    }
    return Value::makeBool(allOk);
  });

  api.registerFunction("browser.eval", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.eval requires javascript code");
    std::string js = escapeJs(api.toString(args[0]));
    std::string fullJs = "JSON.stringify(" + js + ")";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + fullJs + "\",\"returnByValue\":true}");
    if (response.empty()) return api.makeString("");
    try {
      auto j = json::parse(response);
      if (j.contains("result") && j["result"].contains("result") &&
          j["result"]["result"].contains("value")) {
        std::string val = j["result"]["result"]["value"].get<std::string>();
        auto parsed = json::parse(val);
        if (parsed.is_string()) return api.makeString(parsed.get<std::string>());
        return api.makeString(parsed.dump());
      }
    } catch (...) {}
    return api.makeString(response);
  });

  api.registerFunction("browser.click", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.click requires a selector");
    std::string sel = escapeJs(api.toString(args[0]));
    std::string js = "(function(){try{var el=document.querySelector('" + sel +
      "');if(el){el.click();return{success:true}}return{success:false,error:'not found'}}"
      "catch(e){return{success:false,error:e.message}}})()";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    return Value::makeBool(!response.empty() && response.find("\"success\":true") != std::string::npos);
  });

  api.registerFunction("browser.type", [api](const std::vector<Value> &args) {
    if (args.size() < 2) throw std::runtime_error("browser.type requires selector and text");
    std::string sel = escapeJs(api.toString(args[0]));
    std::string text = escapeJs(api.toString(args[1]));
    std::string js = "(function(){var el=document.querySelector('" + sel +
      "');if(el){el.focus();el.value='" + text +
      "';el.dispatchEvent(new Event('input',{bubbles:true}));"
      "el.dispatchEvent(new Event('change',{bubbles:true}));return true}return false})()";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    return Value::makeBool(!response.empty() && response.find("true") != std::string::npos);
  });

  api.registerFunction("browser.focus", [api](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.focus requires a selector");
    std::string sel = escapeJs(api.toString(args[0]));
    std::string js = "(function(){var el=document.querySelector('" + sel +
      "');if(el){el.focus();return true}return false})()";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    return Value::makeBool(!response.empty() && response.find("true") != std::string::npos);
  });

  api.registerFunction("browser.screenshot", [api](const std::vector<Value> &args) {
    std::string path = "screenshot.png";
    if (!args.empty()) path = api.toString(args[0]);
    std::string response = sendCdp("Page.captureScreenshot", "{\"format\":\"png\"}");
    if (response.empty() || response.find("\"data\"") == std::string::npos)
      return Value::makeBool(false);
    try {
      auto j = json::parse(response);
      if (j.contains("result") && j["result"].contains("data")) {
        std::string base64 = j["result"]["data"].get<std::string>();
        static const std::string b64chars =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string decoded;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[(unsigned char)b64chars[i]] = i;
        int val = 0, valb = -8;
        for (unsigned char c : base64) {
          if (T[c] == -1) break;
          val = (val << 6) + T[c];
          valb += 6;
          if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
          }
        }
        std::ofstream file(path, std::ios::binary);
        if (file) { file.write(decoded.data(), decoded.size()); return Value::makeBool(true); }
      }
    } catch (...) {}
    return Value::makeBool(false);
  });

  api.registerFunction("browser.setZoom", [](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.setZoom requires a zoom level");
    double level = 1.0;
    if (args[0].isDouble()) level = args[0].asDouble();
    else if (args[0].isInt()) level = static_cast<double>(args[0].asInt());
    if (level < 0.5) level = 0.5;
    if (level > 3.0) level = 3.0;
    std::string response = sendCdp("Emulation.setPageScaleFactor",
      "{\"scaleFactor\":" + std::to_string(level) + "}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.getZoom", [](const std::vector<Value> &) {
    std::string js = "(function(){return document.body.style.zoom||'100%'})()";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    if (!response.empty()) {
      try {
        auto j = json::parse(response);
        if (j.contains("result") && j["result"].contains("result") &&
            j["result"]["result"].contains("value")) {
          std::string zoom = j["result"]["result"]["value"].get<std::string>();
          auto pct = zoom.find('%');
          if (pct != std::string::npos) return Value::makeDouble(std::stod(zoom.substr(0, pct)) / 100.0);
          return Value::makeDouble(std::stod(zoom));
        }
      } catch (...) {}
    }
    return Value::makeDouble(1.0);
  });

  api.registerFunction("browser.resetZoom", [](const std::vector<Value> &) {
    std::string response = sendCdp("Emulation.setPageScaleFactor", "{\"scaleFactor\":1.0}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.getUrl", [api](const std::vector<Value> &) {
    std::string js = "window.location.href";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    if (!response.empty()) {
      try {
        auto j = json::parse(response);
        if (j.contains("result") && j["result"].contains("result") &&
            j["result"]["result"].contains("value"))
          return api.makeString(j["result"]["result"]["value"].get<std::string>());
      } catch (...) {}
    }
    return api.makeString("");
  });

  api.registerFunction("browser.getTitle", [api](const std::vector<Value> &) {
    std::string js = "document.title";
    std::string response = sendCdp("Runtime.evaluate",
      "{\"expression\":\"" + js + "\",\"returnByValue\":true}");
    if (!response.empty()) {
      try {
        auto j = json::parse(response);
        if (j.contains("result") && j["result"].contains("result") &&
            j["result"]["result"].contains("value"))
          return api.makeString(j["result"]["result"]["value"].get<std::string>());
      } catch (...) {}
    }
    return api.makeString("");
  });

  api.registerFunction("browser.listWindows", [api](const std::vector<Value> &) {
    auto tabs = getTabs();
    auto arr = api.makeArray();
    std::set<std::string> seen;
    for (auto &tab : tabs) {
      if (tab.contains("windowId") && tab["windowId"].is_string()) {
        std::string wid = tab["windowId"].get<std::string>();
        if (seen.insert(wid).second) {
          auto obj = api.makeObject();
          api.setField(obj, "id", api.makeString(wid));
          api.push(arr, obj);
        }
      }
    }
    return arr;
  });

  api.registerFunction("browser.maximize", [](const std::vector<Value> &args) {
    std::string response = sendCdp("Browser.setWindowBounds",
      "{\"windowId\":1,\"bounds\":{\"windowState\":\"maximized\"}}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.minimize", [](const std::vector<Value> &args) {
    std::string response = sendCdp("Browser.setWindowBounds",
      "{\"windowId\":1,\"bounds\":{\"windowState\":\"minimized\"}}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.fullscreen", [](const std::vector<Value> &args) {
    std::string response = sendCdp("Browser.setWindowBounds",
      "{\"windowId\":1,\"bounds\":{\"windowState\":\"fullscreen\"}}");
    return Value::makeBool(!response.empty());
  });

  api.registerFunction("browser.setPort", [](const std::vector<Value> &args) {
    if (args.empty()) throw std::runtime_error("browser.setPort requires a port number");
    auto &st = cdpState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.cdpPort = static_cast<int>(args[0].asInt());
    st.browserUrl = "http://localhost:" + std::to_string(st.cdpPort);
    return Value::makeBool(true);
  });

  api.registerFunction("browser.getPort", [](const std::vector<Value> &) {
    return Value::makeInt(cdpState().cdpPort);
  });

  api.registerFunction("browser.getVersion", [api](const std::vector<Value> &) {
    std::string response = httpGet(cdpState().browserUrl + "/json/version");
    if (response.empty()) return api.makeString("");
    try {
      auto j = json::parse(response);
      return api.makeString(j.dump());
    } catch (...) {
      return api.makeString(response);
    }
  });

  api.registerFunction("browser.findPath", [api](const std::vector<Value> &) {
    return api.makeString(findBrowserPath());
  });

  auto browserObj = api.makeObject();
  api.setField(browserObj, "connect", api.makeFunctionRef("browser.connect"));
  api.setField(browserObj, "disconnect", api.makeFunctionRef("browser.disconnect"));
  api.setField(browserObj, "isConnected", api.makeFunctionRef("browser.isConnected"));
  api.setField(browserObj, "goto", api.makeFunctionRef("browser.goto"));
  api.setField(browserObj, "open", api.makeFunctionRef("browser.open"));
  api.setField(browserObj, "newTab", api.makeFunctionRef("browser.newTab"));
  api.setField(browserObj, "back", api.makeFunctionRef("browser.back"));
  api.setField(browserObj, "forward", api.makeFunctionRef("browser.forward"));
  api.setField(browserObj, "reload", api.makeFunctionRef("browser.reload"));
  api.setField(browserObj, "listTabs", api.makeFunctionRef("browser.listTabs"));
  api.setField(browserObj, "activate", api.makeFunctionRef("browser.activate"));
  api.setField(browserObj, "closeTab", api.makeFunctionRef("browser.closeTab"));
  api.setField(browserObj, "closeAll", api.makeFunctionRef("browser.closeAll"));
  api.setField(browserObj, "eval", api.makeFunctionRef("browser.eval"));
  api.setField(browserObj, "click", api.makeFunctionRef("browser.click"));
  api.setField(browserObj, "type", api.makeFunctionRef("browser.type"));
  api.setField(browserObj, "focus", api.makeFunctionRef("browser.focus"));
  api.setField(browserObj, "screenshot", api.makeFunctionRef("browser.screenshot"));
  api.setField(browserObj, "setZoom", api.makeFunctionRef("browser.setZoom"));
  api.setField(browserObj, "getZoom", api.makeFunctionRef("browser.getZoom"));
  api.setField(browserObj, "resetZoom", api.makeFunctionRef("browser.resetZoom"));
  api.setField(browserObj, "getUrl", api.makeFunctionRef("browser.getUrl"));
  api.setField(browserObj, "getTitle", api.makeFunctionRef("browser.getTitle"));
  api.setField(browserObj, "listWindows", api.makeFunctionRef("browser.listWindows"));
  api.setField(browserObj, "maximize", api.makeFunctionRef("browser.maximize"));
  api.setField(browserObj, "minimize", api.makeFunctionRef("browser.minimize"));
  api.setField(browserObj, "fullscreen", api.makeFunctionRef("browser.fullscreen"));
  api.setField(browserObj, "setPort", api.makeFunctionRef("browser.setPort"));
  api.setField(browserObj, "getPort", api.makeFunctionRef("browser.getPort"));
  api.setField(browserObj, "getVersion", api.makeFunctionRef("browser.getVersion"));
  api.setField(browserObj, "findPath", api.makeFunctionRef("browser.findPath"));
  api.setGlobal("browser", browserObj);
}

} // namespace havel::stdlib
