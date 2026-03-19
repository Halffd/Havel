/*
 * WindowQuery.hpp
 *
 * Window query API for Havel language.
 * window.any(), window.count(), window.filter()
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace havel {

/**
 * WindowInfo - Information about a window
 */
struct WindowInfo {
  uint64_t id = 0;
  std::string title;
  std::string windowClass;
  std::string appId;
  std::string exe;
  int pid = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool floating = false;
  bool minimized = false;
  bool maximized = false;
  bool fullscreen = false;
  int workspace = 0;
  bool valid = false;
};

struct WorkspaceInfo {
  int id = 0;
  std::string name;
  bool visible = false;
  int windowCount = 0;
};

/**
 * WindowQuery - Query windows matching conditions
 *
 * Usage:
 *   window.any(exe == "steam.exe")
 *   window.count(class == "discord")
 *   window.filter(title ~ ".*YouTube.*")
 */
class WindowQuery {
public:
  using ConditionFn = std::function<bool(const WindowInfo &)>;

  // Get all windows
  static std::vector<WindowInfo> getAll();

  // Get active window
  static WindowInfo getActive();

  // Check if any window matches condition
  static bool any(ConditionFn condition);

  // Count windows matching condition
  static int count(ConditionFn condition);

  // Filter windows matching condition
  static std::vector<WindowInfo> filter(ConditionFn condition);

  // Find first window matching condition
  static WindowInfo find(ConditionFn condition);
};

} // namespace havel
