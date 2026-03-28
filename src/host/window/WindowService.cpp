/*
 * WindowService.cpp
 *
 * Pure C++ window service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "WindowService.hpp"
#include "window/WindowManager.hpp"
#include "window/WindowQuery.hpp"

namespace havel::host {

WindowService::WindowService(havel::WindowManager* manager)
    : wm_(manager) {}

WindowInfo WindowService::getActiveWindowInfo() const {
    if (!wm_) return WindowInfo{};
    return wm_->getActiveWindowInfo();
}

WindowInfo WindowService::getWindowInfo(uint64_t id) const {
    if (!wm_) return WindowInfo{};
    return wm_->getWindowInfo(id);
}

std::vector<WindowInfo> WindowService::getAllWindows() const {
    if (!wm_) return {};
    return wm_->getAllWindows();
}

uint64_t WindowService::getActiveWindow() const {
    if (!wm_) return 0;
    return wm_->getActiveWindow();
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

bool WindowService::moveResizeWindow(uint64_t id, int x, int y, int width, int height) {
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

bool WindowService::hideWindow(uint64_t id) {
    if (!wm_) return false;
    wm_->hideWindow(id);
    return true;
}

bool WindowService::showWindow(uint64_t id) {
    if (!wm_) return false;
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
    if (!wm_) return {};
    return wm_->getWorkspaces();
}

bool WindowService::switchToWorkspace(int workspace) {
    return wm_ && wm_->switchToWorkspace(workspace);
}

int WindowService::getCurrentWorkspace() const {
    if (!wm_) return 0;
    return wm_->getCurrentWorkspace();
}

std::vector<std::string> WindowService::getGroupNames() const {
    if (!wm_) return {};
    return wm_->getGroupNames();
}

std::vector<WindowInfo> WindowService::getGroupWindows(const std::string& groupName) const {
    if (!wm_) return {};
    auto names = wm_->getGroupWindows(groupName);
    std::vector<WindowInfo> result;
    return result;
}

bool WindowService::addWindowToGroup(uint64_t id, const std::string& groupName) {
    return false;
}

bool WindowService::removeWindowFromGroup(uint64_t id, const std::string& groupName) {
    return false;
}

void WindowService::moveActiveWindowToNextMonitor() {
    havel::WindowManager::MoveWindowToNextMonitor();
}

std::string WindowService::getActiveWindowTitle() {
    return havel::WindowManager::GetActiveWindowTitle();
}

std::string WindowService::getActiveWindowClass() {
    return havel::WindowManager::GetActiveWindowClass();
}

std::string WindowService::getActiveWindowProcess() {
    return havel::WindowManager::GetActiveWindowProcess();
}

} // namespace havel::host
