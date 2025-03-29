#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>
#include "WindowManagerDetector.hpp"

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <sys/wait.h>

// Use X11's Window type directly
typedef ::Window XWindow;
#else
typedef unsigned long XWindow;
#endif

namespace H {

class WindowManager {
public:
    WindowManager();
    ~WindowManager() = default;

    // Static window methods
    static XWindow GetActiveWindow();
    static XWindow GetwIDByPID(pID pid);
    static XWindow GetwIDByProcessName(cstr processName);
    static XWindow FindByClass(cstr className);
    static XWindow FindByTitle(cstr title);
    static XWindow Find(cstr identifier);
    static XWindow FindWindowInGroup(cstr groupName);
    static XWindow NewWindow(cstr name, std::vector<int>* dimensions = nullptr, bool hide = false);

    // Process management
    static ProcessMethodType toMethod(cstr method);
    static void SetPriority(int priority, pID procID = 0);
    static int64_t Terminal(cstr command, bool canPause, str windowState, bool continueExecution, cstr terminal = "");
    
    template<typename T>
    static int64_t Run(str path, T method, str windowState, str command, int priority);

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

private:
    static bool InitializeX11();
    std::string DetectWindowManager() const;
    bool CheckWMProtocols() const;

    // Private members
    std::string wmName;
    bool wmSupported{false};
    WindowManagerDetector::WMType wmType{};  // Default initialization
};

// Template definition
template<typename T>
int64_t WindowManager::Run(str path, T method, str windowState, str command, int priority) {
#ifdef __linux__
    (void)windowState; // Suppress unused parameter warning
    (void)priority;    // Suppress unused parameter warning

    ProcessMethodType processMethod;
    if constexpr (std::is_same_v<T, ProcessMethodType>) {
        processMethod = method;
    } else if constexpr (std::is_same_v<T, int>) {
        processMethod = static_cast<ProcessMethodType>(method);
    } else if constexpr (std::is_same_v<T, str>) {
        processMethod = toMethod(method);
    } else {
        return -1;
    }

    // Implementation of process creation logic
    switch (processMethod) {
        case ProcessMethodType::WaitForTerminate: {
            pid_t childPid = ::fork();
            if (childPid == 0) {
                // Child process
                ::execl(path.c_str(), path.c_str(), command.c_str(), nullptr);
                ::exit(1);
            } else if (childPid > 0) {
                // Parent process
                int status;
                ::waitpid(childPid, &status, 0);
                return static_cast<int64_t>(childPid);
            }
            return -1;
        }
        
        case ProcessMethodType::ForkProcess: {
            pid_t childPid = ::fork();
            if (childPid == 0) {
                // Child process
                ::execl(path.c_str(), path.c_str(), command.c_str(), nullptr);
                ::exit(1);
            } else if (childPid > 0) {
                // Parent process
                return static_cast<int64_t>(childPid);
            }
            return -1;
        }
        
        // Add other cases as needed...
        
        default:
            return -1;
    }
#else
    (void)path;       // Suppress unused parameter warnings
    (void)method;
    (void)windowState;
    (void)command;
    (void)priority;
    return -1;
#endif
}

} // namespace H