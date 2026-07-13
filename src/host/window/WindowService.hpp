#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <vector>

namespace havel {
class WindowManager;
}

namespace havel::host {

struct WindowInfo {
    uint64_t id = 0;
    std::string title;
    std::string windowClass;
    std::string className;
    std::string exe;
    int pid = 0;
    std::string cmdline;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool valid = false;
};

struct WorkspaceInfo {
    int id = 0;
    std::string name;
    bool active = false;
};

struct WorkspaceInfo;

class WindowService {
public:
    WindowService();
    ~WindowService() = default;

    struct WindowInfo getActiveWindowInfo() const;
    struct WindowInfo getWindowInfo(uint64_t id) const;
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
};

} // namespace havel::host
