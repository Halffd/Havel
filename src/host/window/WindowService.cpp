#include <functional>
#include <stdexcept>
/*
 * WindowService.cpp
 *
 * Minimal stub implementation - no VM, no interpreter, no HavelValue.
 * The actual window management is now in pure Havel (modules/std/window.hv).
 */
#include "WindowService.hpp"

namespace havel::host {

WindowService::WindowService() {}


WindowInfo WindowService::getActiveWindowInfo() const {
  return WindowInfo{};
}

WindowInfo WindowService::getWindowInfo(uint64_t) const {
  return WindowInfo{};
}

std::vector<WindowInfo> WindowService::getAllWindows() const {
  return {};
}

uint64_t WindowService::getActiveWindow() const {
  return 0;
}

bool WindowService::anyWindow(const std::function<bool(const WindowInfo &)> &) const {
  return false;
}

int WindowService::countWindows(const std::function<bool(const WindowInfo &)> &) const {
  return 0;
}

std::vector<WindowInfo> WindowService::filterWindows(const std::function<bool(const WindowInfo &)> &) const {
  return {};
}

std::string WindowService::getActiveWindowProcess() const { return ""; }
std::string WindowService::getActiveWindowTitle() const { return ""; }
std::string WindowService::getActiveWindowClass() const { return ""; }

bool WindowService::focusWindow(uint64_t) { return false; }
bool WindowService::closeWindow(uint64_t) { return false; }
bool WindowService::moveWindow(uint64_t, int, int) { return false; }
bool WindowService::resizeWindow(uint64_t, int, int) { return false; }
bool WindowService::moveResizeWindow(uint64_t, int, int, int, int) { return false; }
bool WindowService::maximizeWindow(uint64_t) { return false; }
bool WindowService::minimizeWindow(uint64_t) { return false; }
bool WindowService::restoreWindow(uint64_t) { return false; }
bool WindowService::hideWindow(uint64_t) { return false; }
bool WindowService::showWindow(uint64_t) { return false; }
bool WindowService::toggleFullscreen(uint64_t) { return false; }
bool WindowService::setFloating(uint64_t, bool) { return false; }
bool WindowService::centerWindow(uint64_t) { return false; }
bool WindowService::snapWindow(uint64_t, int) { return false; }
bool WindowService::moveWindowToWorkspace(uint64_t, int) { return false; }
bool WindowService::setAlwaysOnTop(uint64_t, bool) { return false; }
bool WindowService::moveWindowToMonitor(uint64_t, int) { return false; }

std::vector<WorkspaceInfo> WindowService::getWorkspaces() const { return {}; }
bool WindowService::switchToWorkspace(int) { return false; }
int WindowService::getCurrentWorkspace() const { return 0; }
std::vector<std::string> WindowService::getGroupNames() const { return {}; }
std::vector<WindowInfo> WindowService::getGroupWindows(const std::string &) const { return {}; }
bool WindowService::addWindowToGroup(uint64_t, const std::string &) { return false; }
bool WindowService::removeWindowFromGroup(uint64_t, const std::string &) { return false; }

void WindowService::moveActiveWindowToNextMonitor() {}
std::string WindowService::getActiveWindowTitleStatic() { return ""; }
std::string WindowService::getActiveWindowClassStatic() { return ""; }
std::string WindowService::getActiveWindowProcessStatic() { return ""; }

} // namespace havel::host
