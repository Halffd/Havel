#include "window/strategies/CompositorStrategy.hpp"
#include "utils/Logger.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

namespace havel {

static std::string ExecCmd(const std::string &cmd) {
  std::array<char, 256> buffer;
  std::string result;
  auto pipe = popen(cmd.c_str(), "r");
  if (!pipe) return result;
  while (fgets(buffer.data(), buffer.size(), pipe)) {
    result += buffer.data();
  }
  pclose(pipe);
  if (!result.empty() && result.back() == '\n') result.pop_back();
  return result;
}

static std::string ExtractJsonString(const std::string &json, const std::string &key) {
  std::string searchKey = "\"" + key + "\":\"";
  size_t pos = json.find(searchKey);
  if (pos == std::string::npos) {
    searchKey = "\"" + key + "\": \""; 
    pos = json.find(searchKey);
  }
  if (pos != std::string::npos) {
    pos += searchKey.length();
    size_t endPos = json.find("\"", pos);
    if (endPos != std::string::npos) {
      return json.substr(pos, endPos - pos);
    }
  }
  return "";
}

static int ExtractJsonInt(const std::string &json, const std::string &key) {
  std::string searchKey = "\"" + key + "\":";
  size_t pos = json.find(searchKey);
  if (pos == std::string::npos) return 0;
  pos += searchKey.length();
  while (pos < json.length() && std::isspace(json[pos])) pos++;
  size_t endPos = pos;
  while (endPos < json.length() && std::isdigit(json[endPos])) endPos++;
  if (endPos > pos) {
    try { return std::stoi(json.substr(pos, endPos - pos)); }
    catch (...) { return 0; }
  }
  return 0;
}

std::string KWinStrategy::GetHome() const {
  const char *home = std::getenv("HOME");
  return home ? home : "";
}

CompositorBridge::WindowInfo KWinStrategy::QueryActiveWindow() {
  CompositorBridge::WindowInfo info;
  std::string path = GetHome() + "/.local/share/kwin/scripts/havelbridge/activewindow.txt";
  std::ifstream file(path);
  if (!file.is_open()) {
    debug("KWin bridge file not found");
    return info;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.find("title=") == 0) info.title = line.substr(6);
    else if (line.find("appid=") == 0) info.appId = line.substr(6);
    else if (line.find("pid=") == 0) {
      try { info.pid = std::stoi(line.substr(4)); }
      catch (...) { info.pid = 0; }
    }
  }
  if (!info.title.empty() || !info.appId.empty()) info.valid = true;
  return info;
}

CompositorBridge::CompositorType KWinStrategy::GetType() const {
  return CompositorBridge::CompositorType::KWin;
}

std::string KWinStrategy::GetName() const {
  return "KWin";
}

CompositorBridge::WindowInfo SwayStrategy::QueryActiveWindow() {
  CompositorBridge::WindowInfo info;
  std::string result = ExecCmd("swaymsg -t get_tree 2>/dev/null");
  if (result.empty()) return info;

  size_t focusedPos = result.find("\"focused\": true");
  if (focusedPos == std::string::npos) return info;

  size_t objectStart = result.rfind("{", focusedPos);
  if (objectStart == std::string::npos) return info;

  std::string focused = result.substr(objectStart,
    result.find("}", focusedPos) - objectStart);

  auto extract = [&focused](const std::string &k) -> std::string {
    std::string sk = "\"" + k + "\":\"";
    size_t p = focused.find(sk);
    if (p != std::string::npos) {
      p += sk.length();
      size_t e = focused.find("\"", p);
      if (e != std::string::npos) return focused.substr(p, e - p);
    }
    return "";
  };

  info.title = extract("name");
  info.appId = extract("app_id");
  info.pid = ExtractJsonInt(focused, "pid");
  if (!info.title.empty() || !info.appId.empty()) info.valid = true;
  return info;
}

CompositorBridge::CompositorType SwayStrategy::GetType() const {
  return CompositorBridge::CompositorType::Sway;
}

std::string SwayStrategy::GetName() const {
  return "Sway";
}

CompositorBridge::WindowInfo HyprlandStrategy::QueryActiveWindow() {
  CompositorBridge::WindowInfo info;
  std::string result = ExecCmd("hyprctl activewindow -j 2>/dev/null");
  if (result.empty()) return info;

  info.title = ExtractJsonString(result, "title");
  info.appId = ExtractJsonString(result, "class");
  info.pid = ExtractJsonInt(result, "pid");
  if (!info.title.empty() || !info.appId.empty()) info.valid = true;
  return info;
}

CompositorBridge::CompositorType HyprlandStrategy::GetType() const {
  return CompositorBridge::CompositorType::Hyprland;
}

std::string HyprlandStrategy::GetName() const {
  return "Hyprland";
}

CompositorBridge::WindowInfo RiverStrategy::QueryActiveWindow() {
  return {};
}

CompositorBridge::CompositorType RiverStrategy::GetType() const {
  return CompositorBridge::CompositorType::River;
}

std::string RiverStrategy::GetName() const {
  return "River";
}

CompositorBridge::WindowInfo WayfireStrategy::QueryActiveWindow() {
  return {};
}

CompositorBridge::CompositorType WayfireStrategy::GetType() const {
  return CompositorBridge::CompositorType::Wayfire;
}

std::string WayfireStrategy::GetName() const {
  return "Wayfire";
}

std::unique_ptr<CompositorStrategy> CreateCompositorStrategy() {
  const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
  if (!waylandDisplay) return nullptr;

  const char *kdeSession = std::getenv("KDE_SESSION_VERSION");
  if (kdeSession) {
    if (system("pgrep -x kwin_wayland >/dev/null 2>&1") == 0)
      return std::make_unique<KWinStrategy>();
  }

  const char *swaySocket = std::getenv("SWAYSOCK");
  if (swaySocket) return std::make_unique<SwayStrategy>();

  const char *hyprlandInstance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (hyprlandInstance) return std::make_unique<HyprlandStrategy>();

  if (system("pgrep -x river >/dev/null 2>&1") == 0)
    return std::make_unique<RiverStrategy>();
  if (system("pgrep -x wayfire >/dev/null 2>&1") == 0)
    return std::make_unique<WayfireStrategy>();

  return nullptr;
}

} // namespace havel
