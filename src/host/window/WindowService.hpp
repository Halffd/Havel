#include <functional>
#include <stdexcept>
/*
 * WindowService.hpp
 *
 * Pure C++ window service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for window operations.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Use existing WindowInfo from window query
namespace havel {
struct WindowInfo;
struct WorkspaceInfo;
class WindowManager;
} // namespace havel

namespace havel::host {

// Re-export for convenience
using ::havel::WindowInfo;
using ::havel::WorkspaceInfo;

/**
 * WindowService - Pure window business logic
 *
 * Provides system-level window operations without any language runtime
 * coupling. All methods return simple C++ types (bool, int, string, vector,
 * etc.)
 */
class WindowService {
public:
  explicit WindowService(havel::WindowManager *manager);
  ~WindowService() = default;

  // =========================================================================
  // Window queries
  // =========================================================================

  /// Get active window info
  /// @return WindowInfo (valid=false if no active window)
  WindowInfo getActiveWindowInfo() const;

  /// Get window info by ID
  /// @param id Window ID
  /// @return WindowInfo (valid=false if not found)
  WindowInfo getWindowInfo(uint64_t id) const;

  /// Get all windows
  /// @return vector of WindowInfo
  std::vector<WindowInfo> getAllWindows() const;

  /// Get active window ID
  /// @return window ID (0 if none)
  uint64_t getActiveWindow() const;

  // =========================================================================
  // Window query functions (for Havel window.* namespace)
  // =========================================================================

  /// Check if any window matches predicate
  /// @param predicate Function that takes WindowInfo and returns bool
  /// @return true if any window matches
  bool
  anyWindow(const std::function<bool(const WindowInfo &)> &predicate) const;

  /// Count windows matching predicate
  /// @param predicate Function that takes WindowInfo and returns bool
  /// @return count of matching windows
  int countWindows(
      const std::function<bool(const WindowInfo &)> &predicate) const;

  /// Filter windows by predicate
  /// @param predicate Function that takes WindowInfo and returns bool
  /// @return vector of matching WindowInfo
  std::vector<WindowInfo>
  filterWindows(const std::function<bool(const WindowInfo &)> &predicate) const;

  /// Get active window process name (exe)
  /// @return process name or empty string
  std::string getActiveWindowProcess() const;

  /// Get active window title
  /// @return window title or empty string
  std::string getActiveWindowTitle() const;

  /// Get active window class
  /// @return window class or empty string
  std::string getActiveWindowClass() const;

  // =========================================================================
  // Window control
  // =========================================================================

  /// Focus a window
  /// @param id Window ID
  /// @return true on success
  bool focusWindow(uint64_t id);

  /// Close a window
  /// @param id Window ID
  /// @return true on success
  bool closeWindow(uint64_t id);

  /// Move window to position
  /// @param id Window ID
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @return true on success
  bool moveWindow(uint64_t id, int x, int y);

  /// Resize window
  /// @param id Window ID
  /// @param width New width
  /// @param height New height
  /// @return true on success
  bool resizeWindow(uint64_t id, int width, int height);

  /// Move and resize window
  /// @param id Window ID
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @param width New width
  /// @param height New height
  /// @return true on success
  bool moveResizeWindow(uint64_t id, int x, int y, int width, int height);

  /// Maximize window
  /// @param id Window ID
  /// @return true on success
  bool maximizeWindow(uint64_t id);

  /// Minimize window
  /// @param id Window ID
  /// @return true on success
  bool minimizeWindow(uint64_t id);

  /// Restore minimized window
  /// @param id Window ID
  /// @return true on success
  bool restoreWindow(uint64_t id);

  /// Hide window (remove from taskbar/desktop but don't close)
  /// @param id Window ID
  /// @return true on success
  bool hideWindow(uint64_t id);

  /// Show previously hidden window
  /// @param id Window ID
  /// @return true on success
  bool showWindow(uint64_t id);

  /// Toggle fullscreen
  /// @param id Window ID
  /// @return true on success
  bool toggleFullscreen(uint64_t id);

  /// Set floating state
  /// @param id Window ID
  /// @param floating True for floating, false for tiled
  /// @return true on success
  bool setFloating(uint64_t id, bool floating);

  /// Center window
  /// @param id Window ID
  /// @return true on success
  bool centerWindow(uint64_t id);

  /// Snap window to position
  /// @param id Window ID
  /// @param position Position code (0-3 for corners, etc.)
  /// @return true on success
  bool snapWindow(uint64_t id, int position);

  /// Move window to workspace
  /// @param id Window ID
  /// @param workspace Workspace number
  /// @return true on success
  bool moveWindowToWorkspace(uint64_t id, int workspace);

  /// Set always on top
  /// @param id Window ID
  /// @param onTop True to set on top
  /// @return true on success
  bool setAlwaysOnTop(uint64_t id, bool onTop);

  /// Move window to monitor
  /// @param id Window ID
  /// @param monitor Monitor index
  /// @return true on success
  bool moveWindowToMonitor(uint64_t id, int monitor);

  // =========================================================================
  // Workspace operations
  // =========================================================================

  /// Get all workspaces
  /// @return vector of WorkspaceInfo
  std::vector<WorkspaceInfo> getWorkspaces() const;

  /// Switch to workspace
  /// @param workspace Workspace number
  /// @return true on success
  bool switchToWorkspace(int workspace);

  /// Get current workspace
  /// @return workspace number
  int getCurrentWorkspace() const;

  // =========================================================================
  // Window grouping
  // =========================================================================

  /// Get all group names
  /// @return vector of group names
  std::vector<std::string> getGroupNames() const;

  /// Get windows in a group
  /// @param groupName Group name
  /// @return vector of WindowInfo
  std::vector<WindowInfo> getGroupWindows(const std::string &groupName) const;

  /// Add window to group
  /// @param id Window ID
  /// @param groupName Group name
  /// @return true on success
  bool addWindowToGroup(uint64_t id, const std::string &groupName);

  /// Remove window from group
  /// @param id Window ID
  /// @param groupName Group name
  /// @return true on success
  bool removeWindowFromGroup(uint64_t id, const std::string &groupName);

  // =========================================================================
  // Global window operations (no window ID needed)
  // =========================================================================

  /// Move active window to next monitor
  static void moveActiveWindowToNextMonitor();

  /// Get active window title (static version for host bridge)
  /// @return window title
  static std::string getActiveWindowTitleStatic();

  /// Get active window class (static version for host bridge)
  /// @return window class
  static std::string getActiveWindowClassStatic();

  /// Get active window process name (static version for host bridge)
  /// @return process name
  static std::string getActiveWindowProcessStatic();

private:
  havel::WindowManager *wm_; // Non-owning pointer
};

} // namespace havel::host
