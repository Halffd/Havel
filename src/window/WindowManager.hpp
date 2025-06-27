#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>
#include "WindowManagerDetector.hpp"
#include "../utils/Logger.hpp"

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <sys/wait.h>
// Use X11's Window type directly
typedef ::Window XWindow;
#endif
#ifdef WINDOWS
// Struct to hold the window handle and the target process name
struct EnumWindowsData {
    wID id;
    std::string targetProcessName;

    EnumWindowsData(const std::string& processName)
        : id(NULL), targetProcessName(processName) {}
};
#endif

namespace havel {
    struct WindowStats {
        wID id;
        std::string className;
        std::string title;
        bool isFullscreen;
        int x, y, width, height;
    };

class WindowManager {
public:
    WindowManager();
    ~WindowManager() = default;
    static str defaultTerminal;
    static WindowStats activeWindow;
    // Static window methods
    static XWindow GetActiveWindow();
    static XWindow GetwIDByPID(pID pid);
    static XWindow GetwIDByProcessName(cstr processName);
    static XWindow FindByClass(cstr className);
    static XWindow FindByTitle(cstr title);
    static XWindow Find(cstr identifier);
    static XWindow FindWindowInGroup(cstr groupName);
    static XWindow NewWindow(cstr name, std::vector<int>* dimensions = nullptr, bool hide = false);

    // Window manager info
    std::string GetCurrentWMName() const;
    bool IsWMSupported() const;
    bool IsX11() const;
    bool IsWayland() const;
    void All();

    // Group management
    static void AddGroup(cstr groupName, cstr identifier);

    // Window switching
    static void AltTab();
    static void UpdatePreviousActiveWindow();

    // Helper methods
    static str GetIdentifierType(cstr identifier);
    static str GetIdentifierValue(cstr identifier);
    static str getProcessName(pid_t windowPID);

    // Add to WindowManager class
    static void MoveWindow(int direction, int distance = 10);
    static void ResizeWindow(int direction, int distance = 10);
    static void ToggleAlwaysOnTop();
    static void SendToMonitor(int monitorIndex);
    static void SnapWindow(int position);
    static void RotateWindow();
    static void ManageVirtualDesktops(int action);
    static void WindowSpy();
    static void MouseDrag();
    static void ClickThrough();
    static void ToggleClickLock();
    static void AltTabMenu();
    static void WinClose();
    static void WinMinimize();
    static void WinMaximize();
    static void WinRestore();
    static void WinTransparent();
    static void WinMoveResize();
    static void WinSetAlwaysOnTop(bool onTop);
    static void SnapWindowWithPadding(int position, int padding);

    // New method
    static std::string GetActiveWindowClass();
    static void MoveWindowToNextMonitor();
    static void ToggleFullscreen(Display* display, Window win, Atom stateAtom, Atom fsAtom, bool enable);

private:
    static bool InitializeX11();
    std::string DetectWindowManager() const;
    bool CheckWMProtocols() const;
    // Private members
    std::string wmName;
    bool wmSupported{false};
    WindowManagerDetector::WMType wmType{};  // Default initialization

    // Static member to track previous active window
    static XWindow previousActiveWindow;
};
} // namespace havel