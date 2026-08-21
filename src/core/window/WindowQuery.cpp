#include "WindowQuery.hpp"
#include "WindowManager.hpp"

namespace havel {

std::vector<WindowInfo> WindowQuery::getAll() {
  return WindowManager::getAllWindows();
}

WindowInfo WindowQuery::getActive() {
  return WindowManager::getActiveWindowInfo();
}

bool WindowQuery::any(ConditionFn condition) {
  if (!condition) return false;
  auto active = getActive();
  if (active.valid && condition(active)) return true;
  auto all = getAll();
  for (const auto &w : all) {
    if (condition(w)) return true;
  }
  return false;
}

int WindowQuery::count(ConditionFn condition) {
  if (!condition) return 0;
  int count = 0;
  auto all = getAll();
  for (const auto &w : all) {
    if (condition(w)) count++;
  }
  return count;
}

std::vector<WindowInfo> WindowQuery::filter(ConditionFn condition) {
  std::vector<WindowInfo> result;
  if (!condition) return result;
  auto all = getAll();
  for (const auto &w : all) {
    if (condition(w)) result.push_back(w);
  }
  return result;
}

WindowInfo WindowQuery::find(ConditionFn condition) {
  if (!condition) return {};
  auto all = getAll();
  for (const auto &w : all) {
    if (condition(w)) return w;
  }
  return {};
}

} // namespace havel
