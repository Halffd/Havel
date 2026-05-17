#include "window/backends/WaylandBackend.hpp"
#include "utils/Logger.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <memory>

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
  return result;
}

std::string WaylandBackend::ReadProcFile(const std::string &path) const {
  std::ifstream file(path);
  if (!file.is_open()) return "";
  std::string content;
  std::getline(file, content);
  return content;
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
  return "";
}

std::string WaylandBackend::getActiveWindowClass() {
  if (compositorBridge && compositorBridge->IsAvailable()) {
    auto info = compositorBridge->GetActiveWindow();
    if (info.valid) return info.appId;
  }
  return "";
}

pID WaylandBackend::getWindowPID(wID id) {
  return getActiveWindowPID();
}

std::string WaylandBackend::getWindowTitle(wID id) {
  return getActiveWindowTitle();
}

std::string WaylandBackend::getWindowClass(wID id) {
  return getActiveWindowClass();
}

Rect WaylandBackend::getWindowPosition(wID /* id */) {
  return {};
}

bool WaylandBackend::isWindowActive(wID id) {
  return getActiveWindow() == id;
}

bool WaylandBackend::isWindowExists(wID /* id */) {
  return false;
}

bool WaylandBackend::isWindowFullscreen(wID /* id */) {
  return false;
}

wID WaylandBackend::findWindowByPID(pID /* pid */) {
  return 0;
}

wID WaylandBackend::findWindowByProcessName(const std::string & /* processName */) {
  return 0;
}

wID WaylandBackend::findWindowByClass(const std::string & /* className */) {
  return 0;
}

wID WaylandBackend::findWindowByTitle(const std::string & /* title */) {
  return 0;
}

wID WaylandBackend::newWindow(const std::string & /* name */,
                               std::vector<int> * /* dimensions */,
                               bool /* hide */) {
  return 0;
}

bool WaylandBackend::moveWindow(wID /* id */, int /* x */, int /* y */) {
  return false;
}

bool WaylandBackend::resizeWindow(wID /* id */, int /* width */, int /* height */) {
  return false;
}

bool WaylandBackend::moveResizeWindow(wID /* id */, int /* x */, int /* y */,
                                       int /* width */, int /* height */) {
  return false;
}

bool WaylandBackend::closeWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::focusWindow(wID id) {
  if (id) {
    std::ostringstream cmd;
    cmd << "wmctrl -i -a " << std::hex << reinterpret_cast<uintptr_t>(static_cast<size_t>(id));
    ExecCmd(cmd.str());
    return true;
  }
  return false;
}

bool WaylandBackend::minimizeWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::maximizeWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::restoreWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::hideWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::showWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::setWindowOpacity(wID /* id */, float /* opacity */) {
  return false;
}

bool WaylandBackend::setWindowAlwaysOnTop(wID /* id */, bool /* onTop */) {
  return false;
}

bool WaylandBackend::toggleWindowFullscreen(wID /* id */) {
  return false;
}

bool WaylandBackend::centerWindow(wID /* id */) {
  return false;
}

bool WaylandBackend::snapWindow(wID /* id */, int /* position */, int /* padding */) {
  return false;
}

bool WaylandBackend::setWindowFloating(wID /* id */, bool /* floating */) {
  return false;
}

int WaylandBackend::getCurrentWorkspace() {
  return 1;
}

std::vector<WorkspaceInfo> WaylandBackend::getWorkspaces() {
  std::vector<WorkspaceInfo> workspaces;
  for (int i = 1; i <= 4; i++) {
    WorkspaceInfo ws;
    ws.id = i;
    ws.name = "Workspace " + std::to_string(i);
    ws.visible = (i == 1);
    ws.windowCount = 0;
    workspaces.push_back(ws);
  }
  return workspaces;
}

bool WaylandBackend::switchToWorkspace(int /* workspace */) {
  return false;
}

bool WaylandBackend::moveWindowToWorkspace(wID /* id */, int /* workspace */) {
  return false;
}

bool WaylandBackend::moveWindowToMonitor(wID /* id */, int /* monitor */) {
  return false;
}

void WaylandBackend::startAltTab() {}
void WaylandBackend::continueAltTab() {}
void WaylandBackend::finishAltTab() {}

std::vector<WindowInfo> WaylandBackend::getAllWindows() {
  return {};
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
