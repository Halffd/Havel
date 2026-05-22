#include "core/window/backends/MacOSBackend.hpp"

namespace havel {

MacOSBackend::MacOSBackend() = default;
MacOSBackend::~MacOSBackend() = default;

DisplayServer MacOSBackend::getDisplayServer() const {
  return DisplayServer::Unknown;
}

WindowManagerDetector::WMType MacOSBackend::getWMType() const {
  return WindowManagerDetector::WMType::UNKNOWN;
}

std::string MacOSBackend::getWMName() const { return "macOS"; }
bool MacOSBackend::isWMSupported() const { return true; }
bool MacOSBackend::initialize() { return true; }
void MacOSBackend::shutdown() {}

wID MacOSBackend::getActiveWindow() { return 0; }
pID MacOSBackend::getActiveWindowPID() { return 0; }
std::string MacOSBackend::getActiveWindowProcess() { return ""; }
std::string MacOSBackend::getActiveWindowTitle() { return ""; }
std::string MacOSBackend::getActiveWindowClass() { return ""; }
pID MacOSBackend::getWindowPID(wID /* id */) { return 0; }
std::string MacOSBackend::getWindowTitle(wID /* id */) { return ""; }
std::string MacOSBackend::getWindowClass(wID /* id */) { return ""; }
Rect MacOSBackend::getWindowPosition(wID /* id */) { return {}; }
bool MacOSBackend::isWindowActive(wID /* id */) { return false; }
bool MacOSBackend::isWindowExists(wID /* id */) { return false; }
bool MacOSBackend::isWindowFullscreen(wID /* id */) { return false; }

wID MacOSBackend::findWindowByPID(pID /* pid */) { return 0; }
wID MacOSBackend::findWindowByProcessName(const std::string &) { return 0; }
wID MacOSBackend::findWindowByClass(const std::string &) { return 0; }
wID MacOSBackend::findWindowByTitle(const std::string &) { return 0; }
wID MacOSBackend::newWindow(const std::string &, std::vector<int> *, bool) { return 0; }

bool MacOSBackend::moveWindow(wID, int, int) { return false; }
bool MacOSBackend::resizeWindow(wID, int, int) { return false; }
bool MacOSBackend::moveResizeWindow(wID, int, int, int, int) { return false; }
bool MacOSBackend::closeWindow(wID) { return false; }
bool MacOSBackend::focusWindow(wID) { return false; }
bool MacOSBackend::minimizeWindow(wID) { return false; }
bool MacOSBackend::maximizeWindow(wID) { return false; }
bool MacOSBackend::restoreWindow(wID) { return false; }
bool MacOSBackend::hideWindow(wID) { return false; }
bool MacOSBackend::showWindow(wID) { return false; }
bool MacOSBackend::setWindowOpacity(wID, float) { return false; }
bool MacOSBackend::setWindowAlwaysOnTop(wID, bool) { return false; }
bool MacOSBackend::toggleWindowFullscreen(wID) { return false; }
bool MacOSBackend::centerWindow(wID) { return false; }
bool MacOSBackend::snapWindow(wID, int, int) { return false; }
bool MacOSBackend::setWindowFloating(wID, bool) { return false; }

int MacOSBackend::getCurrentWorkspace() { return 1; }
std::vector<WorkspaceInfo> MacOSBackend::getWorkspaces() { return {}; }
bool MacOSBackend::switchToWorkspace(int) { return false; }
bool MacOSBackend::moveWindowToWorkspace(wID, int) { return false; }
bool MacOSBackend::moveWindowToMonitor(wID, int) { return false; }

void MacOSBackend::startAltTab() {}
void MacOSBackend::continueAltTab() {}
void MacOSBackend::finishAltTab() {}

std::vector<WindowInfo> MacOSBackend::getAllWindows() { return {}; }
WindowInfo MacOSBackend::getWindowInfo(wID) { return {}; }
WindowInfo MacOSBackend::getActiveWindowInfo() { return {}; }

std::string MacOSBackend::getProcessName(pid_t) { return ""; }
std::string MacOSBackend::getProcessCmdline(pid_t) { return ""; }

} // namespace havel
