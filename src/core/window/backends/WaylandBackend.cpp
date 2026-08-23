#include "core/window/backends/WaylandBackend.hpp"
#include "core/window/WindowManager.hpp"
#include "utils/Logger.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include <chrono>

namespace havel {

WaylandBackend::WaylandBackend() {
  wmName = detector.GetWMName();
  wmSupported = true;
}

WaylandBackend::~WaylandBackend() {
  shutdown();
}

DisplayServer WaylandBackend::getDisplayServer() const {
  return DisplayServer::Wayland;
}

WindowManagerDetector::WMType WaylandBackend::getWMType() const {
  return detector.Detect();
}

std::string WaylandBackend::getWMName() const {
  return wmName;
}

bool WaylandBackend::isWMSupported() const {
  return wmSupported;
}

bool WaylandBackend::initialize() {
  compositorBridge = std::make_unique<CompositorBridge>();
  if (compositorBridge->IsAvailable()) {
    compositorBridge->Start();
    return true;
  }
  compositorBridge.reset();
  return true;
}

void WaylandBackend::shutdown() {
  if (compositorBridge) {
    compositorBridge->Stop();
    compositorBridge.reset();
  }
}

std::string WaylandBackend::ExecCmd(const std::string &cmd) const {
  std::array<char, 128> buffer;
  std::string result;
  auto pipe = popen(cmd.c_str(), "r");
  if (!pipe) return result;
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }
  pclose(pipe);
  if (!result.empty() && result.back() == '\n') result.pop_back();
  return result;
}

std::string WaylandBackend::ReadProcFile(const std::string &path) const {
  std::ifstream file(path);
  if (!file.is_open()) return "";
  std::string content;
  std::getline(file, content);
  return content;
}

CompositorBridge::CompositorType WaylandBackend::GetCompositor() const {
  return compositorBridge ? compositorBridge->GetCompositorType() :
         CompositorBridge::CompositorType::Unknown;
}

wID WaylandBackend::getActiveWindow() {
  if (compositorBridge && compositorBridge->IsAvailable()) {
    auto info = compositorBridge->GetActiveWindow();
    if (info.valid) return static_cast<wID>(info.pid);
  }
  return 0;
}

pID WaylandBackend::getActiveWindowPID() {
  if (compositorBridge && compositorBridge->IsAvailable()) {
    auto info = compositorBridge->GetActiveWindow();
    if (info.valid) return info.pid;
  }

  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activewindow -j 2>/dev/null");
    if (!out.empty()) {
      auto pidPos = out.find("\"pid\":");
      if (pidPos != std::string::npos) {
        auto start = out.find_first_of("0123456789", pidPos);
        if (start != std::string::npos)
          return std::stoi(out.substr(start));
      }
    }
  } else if (type == CompositorBridge::CompositorType::Sway) {
    auto out = ExecCmd("swaymsg -t get_tree 2>/dev/null");
    if (!out.empty()) {
      auto focused = out.find("\"focused\":true");
      if (focused != std::string::npos) {
        auto pidPos = out.rfind("\"pid\":", focused);
        if (pidPos != std::string::npos) {
          auto start = out.find_first_of("0123456789", pidPos);
          if (start != std::string::npos)
            return std::stoi(out.substr(start));
        }
      }
    }
  }
  return 0;
}

std::string WaylandBackend::getActiveWindowProcess() {
  pID pid = getActiveWindowPID();
  if (pid == 0) return "";
  return ReadProcFile("/proc/" + std::to_string(pid) + "/comm");
}

std::string WaylandBackend::getActiveWindowTitle() {
  if (compositorBridge && compositorBridge->IsAvailable()) {
    auto info = compositorBridge->GetActiveWindow();
    if (info.valid) return info.title;
  }

  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activewindow -j 2>/dev/null");
    if (!out.empty()) {
      auto titlePos = out.find("\"title\":");
      if (titlePos != std::string::npos) {
        auto start = out.find('"', titlePos + 8);
        if (start != std::string::npos) {
          auto end = out.find('"', start + 1);
          if (end != std::string::npos)
            return out.substr(start + 1, end - start - 1);
        }
      }
    }
  }

  auto wmctrl = ExecCmd("wmctrl -l 2>/dev/null");
  if (!wmctrl.empty()) {
    auto lines = wmctrl;
    size_t pos = 0;
    while ((pos = lines.find('\n', pos)) != std::string::npos) {
      pos++;
    }
    auto activeWin = getActiveWindow();
    if (activeWin) {
      std::ostringstream ss;
      ss << std::hex << reinterpret_cast<uintptr_t>(static_cast<size_t>(activeWin));
      auto hexId = ss.str();
      if (wmctrl.find(hexId) != std::string::npos) {
        auto space = wmctrl.rfind(' ');
        if (space != std::string::npos)
          return wmctrl.substr(space + 1);
      }
    }
  }
  return "";
}

std::string WaylandBackend::getActiveWindowClass() {
  if (compositorBridge && compositorBridge->IsAvailable()) {
    auto info = compositorBridge->GetActiveWindow();
    if (info.valid) return info.appId;
  }

  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activewindow -j 2>/dev/null");
    if (!out.empty()) {
      auto clsPos = out.find("\"class\":");
      if (clsPos != std::string::npos) {
        auto start = out.find('"', clsPos + 8);
        if (start != std::string::npos) {
          auto end = out.find('"', start + 1);
          if (end != std::string::npos)
            return out.substr(start + 1, end - start - 1);
        }
      }
    }
  }
  return "";
}

pID WaylandBackend::getWindowPID(wID /* id */) {
  return getActiveWindowPID();
}

std::string WaylandBackend::getWindowTitle(wID /* id */) {
  return getActiveWindowTitle();
}

std::string WaylandBackend::getWindowClass(wID /* id */) {
  return getActiveWindowClass();
}

Rect WaylandBackend::getWindowPosition(wID /* id */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activewindow -j 2>/dev/null");
    if (!out.empty()) {
      auto atPos = out.find("\"at\":");
      if (atPos != std::string::npos) {
        auto start = out.find('[', atPos);
        auto end = out.find(']', start);
        if (start != std::string::npos && end != std::string::npos) {
          auto coords = out.substr(start + 1, end - start - 1);
          auto comma = coords.find(',');
          if (comma != std::string::npos) {
            int x = std::stoi(coords.substr(0, comma));
            int y = std::stoi(coords.substr(comma + 1));

            auto sizePos = out.find("\"size\":");
            if (sizePos != std::string::npos) {
              auto sStart = out.find('[', sizePos);
              auto sEnd = out.find(']', sStart);
              if (sStart != std::string::npos && sEnd != std::string::npos) {
                auto dims = out.substr(sStart + 1, sEnd - sStart - 1);
                auto sc = dims.find(',');
                if (sc != std::string::npos) {
                  int w = std::stoi(dims.substr(0, sc));
                  int h = std::stoi(dims.substr(sc + 1));
                  return {x, y, w, h};
                }
              }
            }
            return {x, y, 0, 0};
          }
        }
      }
    }
  }
  return {};
}

bool WaylandBackend::isWindowActive(wID id) {
  return getActiveWindow() == id;
}

bool WaylandBackend::isWindowExists(wID /* id */) {
  return false;
}

bool WaylandBackend::isWindowFullscreen(wID /* id */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activewindow -j 2>/dev/null");
    if (!out.empty()) {
      auto fsPos = out.find("\"fullscreen\":");
      if (fsPos != std::string::npos) {
        auto val = out.substr(fsPos + 13, 4);
        return val.find("true") != std::string::npos;
      }
    }
  } else if (type == CompositorBridge::CompositorType::Sway) {
    auto out = ExecCmd("swaymsg -t get_tree 2>/dev/null");
    if (!out.empty()) {
      auto fsPos = out.find("\"fullscreen_mode\":");
      if (fsPos != std::string::npos) {
        auto val = out.substr(fsPos + 18, 1);
        return val == "1";
      }
    }
  }
  return false;
}

wID WaylandBackend::findWindowByPID(pID pid) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl clients -j 2>/dev/null");
    if (!out.empty()) {
      std::string search = "\"pid\":" + std::to_string(pid);
      if (out.find(search) != std::string::npos) {
        return static_cast<wID>(pid);
      }
    }
  } else if (type == CompositorBridge::CompositorType::Sway) {
    auto out = ExecCmd("swaymsg -t get_tree 2>/dev/null");
    if (!out.empty()) {
      std::string search = "\"pid\":" + std::to_string(pid);
      if (out.find(search) != std::string::npos) {
        return static_cast<wID>(pid);
      }
    }
  }
  return 0;
}

wID WaylandBackend::findWindowByProcessName(const std::string &processName) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl clients -j 2>/dev/null");
    if (!out.empty()) {
      std::string lowerName = processName;
      for (auto &c : lowerName) c = tolower(c);
      auto procPath = "/proc/";
      auto entries = ExecCmd("ls /proc/ 2>/dev/null | grep -E '^[0-9]+$'");
      std::istringstream stream(entries);
      std::string entry;
      while (std::getline(stream, entry)) {
        if (entry.empty()) continue;
        auto name = ReadProcFile("/proc/" + entry + "/comm");
        for (auto &c : name) c = tolower(c);
        if (name.find(lowerName) != std::string::npos) {
          return static_cast<wID>(std::stoi(entry));
        }
      }
    }
  }
  return 0;
}

wID WaylandBackend::findWindowByClass(const std::string &className) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl clients -j 2>/dev/null");
    if (!out.empty() && out.find(className) != std::string::npos) {
      auto out2 = ExecCmd("hyprctl activewindow -j 2>/dev/null");
      if (!out2.empty() && out2.find(className) != std::string::npos) {
        return getActiveWindow();
      }
    }
  }
  return 0;
}

wID WaylandBackend::findWindowByTitle(const std::string &title) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl clients -j 2>/dev/null");
    if (!out.empty() && out.find(title) != std::string::npos) {
      auto activeTitle = getActiveWindowTitle();
      if (activeTitle.find(title) != std::string::npos) {
        return getActiveWindow();
      }
    }
  }

  auto wmctrl = ExecCmd("wmctrl -l 2>/dev/null");
  if (!wmctrl.empty() && wmctrl.find(title) != std::string::npos) {
    auto lines = wmctrl;
    size_t pos = 0;
    std::string line;
    while ((pos = lines.find('\n')) != std::string::npos) {
      line = lines.substr(0, pos);
      lines.erase(0, pos + 1);
      auto space = line.rfind(' ');
      if (space != std::string::npos) {
        auto lineTitle = line.substr(space + 1);
        if (lineTitle.find(title) != std::string::npos) {
          auto hexId = line.substr(0, line.find(' '));
          return static_cast<wID>(std::stoul(hexId, nullptr, 16));
        }
      }
    }
  }
  return 0;
}

wID WaylandBackend::newWindow(const std::string &name,
                               std::vector<int> *dimensions, bool hide) {
  if (!hide) {
    std::string term = WindowManager::defaultTerminal;
    if (!term.empty()) {
      auto pid = ExecCmd("which " + term + " 2>/dev/null && " + term +
                         " -T \"" + name + "\" & echo $!");
      if (!pid.empty()) return static_cast<wID>(std::stoi(pid));
    }
  }
  return 0;
}

bool WaylandBackend::moveWindow(wID id, int x, int y) {
  auto type = GetCompositor();
  if (id == 0) return false;

  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto cmd = "hyprctl dispatch movewindowpixel " + std::to_string(x) +
               " " + std::to_string(y) + ",pid:" + std::to_string(static_cast<pID>(id));
    ExecCmd(cmd);
    return true;
  }
  return false;
}

bool WaylandBackend::resizeWindow(wID id, int width, int height) {
  auto type = GetCompositor();
  if (id == 0) return false;

  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto cmd = "hyprctl dispatch resizewindowpixel exact " +
               std::to_string(width) + " " + std::to_string(height) +
               ",pid:" + std::to_string(static_cast<pID>(id));
    ExecCmd(cmd);
    return true;
  }
  return false;
}

bool WaylandBackend::moveResizeWindow(wID id, int x, int y,
                                       int width, int height) {
  auto type = GetCompositor();
  if (id == 0) return false;

  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto cmd = "hyprctl dispatch movewindowpixel " + std::to_string(x) +
               " " + std::to_string(y) + ",pid:" +
               std::to_string(static_cast<pID>(id));
    ExecCmd(cmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto resize = "hyprctl dispatch resizewindowpixel exact " +
                  std::to_string(width) + " " + std::to_string(height) +
                  ",pid:" + std::to_string(static_cast<pID>(id));
    ExecCmd(resize);
    return true;
  }
  return false;
}

bool WaylandBackend::closeWindow(wID id) {
  auto type = GetCompositor();
  if (id == 0) return false;

  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch closewindow,pid:" +
            std::to_string(static_cast<pID>(id)));
    return true;
  } else if (type == CompositorBridge::CompositorType::Sway) {
    ExecCmd("swaymsg kill");
    return true;
  }
  return false;
}

bool WaylandBackend::focusWindow(wID id) {
  if (!id) return false;

  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch focuswindow,pid:" +
            std::to_string(static_cast<pID>(id)));
    return true;
  } else if (type == CompositorBridge::CompositorType::Sway) {
    ExecCmd("swaymsg [pid=" + std::to_string(static_cast<pID>(id)) + "] focus");
    return true;
  }

  std::ostringstream cmd;
  cmd << "wmctrl -i -a " << std::hex
      << reinterpret_cast<uintptr_t>(static_cast<size_t>(id));
  ExecCmd(cmd.str());
  return true;
}

bool WaylandBackend::minimizeWindow(wID /* id */) { return false; }

bool WaylandBackend::maximizeWindow(wID /* id */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch togglefloating");
    return true;
  }
  return false;
}

bool WaylandBackend::restoreWindow(wID /* id */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch togglefloating");
    return true;
  }
  return false;
}

bool WaylandBackend::hideWindow(wID /* id */) { return false; }
bool WaylandBackend::showWindow(wID /* id */) { return false; }
bool WaylandBackend::setWindowOpacity(wID /* id */, float /* opacity */) { return false; }

bool WaylandBackend::setWindowAlwaysOnTop(wID /* id */, bool /* onTop */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch togglefloating");
    return true;
  }
  return false;
}

bool WaylandBackend::toggleWindowFullscreen(wID /* id */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch fullscreen 1");
    return true;
  } else if (type == CompositorBridge::CompositorType::Sway) {
    ExecCmd("swaymsg fullscreen toggle");
    return true;
  }
  return false;
}

bool WaylandBackend::centerWindow(wID /* id */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch centerwindow");
    return true;
  }
  return false;
}

bool WaylandBackend::snapWindow(wID /* id */, int position, int /* padding */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    std::string dir = (position == 1) ? "l" : "r";
    ExecCmd("hyprctl dispatch movecurrentworkspacetomonitor");
    ExecCmd("hyprctl dispatch split:" + dir);
    return true;
  }
  return false;
}

bool WaylandBackend::setWindowFloating(wID /* id */, bool /* floating */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch togglefloating");
    return true;
  }
  return false;
}

int WaylandBackend::getCurrentWorkspace() {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activeworkspace -j 2>/dev/null");
    if (!out.empty()) {
      auto idPos = out.find("\"id\":");
      if (idPos != std::string::npos) {
        auto start = out.find_first_of("0123456789", idPos);
        if (start != std::string::npos) {
          auto end = out.find_first_not_of("0123456789", start);
          return std::stoi(out.substr(start, end - start));
        }
      }
    }
  } else if (type == CompositorBridge::CompositorType::Sway) {
    auto out = ExecCmd("swaymsg -t get_workspaces 2>/dev/null");
    if (!out.empty()) {
      auto focused = out.find("\"focused\":true");
      if (focused != std::string::npos) {
        auto numPos = out.rfind("\"num\":", focused);
        if (numPos != std::string::npos) {
          auto start = out.find_first_of("0123456789", numPos);
          if (start != std::string::npos)
            return std::stoi(out.substr(start));
        }
      }
    }
  }
  return 1;
}

std::vector<WorkspaceInfo> WaylandBackend::getWorkspaces() {
  std::vector<WorkspaceInfo> workspaces;
  auto type = GetCompositor();

  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl workspaces -j 2>/dev/null");
    if (!out.empty()) {
      size_t pos = 0;
      while ((pos = out.find("\"id\":", pos)) != std::string::npos) {
        auto start = out.find_first_of("0123456789", pos);
        auto end = out.find_first_not_of("0123456789", start);
        if (start != std::string::npos) {
          WorkspaceInfo ws;
          ws.id = std::stoi(out.substr(start, end - start));
          ws.name = "Workspace " + std::to_string(ws.id);
          ws.visible = true;
          workspaces.push_back(ws);
        }
        pos = end;
      }
    }
  } else if (type == CompositorBridge::CompositorType::Sway) {
    auto out = ExecCmd("swaymsg -t get_workspaces 2>/dev/null");
    if (!out.empty()) {
      size_t pos = 0;
      while ((pos = out.find("\"num\":", pos)) != std::string::npos) {
        auto start = out.find_first_of("0123456789", pos + 6);
        auto end = out.find_first_not_of("0123456789", start);
        if (start != std::string::npos) {
          WorkspaceInfo ws;
          ws.id = std::stoi(out.substr(start, end - start));
          ws.name = "Workspace " + std::to_string(ws.id);
          auto focusedPos = out.find("\"focused\":true", pos);
          ws.visible = (focusedPos < end + 50 && focusedPos != std::string::npos);
          workspaces.push_back(ws);
        }
        pos = end;
      }
    }
  }

  if (workspaces.empty()) {
    for (int i = 1; i <= 4; i++) {
      WorkspaceInfo ws;
      ws.id = i;
      ws.name = "Workspace " + std::to_string(i);
      ws.visible = (i == 1);
      ws.windowCount = 0;
      workspaces.push_back(ws);
    }
  }
  return workspaces;
}

bool WaylandBackend::switchToWorkspace(int workspace) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch workspace " + std::to_string(workspace));
    return true;
  } else if (type == CompositorBridge::CompositorType::Sway) {
    ExecCmd("swaymsg workspace " + std::to_string(workspace));
    return true;
  }
  return false;
}

bool WaylandBackend::moveWindowToWorkspace(wID /* id */, int workspace) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch movetoworkspace " + std::to_string(workspace));
    return true;
  } else if (type == CompositorBridge::CompositorType::Sway) {
    ExecCmd("swaymsg move window to workspace " + std::to_string(workspace));
    return true;
  }
  return false;
}

bool WaylandBackend::moveWindowToMonitor(wID /* id */, int /* monitor */) {
  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    ExecCmd("hyprctl dispatch movecurrentworkspacetomonitor");
    return true;
  }
  return false;
}

void WaylandBackend::startAltTab() {}
void WaylandBackend::continueAltTab() {}
void WaylandBackend::finishAltTab() {}

std::vector<WindowInfo> WaylandBackend::getAllWindows() {
  std::vector<WindowInfo> windows;
  auto type = GetCompositor();

  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl clients -j 2>/dev/null");
    if (!out.empty()) {
      size_t pos = 0;
      while ((pos = out.find("\"pid\":", pos)) != std::string::npos) {
        auto start = out.find_first_of("0123456789", pos + 6);
        auto end = out.find_first_not_of("0123456789", start);
        if (start != std::string::npos) {
          WindowInfo info;
          info.pid = std::stoi(out.substr(start, end - start));
          info.id = static_cast<uint64_t>(info.pid);
          info.valid = true;

          auto classPos = out.rfind("\"class\":", pos);
          if (classPos != std::string::npos) {
            auto cs = out.find('"', classPos + 8);
            auto ce = out.find('"', cs + 1);
            if (cs != std::string::npos && ce != std::string::npos)
              info.windowClass = out.substr(cs + 1, ce - cs - 1);
          }

          windows.push_back(info);
        }
        pos = end;
      }
    }
  }
  return windows;
}

WindowInfo WaylandBackend::getWindowInfo(wID id) {
  WindowInfo info;
  auto activeWin = getActiveWindow();
  if (activeWin == id) return getActiveWindowInfo();
  return info;
}

WindowInfo WaylandBackend::getActiveWindowInfo() {
  WindowInfo info;
  if (compositorBridge && compositorBridge->IsAvailable()) {
    auto ci = compositorBridge->GetActiveWindow();
    if (ci.valid) {
      info.id = static_cast<uint64_t>(ci.pid);
      info.pid = ci.pid;
      info.title = ci.title;
      info.appId = ci.appId;
      info.windowClass = ci.appId;
      info.exe = getActiveWindowProcess();
      info.valid = true;
      auto pos = getWindowPosition(static_cast<wID>(ci.pid));
      info.x = pos.x;
      info.y = pos.y;
      info.width = pos.width;
      info.height = pos.height;
      return info;
    }
  }

  auto type = GetCompositor();
  if (type == CompositorBridge::CompositorType::Hyprland) {
    auto out = ExecCmd("hyprctl activewindow -j 2>/dev/null");
    if (!out.empty()) {
      info.id = getActiveWindow();
      info.pid = getActiveWindowPID();
      info.title = getActiveWindowTitle();
      info.windowClass = getActiveWindowClass();
      info.exe = getActiveWindowProcess();
      auto pos = getWindowPosition(static_cast<wID>(info.pid));
      info.x = pos.x;
      info.y = pos.y;
      info.width = pos.width;
      info.height = pos.height;
      info.valid = true;
    }
  }
  return info;
}

std::string WaylandBackend::getProcessName(pid_t pid) {
  return ReadProcFile("/proc/" + std::to_string(pid) + "/comm");
}

std::string WaylandBackend::getProcessCmdline(pid_t pid) {
  std::string result = ReadProcFile("/proc/" + std::to_string(pid) + "/cmdline");
  if (!result.empty()) {
    for (char &c : result) { if (c == '\0') c = ' '; }
  }
  return result;
}

} // namespace havel
