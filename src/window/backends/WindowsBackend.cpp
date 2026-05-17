#include "window/backends/WindowsBackend.hpp"

namespace havel {

WindowsBackend::WindowsBackend() = default;
WindowsBackend::~WindowsBackend() = default;

DisplayServer WindowsBackend::getDisplayServer() const {
  return DisplayServer::Unknown;
}

WindowManagerDetector::WMType WindowsBackend::getWMType() const {
  return WindowManagerDetector::WMType::UNKNOWN;
}

std::string WindowsBackend::getWMName() const { return "Windows"; }
bool WindowsBackend::isWMSupported() const { return true; }
bool WindowsBackend::initialize() { return true; }
void WindowsBackend::shutdown() {}

wID WindowsBackend::getActiveWindow() {
#ifdef _WIN32
  return reinterpret_cast<wID>(GetForegroundWindow());
#else
  return 0;
#endif
}

pID WindowsBackend::getActiveWindowPID() {
#ifdef _WIN32
  HWND hwnd = GetForegroundWindow();
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  return pid;
#else
  return 0;
#endif
}

std::string WindowsBackend::getActiveWindowProcess() { return ""; }
std::string WindowsBackend::getActiveWindowTitle() { return ""; }
std::string WindowsBackend::getActiveWindowClass() { return ""; }

pID WindowsBackend::getWindowPID(wID /* id */) { return 0; }

std::string WindowsBackend::getWindowTitle(wID win) {
#ifdef _WIN32
  char title[256];
  if (GetWindowTextA(reinterpret_cast<HWND>(win), title, sizeof(title))) {
    return std::string(title);
  }
#endif
  return "";
}

std::string WindowsBackend::getWindowClass(wID win) {
#ifdef _WIN32
  char className[256];
  if (GetClassNameA(reinterpret_cast<HWND>(win), className, sizeof(className))) {
    return std::string(className);
  }
#endif
  return "";
}

Rect WindowsBackend::getWindowPosition(wID win) {
#ifdef _WIN32
  RECT rect;
  if (GetWindowRect(reinterpret_cast<HWND>(win), &rect)) {
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
  }
#endif
  return {};
}

bool WindowsBackend::isWindowActive(wID win) {
#ifdef _WIN32
  return GetForegroundWindow() == reinterpret_cast<HWND>(win);
#else
  return false;
#endif
}

bool WindowsBackend::isWindowExists(wID win) {
#ifdef _WIN32
  return IsWindow(reinterpret_cast<HWND>(win)) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::isWindowFullscreen(wID /* id */) { return false; }

wID WindowsBackend::findWindowByPID(pID /* pid */) { return 0; }
wID WindowsBackend::findWindowByProcessName(const std::string & /* processName */) { return 0; }
wID WindowsBackend::findWindowByClass(const std::string & /* className */) { return 0; }
wID WindowsBackend::findWindowByTitle(const std::string & /* title */) { return 0; }
wID WindowsBackend::newWindow(const std::string & /* name */, std::vector<int> * /* dimensions */, bool /* hide */) { return 0; }

bool WindowsBackend::moveWindow(wID /* id */, int /* x */, int /* y */) { return false; }
bool WindowsBackend::resizeWindow(wID /* id */, int /* width */, int /* height */) { return false; }
bool WindowsBackend::moveResizeWindow(wID /* id */, int /* x */, int /* y */, int /* width */, int /* height */) { return false; }

bool WindowsBackend::closeWindow(wID /* id */) { return false; }

bool WindowsBackend::focusWindow(wID win) {
#ifdef _WIN32
  SetForegroundWindow(reinterpret_cast<HWND>(win));
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::minimizeWindow(wID win) {
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(win), SW_MINIMIZE);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::maximizeWindow(wID win) {
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(win), SW_MAXIMIZE);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::restoreWindow(wID win) {
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(win), SW_RESTORE);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::hideWindow(wID win) {
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(win), SW_HIDE);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::showWindow(wID win) {
#ifdef _WIN32
  ShowWindow(reinterpret_cast<HWND>(win), SW_SHOW);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::setWindowOpacity(wID /* id */, float /* opacity */) { return false; }
bool WindowsBackend::setWindowAlwaysOnTop(wID /* id */, bool /* onTop */) { return false; }
bool WindowsBackend::toggleWindowFullscreen(wID /* id */) { return false; }
bool WindowsBackend::centerWindow(wID /* id */) { return false; }
bool WindowsBackend::snapWindow(wID /* id */, int /* position */, int /* padding */) { return false; }
bool WindowsBackend::setWindowFloating(wID /* id */, bool /* floating */) { return false; }

int WindowsBackend::getCurrentWorkspace() { return 1; }
std::vector<WorkspaceInfo> WindowsBackend::getWorkspaces() { return {}; }
bool WindowsBackend::switchToWorkspace(int /* workspace */) { return false; }
bool WindowsBackend::moveWindowToWorkspace(wID /* id */, int /* workspace */) { return false; }
bool WindowsBackend::moveWindowToMonitor(wID /* id */, int /* monitor */) { return false; }

void WindowsBackend::startAltTab() {}
void WindowsBackend::continueAltTab() {}
void WindowsBackend::finishAltTab() {}

std::vector<WindowInfo> WindowsBackend::getAllWindows() { return {}; }
WindowInfo WindowsBackend::getWindowInfo(wID id) { return {}; }
WindowInfo WindowsBackend::getActiveWindowInfo() {
  WindowInfo info;
  info.id = static_cast<uint64_t>(getActiveWindow());
  info.valid = info.id != 0;
  return info;
}

std::string WindowsBackend::getProcessName(pid_t /* pid */) { return ""; }
std::string WindowsBackend::getProcessCmdline(pid_t /* pid */) { return ""; }

} // namespace havel
