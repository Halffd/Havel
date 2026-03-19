/*
 * WindowQuery.cpp
 *
 * Window query API implementation.
 */
#include "WindowQuery.hpp"
#include "WindowManager.hpp"
#include "WindowMonitor.hpp"

namespace havel {

std::vector<WindowInfo> WindowQuery::getAll() {
  // TODO: Implement window enumeration
  // For now, return empty list
  return {};
}

WindowInfo WindowQuery::getActive() {
  WindowInfo info;

  // Get from WindowManager
  info.id = WindowManager::GetActiveWindow();
  info.title = WindowManager::GetActiveWindowTitle();
  info.windowClass = WindowManager::GetActiveWindowClass();
  info.pid = WindowManager::GetActiveWindowPID();
  info.exe = WindowManager::getProcessName(info.pid);
  info.valid = (info.id != 0);

  return info;
}

bool WindowQuery::any(ConditionFn condition) {
  if (!condition)
    return false;

  // Check active window first (most common case)
  auto active = getActive();
  if (active.valid && condition(active)) {
    return true;
  }

  // TODO: Check all windows
  // For now, just check active window
  return false;
}

int WindowQuery::count(ConditionFn condition) {
  if (!condition)
    return 0;

  int count = 0;

  // Check active window
  auto active = getActive();
  if (active.valid && condition(active)) {
    count++;
  }

  // TODO: Count all matching windows
  // For now, just count active window

  return count;
}

std::vector<WindowInfo> WindowQuery::filter(ConditionFn condition) {
  std::vector<WindowInfo> result;

  if (!condition)
    return result;

  // Check active window
  auto active = getActive();
  if (active.valid && condition(active)) {
    result.push_back(active);
  }

  // TODO: Filter all windows
  // For now, just filter active window

  return result;
}

WindowInfo WindowQuery::find(ConditionFn condition) {
  WindowInfo empty;

  if (!condition)
    return empty;

  // Check active window first
  auto active = getActive();
  if (active.valid && condition(active)) {
    return active;
  }

  // TODO: Search all windows
  // For now, just check active window

  return empty;
}

} // namespace havel
