#include <functional>
#include <stdexcept>
/*
 * WindowService.cpp
 *
 * Pure C++ window service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "WindowService.hpp"
#include "core/window/WindowManager.hpp"
#include "core/window/WindowQuery.hpp"
#include "core/window/Rect.hpp"
#include "core/window/WindowBackend.hpp"
#include "x11.h"

namespace havel::host {

WindowService::WindowService(havel::WindowManager *manager) : wm_(manager) {}

WindowInfo WindowService::getActiveWindowInfo() const {
  if (!wm_)
    return WindowInfo{};
  return wm_->getActiveWindowInfo();
}

WindowInfo WindowService::getWindowInfo(uint64_t id) const {
  if (!wm_)
    return WindowInfo{};
  return wm_->getWindowInfo(id);
}

WindowInfo WindowService::getWindowAbsolutePosition(uint64_t id) const {
  WindowInfo info;
  info.valid = false;
  if (!wm_ || id == 0)
    return info;
  auto &backend = wm_->getBackend();
  Rect r = backend.getWindowPosition(id);
  if (r.width <= 0 && r.height <= 0 && r.x == 0 && r.y == 0)
    return info; // backend failure sentinel
  info.id = id;
  info.x = r.x;
  info.y = r.y;
  info.width = r.width;
  info.height = r.height;
  info.valid = true;
  return info;
}

std::vector<WindowInfo> WindowService::getAllWindows() const {
  if (!wm_)
    return {};
  return wm_->getAllWindows();
}

uint64_t WindowService::getActiveWindow() const {
  if (!wm_)
    return 0;
  return wm_->getActiveWindow();
}

// =========================================================================
// Window query functions
// =========================================================================

bool WindowService::anyWindow(
    const std::function<bool(const WindowInfo &)> &predicate) const {
  if (!wm_)
    return false;
  auto windows = wm_->getAllWindows();
  for (const auto &win : windows) {
    if (predicate(win))
      return true;
  }
  return false;
}

int WindowService::countWindows(
    const std::function<bool(const WindowInfo &)> &predicate) const {
  if (!wm_)
    return 0;
  auto windows = wm_->getAllWindows();
  int count = 0;
  for (const auto &win : windows) {
    if (predicate(win))
      ++count;
  }
  return count;
}

std::vector<WindowInfo> WindowService::filterWindows(
    const std::function<bool(const WindowInfo &)> &predicate) const {
  if (!wm_)
    return {};
  auto windows = wm_->getAllWindows();
  std::vector<WindowInfo> result;
  for (const auto &win : windows) {
    if (predicate(win))
      result.push_back(win);
  }
  return result;
}

std::string WindowService::getActiveWindowProcess() const {
  if (!wm_)
    return "";
  auto info = wm_->getActiveWindowInfo();
  return info.exe;
}

std::string WindowService::getActiveWindowTitle() const {
  if (!wm_)
    return "";
  auto info = wm_->getActiveWindowInfo();
  return info.title;
}

std::string WindowService::getActiveWindowClass() const {
  if (!wm_)
    return "";
  auto info = wm_->getActiveWindowInfo();
  return info.windowClass;
}

bool WindowService::focusWindow(uint64_t id) {
  return wm_ && wm_->focusWindow(id);
}

bool WindowService::closeWindow(uint64_t id) {
  return wm_ && wm_->closeWindow(id);
}

bool WindowService::moveWindow(uint64_t id, int x, int y) {
  return wm_ && wm_->moveWindow(id, x, y);
}

bool WindowService::resizeWindow(uint64_t id, int width, int height) {
  return wm_ && wm_->resizeWindow(id, width, height);
}

bool WindowService::moveResizeWindow(uint64_t id, int x, int y, int width,
                                     int height) {
  return wm_ && wm_->moveResizeWindow(id, x, y, width, height);
}

bool WindowService::maximizeWindow(uint64_t id) {
  return wm_ && wm_->maximizeWindow(id);
}

bool WindowService::minimizeWindow(uint64_t id) {
  return wm_ && wm_->minimizeWindow(id);
}

bool WindowService::restoreWindow(uint64_t id) {
  return wm_ && wm_->restoreWindow(id);
}

// EWMH toggles delegate straight to the backend
bool WindowService::setSticky(uint64_t id, bool on) { return wm_ && wm_->getBackend().setWindowSticky(static_cast<wID>(id), on); }
bool WindowService::isSticky(uint64_t id) { return wm_ && wm_->getBackend().isWindowSticky(static_cast<wID>(id)); }
bool WindowService::alwaysOnTop(uint64_t id) {
  // EWMH query: StateAbove = _NET_WM_STATE_ABOVE. There's no simple XQuery;
  // we approximate via read of _NET_WM_STATE prop and substring match.
  if (!wm_) return false;
  Display *d = DisplayManager::GetDisplay();
  if (!d) return false;
  Atom stateAtom = XInternAtom(d, "_NET_WM_STATE", x11::XTrue);
  Atom target = XInternAtom(d, "_NET_WM_STATE_ABOVE", x11::XTrue);
  if (stateAtom == x11::XNone || target == x11::XNone) return false;
  Atom actual; int fmt; unsigned long n=0, bytes_after=0; unsigned char *prop=nullptr;
  if (XGetWindowProperty(d, static_cast<Window>(id), stateAtom, 0, 256, x11::XFalse,
                         XA_ATOM, &actual, &fmt, &n, &bytes_after, &prop) != x11::XSuccess || !prop)
    return false;
  bool found = false;
  if (n) {
    Atom *atoms = reinterpret_cast<Atom *>(prop);
    for (unsigned long i = 0; i < n; ++i) {
      if (atoms[i] == target) { found = true; break; }
    }
  }
  if (prop) XFree(prop);
  return found;
}
bool WindowService::setShaded(uint64_t id, bool on) { return wm_ && wm_->getBackend().setWindowShaded(static_cast<wID>(id), on); }
bool WindowService::isShaded(uint64_t id) { return wm_ && wm_->getBackend().isWindowShaded(static_cast<wID>(id)); }
bool WindowService::setSkipTaskbar(uint64_t id, bool on) { return wm_ && wm_->getBackend().setWindowSkipTaskbar(static_cast<wID>(id), on); }
bool WindowService::isSkipTaskbar(uint64_t id) { return wm_ && wm_->getBackend().isWindowSkipTaskbar(static_cast<wID>(id)); }
bool WindowService::setSkipPager(uint64_t id, bool on) { return wm_ && wm_->getBackend().setWindowSkipPager(static_cast<wID>(id), on); }
bool WindowService::isSkipPager(uint64_t id) { return wm_ && wm_->getBackend().isWindowSkipPager(static_cast<wID>(id)); }
bool WindowService::setDecorated(uint64_t id, bool on) { return wm_ && wm_->getBackend().setWindowDecorated(static_cast<wID>(id), on); }
bool WindowService::isDecorated(uint64_t id) { return wm_ && wm_->getBackend().isWindowDecorated(static_cast<wID>(id)); }
bool WindowService::getOpacity(uint64_t id, double &out) { return wm_ && wm_->getBackend().getWindowOpacity(static_cast<wID>(id), out); }
bool WindowService::getFrameExtents(uint64_t id, int &l, int &r, int &t, int &b) { return wm_ && wm_->getBackend().getWindowFrameExtents(static_cast<wID>(id), l, r, t, b); }
std::string WindowService::getWindowType(uint64_t id) { return wm_ ? wm_->getBackend().getWindowType(static_cast<wID>(id)) : "normal"; }
bool WindowService::setOnAllDesktops(uint64_t id) { return wm_ && wm_->getBackend().setWindowOnAllDesktops(static_cast<wID>(id)); }
int WindowService::getWindowDesktop(uint64_t id) { return wm_ ? wm_->getBackend().getWindowDesktop(static_cast<wID>(id)) : -1; }
bool WindowService::terminate(uint64_t id) { return wm_ && wm_->getBackend().terminateWindow(static_cast<wID>(id)); }
bool WindowService::killClient(uint64_t id) { return wm_ && wm_->getBackend().killWindowClient(static_cast<wID>(id)); }
bool WindowService::raise(uint64_t id) { return wm_ && wm_->getBackend().raiseWindow(static_cast<wID>(id)); }
bool WindowService::lower(uint64_t id) { return wm_ && wm_->getBackend().lowerWindow(static_cast<wID>(id)); }

bool WindowService::hideWindow(uint64_t id) {
  if (!wm_)
    return false;
  wm_->hideWindow(id);
  return true;
}

bool WindowService::showWindow(uint64_t id) {
  if (!wm_)
    return false;
  wm_->showWindow(id);
  return true;
}

bool WindowService::toggleFullscreen(uint64_t id) {
  return wm_ && wm_->toggleFullscreen(id);
}

bool WindowService::setFloating(uint64_t id, bool floating) {
  return wm_ && wm_->setFloating(id, floating);
}

bool WindowService::centerWindow(uint64_t id) {
  return wm_ && wm_->centerWindow(id);
}

bool WindowService::snapWindow(uint64_t id, int position) {
  return wm_ && wm_->snapWindow(id, position);
}

bool WindowService::moveWindowToWorkspace(uint64_t id, int workspace) {
  return wm_ && wm_->moveWindowToWorkspace(id, workspace);
}

bool WindowService::setAlwaysOnTop(uint64_t id, bool onTop) {
  return wm_ && wm_->setAlwaysOnTop(id, onTop);
}

bool WindowService::moveWindowToMonitor(uint64_t id, int monitor) {
  return wm_ && wm_->moveWindowToMonitor(id, monitor);
}

std::vector<WorkspaceInfo> WindowService::getWorkspaces() const {
  if (!wm_)
    return {};
  return wm_->getWorkspaces();
}

bool WindowService::switchToWorkspace(int workspace) {
  return wm_ && wm_->switchToWorkspace(workspace);
}

int WindowService::getCurrentWorkspace() const {
  if (!wm_)
    return 0;
  return wm_->getCurrentWorkspace();
}

std::vector<std::string> WindowService::getGroupNames() const {
  if (!wm_)
    return {};
  return wm_->getGroupNames();
}

std::vector<WindowInfo>
WindowService::getGroupWindows(const std::string &groupName) const {
  return WindowManager::getGroupWindows(groupName);
}

bool WindowService::addWindowToGroup(uint64_t id,
                                    const std::string &groupName) {
  return WindowManager::addWindowToGroup(id, groupName);
}

bool WindowService::removeWindowFromGroup(uint64_t id,
                                          const std::string &groupName) {
  return WindowManager::removeWindowFromGroup(id, groupName);
}

// =========================================================================
// Global window operations (static methods)
// =========================================================================

void WindowService::moveActiveWindowToNextMonitor() {
  WindowManager::MoveWindowToNextMonitor();
}

std::string WindowService::getActiveWindowTitleStatic() {
  auto info = WindowQuery::getActive();
  return info.title;
}

std::string WindowService::getActiveWindowClassStatic() {
  auto info = WindowQuery::getActive();
  return info.windowClass;
}

std::string WindowService::getActiveWindowProcessStatic() {
  auto info = WindowQuery::getActive();
  return info.exe;
}

} // namespace havel::host
