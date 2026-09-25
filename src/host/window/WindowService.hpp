#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <vector>

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

    struct WindowInfo getActiveWindowInfo() const;
    struct WindowInfo getWindowInfo(uint64_t id) const;
    // Absolute (root-window) coordinates. Returns invalid WindowInfo on failure.
    struct WindowInfo getWindowAbsolutePosition(uint64_t id) const;
    std::vector<WindowInfo> getAllWindows() const;
    uint64_t getActiveWindow() const;

    bool anyWindow(const std::function<bool(const WindowInfo &)> &predicate) const;
    int countWindows(const std::function<bool(const WindowInfo &)> &predicate) const;
    std::vector<WindowInfo> filterWindows(const std::function<bool(const WindowInfo &)> &predicate) const;

    std::string getActiveWindowProcess() const;
    std::string getActiveWindowTitle() const;
    std::string getActiveWindowClass() const;

    bool focusWindow(uint64_t id);
    bool closeWindow(uint64_t id);
    bool moveWindow(uint64_t id, int x, int y);
    bool resizeWindow(uint64_t id, int width, int height);
    bool moveResizeWindow(uint64_t id, int x, int y, int width, int height);
    bool maximizeWindow(uint64_t id);
    bool minimizeWindow(uint64_t id);
    bool restoreWindow(uint64_t id);
    bool hideWindow(uint64_t id);
    bool showWindow(uint64_t id);
    bool toggleFullscreen(uint64_t id);
    bool setFloating(uint64_t id, bool floating);
    bool centerWindow(uint64_t id);
    bool snapWindow(uint64_t id, int position);
    bool moveWindowToWorkspace(uint64_t id, int workspace);
    bool setAlwaysOnTop(uint64_t id, bool onTop);
    bool moveWindowToMonitor(uint64_t id, int monitor);

    // EWMH state toggles — delegate to backend; return false when unimplemented.
    bool setSticky(uint64_t id, bool on);
    bool isSticky(uint64_t id);
    bool setShaded(uint64_t id, bool on);
    bool isShaded(uint64_t id);
    bool setSkipTaskbar(uint64_t id, bool on);
    bool isSkipTaskbar(uint64_t id);
    bool setSkipPager(uint64_t id, bool on);
    bool isSkipPager(uint64_t id);
    bool alwaysOnTop(uint64_t id);  // query; set via setAlwaysOnTop(id,onTop)
    bool setDecorated(uint64_t id, bool on);
    bool isDecorated(uint64_t id);
    bool getOpacity(uint64_t id, double &out);
    bool getFrameExtents(uint64_t id, int &l, int &r, int &t, int &b);
    std::string getWindowType(uint64_t id);
    bool setOnAllDesktops(uint64_t id);
    int getWindowDesktop(uint64_t id);
    bool terminate(uint64_t id);
    bool killClient(uint64_t id);
    bool raise(uint64_t id);
    bool lower(uint64_t id);

    std::vector<WorkspaceInfo> getWorkspaces() const;
    bool switchToWorkspace(int workspace);
    int getCurrentWorkspace() const;
    std::vector<std::string> getGroupNames() const;
    std::vector<WindowInfo> getGroupWindows(const std::string &groupName) const;
    bool addWindowToGroup(uint64_t id, const std::string &groupName);
    bool removeWindowFromGroup(uint64_t id, const std::string &groupName);

    static void moveActiveWindowToNextMonitor();
    static std::string getActiveWindowTitleStatic();
    static std::string getActiveWindowClassStatic();
    static std::string getActiveWindowProcessStatic();

private:
  havel::WindowManager *wm_; // Non-owning pointer
};

} // namespace havel::host
