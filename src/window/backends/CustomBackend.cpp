#include "window/backends/CustomBackend.hpp"

namespace havel {

CustomBackend::CustomBackend(std::unique_ptr<WindowBackend> wrapped)
  : wrapped_(std::move(wrapped)) {}

CustomBackend::~CustomBackend() = default;

DisplayServer CustomBackend::getDisplayServer() const { return wrapped_->getDisplayServer(); }
WindowManagerDetector::WMType CustomBackend::getWMType() const { return wrapped_->getWMType(); }
std::string CustomBackend::getWMName() const { return wrapped_->getWMName(); }
bool CustomBackend::isWMSupported() const { return wrapped_->isWMSupported(); }

wID CustomBackend::getActiveWindow() { return wrapped_->getActiveWindow(); }
pID CustomBackend::getActiveWindowPID() { return wrapped_->getActiveWindowPID(); }
std::string CustomBackend::getActiveWindowProcess() { return wrapped_->getActiveWindowProcess(); }
std::string CustomBackend::getActiveWindowTitle() { return wrapped_->getActiveWindowTitle(); }
std::string CustomBackend::getActiveWindowClass() { return wrapped_->getActiveWindowClass(); }

pID CustomBackend::getWindowPID(wID id) { return wrapped_->getWindowPID(id); }
std::string CustomBackend::getWindowTitle(wID id) { return wrapped_->getWindowTitle(id); }
std::string CustomBackend::getWindowClass(wID id) { return wrapped_->getWindowClass(id); }
Rect CustomBackend::getWindowPosition(wID id) { return wrapped_->getWindowPosition(id); }
bool CustomBackend::isWindowActive(wID id) { return wrapped_->isWindowActive(id); }
bool CustomBackend::isWindowExists(wID id) { return wrapped_->isWindowExists(id); }
bool CustomBackend::isWindowFullscreen(wID id) { return wrapped_->isWindowFullscreen(id); }

wID CustomBackend::findWindowByPID(pID pid) { return wrapped_->findWindowByPID(pid); }
wID CustomBackend::findWindowByProcessName(const std::string &s) { return wrapped_->findWindowByProcessName(s); }
wID CustomBackend::findWindowByClass(const std::string &s) { return wrapped_->findWindowByClass(s); }
wID CustomBackend::findWindowByTitle(const std::string &s) { return wrapped_->findWindowByTitle(s); }

wID CustomBackend::newWindow(const std::string &n, std::vector<int> *d, bool h) { return wrapped_->newWindow(n, d, h); }

bool CustomBackend::moveWindow(wID id, int x, int y) { return wrapped_->moveWindow(id, x, y); }
bool CustomBackend::resizeWindow(wID id, int w, int h) { return wrapped_->resizeWindow(id, w, h); }
bool CustomBackend::moveResizeWindow(wID id, int x, int y, int w, int h) { return wrapped_->moveResizeWindow(id, x, y, w, h); }
bool CustomBackend::closeWindow(wID id) { return wrapped_->closeWindow(id); }
bool CustomBackend::focusWindow(wID id) { return wrapped_->focusWindow(id); }
bool CustomBackend::minimizeWindow(wID id) { return wrapped_->minimizeWindow(id); }
bool CustomBackend::maximizeWindow(wID id) { return wrapped_->maximizeWindow(id); }
bool CustomBackend::restoreWindow(wID id) { return wrapped_->restoreWindow(id); }
bool CustomBackend::hideWindow(wID id) { return wrapped_->hideWindow(id); }
bool CustomBackend::showWindow(wID id) { return wrapped_->showWindow(id); }
bool CustomBackend::setWindowOpacity(wID id, float o) { return wrapped_->setWindowOpacity(id, o); }
bool CustomBackend::setWindowAlwaysOnTop(wID id, bool o) { return wrapped_->setWindowAlwaysOnTop(id, o); }
bool CustomBackend::toggleWindowFullscreen(wID id) { return wrapped_->toggleWindowFullscreen(id); }
bool CustomBackend::centerWindow(wID id) { return wrapped_->centerWindow(id); }
bool CustomBackend::snapWindow(wID id, int p, int pad) { return wrapped_->snapWindow(id, p, pad); }
bool CustomBackend::setWindowFloating(wID id, bool f) { return wrapped_->setWindowFloating(id, f); }

int CustomBackend::getCurrentWorkspace() { return wrapped_->getCurrentWorkspace(); }
std::vector<WorkspaceInfo> CustomBackend::getWorkspaces() { return wrapped_->getWorkspaces(); }
bool CustomBackend::switchToWorkspace(int w) { return wrapped_->switchToWorkspace(w); }
bool CustomBackend::moveWindowToWorkspace(wID id, int w) { return wrapped_->moveWindowToWorkspace(id, w); }

bool CustomBackend::moveWindowToMonitor(wID id, int m) { return wrapped_->moveWindowToMonitor(id, m); }

void CustomBackend::startAltTab() { wrapped_->startAltTab(); }
void CustomBackend::continueAltTab() { wrapped_->continueAltTab(); }
void CustomBackend::finishAltTab() { wrapped_->finishAltTab(); }

std::vector<WindowInfo> CustomBackend::getAllWindows() { return wrapped_->getAllWindows(); }
WindowInfo CustomBackend::getWindowInfo(wID id) { return wrapped_->getWindowInfo(id); }
WindowInfo CustomBackend::getActiveWindowInfo() { return wrapped_->getActiveWindowInfo(); }

std::string CustomBackend::getProcessName(pid_t p) { return wrapped_->getProcessName(p); }
std::string CustomBackend::getProcessCmdline(pid_t p) { return wrapped_->getProcessCmdline(p); }

bool CustomBackend::initialize() { return wrapped_->initialize(); }
void CustomBackend::shutdown() { wrapped_->shutdown(); }

} // namespace havel
