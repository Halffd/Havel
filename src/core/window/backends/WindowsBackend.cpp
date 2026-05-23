#include "core/window/backends/WindowsBackend.hpp"
#include "utils/Logger.hpp"
#include <cstring>
#include <fstream>
#include <sstream>

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

std::string WindowsBackend::getActiveWindowProcess() {
  return getProcessName(getActiveWindowPID());
}

std::string WindowsBackend::getActiveWindowTitle() {
  return getWindowTitle(getActiveWindow());
}

std::string WindowsBackend::getActiveWindowClass() {
  return getWindowClass(getActiveWindow());
}

pID WindowsBackend::getWindowPID(wID id) {
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(reinterpret_cast<HWND>(id), &pid);
  return pid;
#else
  return 0;
#endif
}

std::string WindowsBackend::getWindowTitle(wID win) {
#ifdef _WIN32
  char title[1024];
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

bool WindowsBackend::isWindowFullscreen(wID win) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(win);
  if (!IsWindow(hwnd)) return false;

  RECT appRect;
  if (!GetWindowRect(hwnd, &appRect)) return false;

  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {sizeof(MONITORINFO)};
  if (!GetMonitorInfoA(monitor, &mi)) return false;

  return appRect.left == mi.rcMonitor.left &&
         appRect.top == mi.rcMonitor.top &&
         appRect.right == mi.rcMonitor.right &&
         appRect.bottom == mi.rcMonitor.bottom;
#else
  return false;
#endif
}

#ifdef _WIN32
struct EnumWindowData {
  pID pid = 0;
  std::string processName;
  std::string className;
  std::string title;
  wID result = 0;
  std::vector<WindowInfo> windows;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  if (!IsWindowVisible(hwnd)) return TRUE;
  EnumWindowData *data = reinterpret_cast<EnumWindowData *>(lParam);

  wID currentId = reinterpret_cast<wID>(hwnd);

  if (data->pid != 0) {
    DWORD winPid = 0;
    GetWindowThreadProcessId(hwnd, &winPid);
    if (winPid == data->pid) {
      data->result = currentId;
      return FALSE;
    }
  }

  if (!data->className.empty()) {
    char cls[256];
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (data->className == cls) {
      data->result = currentId;
      return FALSE;
    }
  }

  if (!data->title.empty()) {
    char title[1024];
    GetWindowTextA(hwnd, title, sizeof(title));
    if (strstr(title, data->title.c_str()) != nullptr) {
      data->result = currentId;
      return FALSE;
    }
  }

  if (!data->processName.empty()) {
    DWORD winPid = 0;
    GetWindowThreadProcessId(hwnd, &winPid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, winPid);
    if (proc) {
      char name[MAX_PATH];
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameA(proc, 0, name, &size)) {
        std::string exe(name);
        size_t slash = exe.rfind('\\');
        std::string base = (slash != std::string::npos) ? exe.substr(slash + 1) : exe;
        if (base.find(data->processName) != std::string::npos) {
          data->result = currentId;
          CloseHandle(proc);
          return FALSE;
        }
      }
      CloseHandle(proc);
    }
  }

  if (data->pid == 0 && data->className.empty() &&
      data->title.empty() && data->processName.empty()) {
    WindowInfo info;
    info.id = reinterpret_cast<uint64_t>(hwnd);
    char title[1024];
    if (GetWindowTextA(hwnd, title, sizeof(title))) info.title = title;
    char cls[256];
    if (GetClassNameA(hwnd, cls, sizeof(cls))) info.windowClass = cls;
    DWORD winPid = 0;
    GetWindowThreadProcessId(hwnd, &winPid);
    info.pid = winPid;
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
      info.x = rect.left;
      info.y = rect.top;
      info.width = rect.right - rect.left;
      info.height = rect.bottom - rect.top;
    }
    info.valid = true;
    data->windows.push_back(info);
  }

  return TRUE;
}
#endif

wID WindowsBackend::findWindowByPID(pID pid) {
#ifdef _WIN32
  EnumWindowData data;
  data.pid = pid;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
  return data.result;
#else
  return 0;
#endif
}

wID WindowsBackend::findWindowByProcessName(const std::string &processName) {
#ifdef _WIN32
  EnumWindowData data;
  data.processName = processName;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
  return data.result;
#else
  return 0;
#endif
}

wID WindowsBackend::findWindowByClass(const std::string &className) {
#ifdef _WIN32
  EnumWindowData data;
  data.className = className;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
  return data.result;
#else
  return 0;
#endif
}

wID WindowsBackend::findWindowByTitle(const std::string &title) {
#ifdef _WIN32
  EnumWindowData data;
  data.title = title;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
  return data.result;
#else
  return 0;
#endif
}

wID WindowsBackend::newWindow(const std::string &name,
                               std::vector<int> *dimensions, bool hide) {
#ifdef _WIN32
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
  int w = CW_USEDEFAULT, h = CW_USEDEFAULT;
  if (dimensions && dimensions->size() == 4) {
    x = (*dimensions)[0]; y = (*dimensions)[1];
    w = (*dimensions)[2]; h = (*dimensions)[3];
  }

  HWND hwnd = CreateWindowA("STATIC", name.c_str(),
                             hide ? WS_POPUP : WS_OVERLAPPEDWINDOW,
                             x, y, w, h, nullptr, nullptr,
                             GetModuleHandleA(nullptr), nullptr);
  return reinterpret_cast<wID>(hwnd);
#else
  return 0;
#endif
}

bool WindowsBackend::moveWindow(wID id, int x, int y) {
  return moveResizeWindow(id, x, y, -1, -1);
}

bool WindowsBackend::resizeWindow(wID id, int width, int height) {
  return moveResizeWindow(id, -1, -1, width, height);
}

bool WindowsBackend::moveResizeWindow(wID id, int x, int y,
                                       int width, int height) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;

  RECT current;
  if (!GetWindowRect(hwnd, &current)) return false;

  int finalX = (x == -1) ? current.left : x;
  int finalY = (y == -1) ? current.top : y;
  int finalW = (width == -1) ? (current.right - current.left) : width;
  int finalH = (height == -1) ? (current.bottom - current.top) : height;

  return SetWindowPos(hwnd, nullptr, finalX, finalY, finalW, finalH,
                      SWP_NOZORDER | SWP_NOACTIVATE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::closeWindow(wID id) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;
  PostMessageA(hwnd, WM_CLOSE, 0, 0);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::focusWindow(wID id) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;
  SetForegroundWindow(hwnd);
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::minimizeWindow(wID id) {
#ifdef _WIN32
  return ShowWindow(reinterpret_cast<HWND>(id), SW_MINIMIZE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::maximizeWindow(wID id) {
#ifdef _WIN32
  return ShowWindow(reinterpret_cast<HWND>(id), SW_MAXIMIZE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::restoreWindow(wID id) {
#ifdef _WIN32
  return ShowWindow(reinterpret_cast<HWND>(id), SW_RESTORE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::hideWindow(wID id) {
#ifdef _WIN32
  return ShowWindow(reinterpret_cast<HWND>(id), SW_HIDE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::showWindow(wID id) {
#ifdef _WIN32
  return ShowWindow(reinterpret_cast<HWND>(id), SW_SHOW) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::setWindowOpacity(wID id, float opacity) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;

  SetWindowLongA(hwnd, GWL_EXSTYLE,
                 GetWindowLongA(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
  BYTE alpha = static_cast<BYTE>(opacity * 255);
  return SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::setWindowAlwaysOnTop(wID id, bool onTop) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;
  HWND insertAfter = onTop ? HWND_TOPMOST : HWND_NOTOPMOST;
  return SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::toggleWindowFullscreen(wID id) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;

  static WINDOWPLACEMENT prevPlacement = {sizeof(WINDOWPLACEMENT)};

  LONG style = GetWindowLongA(hwnd, GWL_STYLE);
  if (style & WS_OVERLAPPEDWINDOW) {
    GetWindowPlacement(hwnd, &prevPlacement);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(MONITORINFO)};
    GetMonitorInfoA(monitor, &mi);

    SetWindowLongA(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    SetWindowPos(hwnd, HWND_TOP,
                 mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    SetWindowLongA(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(hwnd, &prevPlacement);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
  return true;
#else
  return false;
#endif
}

bool WindowsBackend::centerWindow(wID id) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;

  RECT rect;
  if (!GetWindowRect(hwnd, &rect)) return false;
  int w = rect.right - rect.left;
  int h = rect.bottom - rect.top;

  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {sizeof(MONITORINFO)};
  if (!GetMonitorInfoA(monitor, &mi)) return false;

  int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
  int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
  return SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::snapWindow(wID id, int position, int padding) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;

  HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {sizeof(MONITORINFO)};
  if (!GetMonitorInfoA(monitor, &mi)) return false;

  int screenW = mi.rcWork.right - mi.rcWork.left - padding * 2;
  int screenH = mi.rcWork.bottom - mi.rcWork.top - padding * 2;
  int x = mi.rcWork.left + padding;
  int y = mi.rcWork.top + padding;

  switch (position) {
    case 1: break; // left half
    case 2: x += screenW / 2 + padding; break; // right half
    default: return false;
  }

  return SetWindowPos(hwnd, nullptr, x, y,
                      screenW / 2, screenH,
                      SWP_NOZORDER | SWP_NOACTIVATE) != 0;
#else
  return false;
#endif
}

bool WindowsBackend::setWindowFloating(wID /* id */, bool /* floating */) {
  return false;
}

int WindowsBackend::getCurrentWorkspace() { return 1; }

std::vector<WorkspaceInfo> WindowsBackend::getWorkspaces() {
  std::vector<WorkspaceInfo> workspaces;
  WorkspaceInfo ws;
  ws.id = 1;
  ws.name = "Default";
  ws.visible = true;
  workspaces.push_back(ws);
  return workspaces;
}

bool WindowsBackend::switchToWorkspace(int /* workspace */) { return false; }
bool WindowsBackend::moveWindowToWorkspace(wID /* id */, int /* workspace */) { return false; }

bool WindowsBackend::moveWindowToMonitor(wID id, int monitor) {
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(id);
  if (!IsWindow(hwnd)) return false;

  HMONITOR targetMon = nullptr;
  int monIdx = 0;
  DISPLAY_DEVICEA dd = {sizeof(DISPLAY_DEVICEA)};
  for (int i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); i++) {
    if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE) {
      if (monIdx == monitor) {
        DEVMODEA dm = {};
        dm.dmSize = sizeof(DEVMODEA);
        if (EnumDisplaySettingsExA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) {
          RECT current;
          if (!GetWindowRect(hwnd, &current)) return false;
          int w = current.right - current.left;
          int h = current.bottom - current.top;
          int x = dm.dmPosition.x + (current.left % GetSystemMetrics(SM_XVIRTUALSCREEN));
          int y = dm.dmPosition.y + (current.top % GetSystemMetrics(SM_YVIRTUALSCREEN));
          return SetWindowPos(hwnd, nullptr, x, y, w, h,
                              SWP_NOZORDER | SWP_NOACTIVATE) != 0;
        }
      }
      monIdx++;
    }
  }
  return false;
#else
  return false;
#endif
}

void WindowsBackend::startAltTab() {
#ifdef _WIN32
  keybd_event(VK_MENU, 0, 0, 0);
  keybd_event(VK_TAB, 0, 0, 0);
#endif
}

void WindowsBackend::continueAltTab() {
#ifdef _WIN32
  keybd_event(VK_TAB, 0, 0, 0);
#endif
}

void WindowsBackend::finishAltTab() {
#ifdef _WIN32
  keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
  keybd_event(VK_TAB, 0, KEYEVENTF_KEYUP, 0);
#endif
}

std::vector<WindowInfo> WindowsBackend::getAllWindows() {
  std::vector<WindowInfo> windows;
#ifdef _WIN32
  EnumWindowData data;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
  windows = std::move(data.windows);
#endif
  return windows;
}

WindowInfo WindowsBackend::getWindowInfo(wID id) {
  WindowInfo info;
  info.id = static_cast<uint64_t>(id);
  info.title = getWindowTitle(id);
  info.windowClass = getWindowClass(id);
  info.pid = getWindowPID(id);
  info.exe = getProcessName(info.pid);

  Rect pos = getWindowPosition(id);
  info.x = pos.x;
  info.y = pos.y;
  info.width = pos.width;
  info.height = pos.height;
  info.valid = true;
  return info;
}

WindowInfo WindowsBackend::getActiveWindowInfo() {
  return getWindowInfo(getActiveWindow());
}

std::string WindowsBackend::getProcessName(pid_t pid) {
#ifdef _WIN32
  HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                            FALSE, pid);
  if (!proc) return "";
  char name[MAX_PATH];
  DWORD size = MAX_PATH;
  if (QueryFullProcessImageNameA(proc, 0, name, &size)) {
    CloseHandle(proc);
    std::string full(name);
    size_t slash = full.rfind('\\');
    return (slash != std::string::npos) ? full.substr(slash + 1) : full;
  }
  CloseHandle(proc);
  return "";
#else
  return "";
#endif
}

std::string WindowsBackend::getProcessCmdline(pid_t pid) {
  return getProcessName(pid);
}

} // namespace havel
