#include "WindowManager.hpp"
#include "utils/Logger.hpp"
#include "types.hpp"
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

namespace havel {

typedef std::map<std::string, std::vector<std::string>> group;
static group groups;

str WindowManager::defaultTerminal = "alacritty";
wID WindowManager::previousActiveWindow = 0;
WindowStats WindowManager::activeWindow = {};

WindowManager::WindowManager() {
  backend_ = WindowBackendFactory::Create();
  WindowManagerDetector detector;
  wmName = detector.GetWMName();
  wmType = detector.Detect();
  wmSupported = true;
}

void WindowManager::ensureBackend() {
  std::call_once(backendInitFlag_, [this]() {
    backend_->initialize();

    if (backend_->getDisplayServer() == DisplayServer::X11) {
      InitializeX11();
    }

    LoadGroupsFromConfig();
  });
}

bool WindowManager::InitializeX11() {
  DisplayManager::Initialize();
  return DisplayManager::GetDisplay() != nullptr;
}

void WindowManager::LoadGroupsFromConfig() {
  auto &config = Configs::Get();
  auto allKeys = config.GetAllKeys();
  for (const auto &key : allKeys) {
    if (key.find("window.group.") == 0) {
      std::string rest = key.substr(13);
      size_t dotPos = rest.find('.');
      if (dotPos != std::string::npos) {
        std::string groupName = "group." + rest.substr(0, dotPos);
        std::string identifier = config.Get<std::string>(key, "");
        if (!identifier.empty()) {
          AddGroup(groupName, identifier);
          debug("Loaded window group: {} = {}", groupName, identifier);
        }
      }
    }
  }
  debug("Loaded {} window groups from config", groups.size());
}

wID WindowManager::GetActiveWindow() {
  get().ensureBackend();
  return get().getBackend().getActiveWindow();
}

pID WindowManager::GetActiveWindowPID() {
  get().ensureBackend();
  return get().getBackend().getActiveWindowPID();
}

std::string WindowManager::GetActiveWindowProcess() {
  get().ensureBackend();
  return get().getBackend().getActiveWindowProcess();
}

std::string WindowManager::GetActiveWindowTitle() {
  get().ensureBackend();
  return get().getBackend().getActiveWindowTitle();
}

pID WindowManager::GetWindowPID(wID id) {
  get().ensureBackend();
  return get().getBackend().getWindowPID(id);
}

std::string WindowManager::GetWindowTitle(wID id) {
  get().ensureBackend();
  return get().getBackend().getWindowTitle(id);
}

std::string WindowManager::GetWindowClass(wID id) {
  get().ensureBackend();
  return get().getBackend().getWindowClass(id);
}

wID WindowManager::GetwIDByPID(pID pid) {
  get().ensureBackend();
  return get().getBackend().findWindowByPID(pid);
}

wID WindowManager::GetwIDByProcessName(cstr processName) {
  get().ensureBackend();
  return get().getBackend().findWindowByProcessName(processName);
}

wID WindowManager::FindByClass(cstr className) {
  get().ensureBackend();
  return get().getBackend().findWindowByClass(className);
}

wID WindowManager::FindByTitle(cstr title) {
  get().ensureBackend();
  return get().getBackend().findWindowByTitle(title);
}

wID WindowManager::Find(cstr identifier) {
  std::string type = GetIdentifierType(identifier);
  std::string value = GetIdentifierValue(identifier);

  if (type == "group") {
    return FindWindowInGroup(value);
  } else if (type == "class") {
    return FindByClass(value);
  } else if (type == "pid") {
    pID pid = std::stoul(value);
    return GetwIDByPID(pid);
  } else if (type == "exe") {
    return GetwIDByProcessName(value);
  } else if (type == "title") {
    return FindByTitle(value);
  } else if (type == "id") {
    return std::stoul(value);
  }
  return FindByTitle(value);
}

wID WindowManager::FindWindowInGroup(cstr groupName) {
  auto it = groups.find(groupName);
  if (it != groups.end()) {
    for (const auto &identifier : it->second) {
      wID win = Find(identifier);
      if (win) return win;
    }
  }
  return 0;
}

wID WindowManager::NewWindow(cstr name, std::vector<int> *dimensions,
                              bool hide) {
  return get().getBackend().newWindow(name, dimensions, hide);
}

void WindowManager::AddGroup(cstr groupName, cstr identifier) {
  groups[groupName].push_back(identifier);
}

std::vector<std::string> WindowManager::GetGroupNames() {
  std::vector<std::string> names;
  for (const auto &[name, _] : groups) {
    names.push_back(name);
  }
  return names;
}

std::vector<std::string> WindowManager::GetGroupWindows(cstr groupName) {
  auto it = groups.find(groupName);
  if (it != groups.end()) return it->second;
  return {};
}

bool WindowManager::IsWindowInGroup(cstr windowTitle, cstr groupName) {
  auto it = groups.find(groupName);
  if (it != groups.end()) {
    for (const auto &identifier : it->second) {
      if (identifier == windowTitle) return true;
    }
  }
  return false;
}

std::string WindowManager::GetCurrentWMName() const { return wmName; }

bool WindowManager::IsWMSupported() const { return wmSupported; }

bool WindowManager::IsX11() {
  return get().getBackend().getDisplayServer() == DisplayServer::X11;
}

bool WindowManager::IsWayland() {
  return get().getBackend().getDisplayServer() == DisplayServer::Wayland;
}

std::string WindowManager::DetectWindowManager() const {
  return WindowManagerDetector().GetWMName();
}

bool WindowManager::CheckWMProtocols() const {
  return true;
}

void WindowManager::All() {
  auto windows = get().getBackend().getAllWindows();
  for (const auto &w : windows) {
    std::cout << "Window: id=" << w.id << " title='" << w.title
              << "' class='" << w.windowClass << "'" << std::endl;
  }
}

str WindowManager::GetIdentifierType(cstr identifier) {
  size_t pos = identifier.find('=');
  if (pos == std::string::npos) return "title";
  return identifier.substr(0, pos);
}

str WindowManager::GetIdentifierValue(cstr identifier) {
  size_t pos = identifier.find('=');
  if (pos == std::string::npos) return identifier;
  return identifier.substr(pos + 1);
}

str WindowManager::getProcessName(pid_t windowPID) {
  return get().getBackend().getProcessName(windowPID);
}

str WindowManager::getProcessCmdline(pid_t windowPID) {
  return get().getBackend().getProcessCmdline(windowPID);
}

bool WindowManager::Resize(wID windowId, int width, int height,
                            bool fullscreen) {
  return get().getBackend().resizeWindow(windowId, width, height);
}

bool WindowManager::Move(wID windowId, int x, int y, bool centerOnScreen) {
  if (centerOnScreen) return Center(windowId);
  return get().getBackend().moveWindow(windowId, x, y);
}

bool WindowManager::MoveResize(wID windowId, int x, int y, int width,
                                int height) {
  return get().getBackend().moveResizeWindow(windowId, x, y, width, height);
}

bool WindowManager::Center(wID windowId) {
  return get().getBackend().centerWindow(windowId);
}

bool WindowManager::MoveToCorner(wID windowId, const std::string &corner) {
  auto monitors = DisplayManager::GetMonitors();
  if (monitors.empty()) return false;

  auto &monitor = monitors[0];
  Rect pos = get().getBackend().getWindowPosition(windowId);

  int x = 0, y = 0;
  if (corner == "top-left" || corner == "tl") {
    x = monitor.x; y = monitor.y;
  } else if (corner == "top-right" || corner == "tr") {
    x = monitor.x + monitor.width - pos.width; y = monitor.y;
  } else if (corner == "bottom-left" || corner == "bl") {
    x = monitor.x; y = monitor.y + monitor.height - pos.height;
  } else if (corner == "bottom-right" || corner == "br") {
    x = monitor.x + monitor.width - pos.width;
    y = monitor.y + monitor.height - pos.height;
  }
  return get().getBackend().moveWindow(windowId, x, y);
}

bool WindowManager::MoveToMonitor(wID windowId, int monitorIndex) {
  return get().getBackend().moveWindowToMonitor(windowId, monitorIndex);
}

bool WindowManager::Center(const std::string &windowTitle) {
  wID win = FindByTitle(windowTitle);
  if (!win) return false;
  return Center(win);
}

bool WindowManager::Resize(const std::string &windowTitle, int width,
                            int height, bool fullscreen) {
  wID win = FindByTitle(windowTitle);
  if (!win) return false;
  return Resize(win, width, height, fullscreen);
}

bool WindowManager::SetResolution(wID windowId,
                                   const std::string &resolution) {
  size_t xPos = resolution.find('x');
  if (xPos == std::string::npos) return false;
  int width = std::stoi(resolution.substr(0, xPos));
  int height = std::stoi(resolution.substr(xPos + 1));
  return Resize(windowId, width, height);
}

bool WindowManager::Move(const std::string &windowTitle, int x, int y,
                          bool centerOnScreen) {
  wID win = FindByTitle(windowTitle);
  if (!win) return false;
  return Move(win, x, y, centerOnScreen);
}

void WindowManager::MoveToCorners(int direction, int distance) {
  wID win = GetActiveWindow();
  if (!win) return;

  if (direction == 1) {
    Move(win, distance, distance);
  } else if (direction == 2) {
    auto monitor = DisplayManager::GetPrimaryMonitor();
    Rect pos = get().getBackend().getWindowPosition(win);
    Move(win, monitor.width - pos.width - distance, distance);
  }
}

void WindowManager::ResizeToCorner(int direction, int distance) {
  wID win = GetActiveWindow();
  if (!win) return;

  auto monitor = DisplayManager::GetPrimaryMonitor();
  int w = (monitor.width / 2) - distance * 2;
  int h = (monitor.height / 2) - distance * 2;

  if (direction == 1) {
    MoveResize(win, distance, distance, w, h);
  } else if (direction == 2) {
    MoveResize(win, monitor.width / 2 + distance, distance, w, h);
  }
}

void WindowManager::SnapWindow(wID windowId, int position) {
  get().getBackend().snapWindow(windowId, position);
}

void WindowManager::SnapWindowWithPadding(int position, int padding) {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().snapWindow(win, position, padding);
}

void WindowManager::ManageVirtualDesktops(int action) {
  auto &backend = get().getBackend();
  if (action == 1 || action == 2) {
    int current = backend.getCurrentWorkspace();
    auto workspaces = backend.getWorkspaces();
    if (workspaces.empty()) return;
    int next = (action == 1) ? current + 1 : current - 1;
    if (next < 1) next = workspaces.size();
    if (next > static_cast<int>(workspaces.size())) next = 1;
    backend.switchToWorkspace(next);
  }
}

void WindowManager::ToggleAlwaysOnTop() {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().setWindowAlwaysOnTop(win, true);
}

std::string WindowManager::GetActiveWindowClass() {
  return get().getBackend().getActiveWindowClass();
}

void WindowManager::UpdatePreviousActiveWindow() {
  auto active = GetActiveWindow();
  if (active != 0 && active != previousActiveWindow) {
    activeWindow.id = active;
    activeWindow.className = GetActiveWindowClass();
    activeWindow.title = GetActiveWindowTitle();
    previousActiveWindow = active;
  }
}

void WindowManager::ToggleFullscreen(wID windowId) {
  get().getBackend().toggleWindowFullscreen(windowId);
}

bool WindowManager::IsWindowFullscreen(wID windowId) {
  return get().getBackend().isWindowFullscreen(windowId);
}

void WindowManager::MoveWindowToNextMonitor() {
  wID win = GetActiveWindow();
  if (!win) return;

  auto monitors = DisplayManager::GetMonitors();
  if (monitors.size() < 2) return;

  Rect pos = get().getBackend().getWindowPosition(win);
  if (pos.width == 0) return;

  bool isFullscreen = IsWindowFullscreen(win);
  if (isFullscreen) ToggleFullscreen(win);

  int currentMonitor = 0;
  for (size_t i = 0; i < monitors.size(); i++) {
    if (pos.x >= monitors[i].x &&
        pos.x < monitors[i].x + monitors[i].width) {
      currentMonitor = i;
      break;
    }
  }

  int nextMonitor = (currentMonitor + 1) % monitors.size();
  auto &target = monitors[nextMonitor];
  auto &current = monitors[currentMonitor];
  int newX = target.x + (pos.x - current.x);
  int newY = target.y + (pos.y - current.y);
  Move(win, newX, newY);
}

void WindowManager::AltTab() {
  UpdatePreviousActiveWindow();
  auto windows = get().getBackend().getAllWindows();
  if (windows.size() < 2) return;

  size_t currentIdx = 0;
  wID active = GetActiveWindow();
  for (size_t i = 0; i < windows.size(); i++) {
    if (windows[i].id == active) {
      currentIdx = i;
      break;
    }
  }

  size_t nextIdx = (currentIdx + 1) % windows.size();
  if (windows[nextIdx].valid && windows[nextIdx].id != 0) {
    get().getBackend().focusWindow(static_cast<wID>(windows[nextIdx].id));
  }
}

void WindowManager::WindowSpy() {}
void WindowManager::MouseDrag() {}
void WindowManager::ClickThrough() {}
void WindowManager::ToggleClickLock() {}
void WindowManager::AltTabMenu() {}
void WindowManager::RotateWindow() {}

void WindowManager::WinClose() {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().closeWindow(win);
}

void WindowManager::WinMinimize() {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().minimizeWindow(win);
}

void WindowManager::WinMaximize() {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().maximizeWindow(win);
}

void WindowManager::WinRestore() {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().restoreWindow(win);
}

void WindowManager::WinHide(wID win) {
  get().getBackend().hideWindow(win);
}

void WindowManager::WinShow(wID win) {
  get().getBackend().showWindow(win);
}

void WindowManager::WinTransparent() {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().setWindowOpacity(win, 0.8f);
}

void WindowManager::WinMoveResize() {}

void WindowManager::WinSetAlwaysOnTop(bool onTop) {
  wID win = GetActiveWindow();
  if (!win) return;
  get().getBackend().setWindowAlwaysOnTop(win, onTop);
}

void WindowManager::SendToMonitor(int monitorIndex) {
  wID win = GetActiveWindow();
  if (!win) return;
  MoveToMonitor(win, monitorIndex);
}

void WindowManager::InitializeCompositorBridge() {
}

void WindowManager::ShutdownCompositorBridge() {
}

CompositorBridge *WindowManager::GetCompositorBridge() {
  return nullptr;
}

WindowInfo WindowManager::getActiveWindowInfo() {
  get().ensureBackend();
  return get().getBackend().getActiveWindowInfo();
}

WindowInfo WindowManager::getWindowInfo(wID id) {
  get().ensureBackend();
  return get().getBackend().getWindowInfo(id);
}

std::vector<WindowInfo> WindowManager::getAllWindows() {
  get().ensureBackend();
  return get().getBackend().getAllWindows();
}

uint64_t WindowManager::getActiveWindow() {
  return static_cast<uint64_t>(GetActiveWindow());
}

bool WindowManager::focusWindow(uint64_t id) {
  get().ensureBackend();
  return get().getBackend().focusWindow(static_cast<wID>(id));
}

bool WindowManager::closeWindow(uint64_t id) {
  get().ensureBackend();
  return get().getBackend().closeWindow(static_cast<wID>(id));
}

bool WindowManager::moveWindow(uint64_t id, int x, int y) {
  get().ensureBackend();
  return Move(static_cast<wID>(id), x, y);
}

bool WindowManager::resizeWindow(uint64_t id, int width, int height) {
  return Resize(static_cast<wID>(id), width, height);
}

bool WindowManager::moveResizeWindow(uint64_t id, int x, int y,
                                      int width, int height) {
  return MoveResize(static_cast<wID>(id), x, y, width, height);
}

bool WindowManager::maximizeWindow(uint64_t id) {
  return get().getBackend().maximizeWindow(static_cast<wID>(id));
}

bool WindowManager::minimizeWindow(uint64_t id) {
  return get().getBackend().minimizeWindow(static_cast<wID>(id));
}

bool WindowManager::restoreWindow(uint64_t id) {
  return get().getBackend().restoreWindow(static_cast<wID>(id));
}

bool WindowManager::hideWindow(uint64_t id) {
  return get().getBackend().hideWindow(static_cast<wID>(id));
}

bool WindowManager::showWindow(uint64_t id) {
  return get().getBackend().showWindow(static_cast<wID>(id));
}

bool WindowManager::toggleFullscreen(uint64_t id) {
  ToggleFullscreen(static_cast<wID>(id));
  return true;
}

bool WindowManager::setFloating(uint64_t id, bool floating) {
  return get().getBackend().setWindowFloating(static_cast<wID>(id), floating);
}

bool WindowManager::centerWindow(uint64_t id) {
  return Center(static_cast<wID>(id));
}

bool WindowManager::snapWindow(uint64_t id, int position) {
  return get().getBackend().snapWindow(static_cast<wID>(id), position);
}

bool WindowManager::moveWindowToWorkspace(uint64_t id, int workspace) {
  return get().getBackend().moveWindowToWorkspace(static_cast<wID>(id), workspace);
}

bool WindowManager::setAlwaysOnTop(uint64_t id, bool onTop) {
  return get().getBackend().setWindowAlwaysOnTop(static_cast<wID>(id), onTop);
}

bool WindowManager::moveWindowToMonitor(uint64_t id, int monitor) {
  return get().getBackend().moveWindowToMonitor(static_cast<wID>(id), monitor);
}

std::vector<WorkspaceInfo> WindowManager::getWorkspaces() {
  return get().getBackend().getWorkspaces();
}

bool WindowManager::switchToWorkspace(int workspace) {
  return get().getBackend().switchToWorkspace(workspace);
}

int WindowManager::getCurrentWorkspace() {
  return get().getBackend().getCurrentWorkspace();
}

std::vector<std::string> WindowManager::getGroupNames() {
  return GetGroupNames();
}

std::vector<WindowInfo>
WindowManager::getGroupWindows(const std::string &groupName) {
  std::vector<WindowInfo> windows;
  auto windowNames = GetGroupWindows(groupName);
  for (const auto &name : windowNames) {
    WindowInfo info;
    info.title = name;
    info.valid = true;
    windows.push_back(info);
  }
  return windows;
}

bool WindowManager::addWindowToGroup(uint64_t id,
                                      const std::string &groupName) {
  return false;
}

bool WindowManager::removeWindowFromGroup(uint64_t id,
                                           const std::string &groupName) {
  return false;
}

} // namespace havel
