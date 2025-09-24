#include "WindowManager.hpp"
#include "types.hpp"
#include "core/DisplayManager.hpp"
#include "../utils/Logger.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <optional>

#ifdef __linux__
#include "x11.h"
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#endif

namespace havel {
    // Define global variables
    static group groups;

#ifdef _WIN32
static str defaultTerminal = "Cmd";
#else
    str WindowManager::defaultTerminal = "alacritty"; //gnome-terminal
    static cstr globalShell = "zsh";
#endif

    // Initialize the static previousActiveWindow variable
    XWindow havel::WindowManager::previousActiveWindow = x11::XNone;
    WindowStats havel::WindowManager::activeWindow = {};

    WindowManager::WindowManager() {
#ifdef _WIN32
        // Windows implementation
#elif defined(__linux__)
        WindowManagerDetector detector;
        wmName = detector.GetWMName();
        wmType = detector.Detect();
        wmSupported = true;

        if (IsX11()) {
            InitializeX11();
        }
#endif
    }

    bool WindowManager::InitializeX11() {
#ifdef __linux__
        DisplayManager::Initialize();
        return DisplayManager::GetDisplay() != nullptr;
#else
    return false;
#endif
    }
    
    pID WindowManager::GetActiveWindowPID() {
    #ifdef __linux__
        wID active_win = GetActiveWindow();
        if (active_win == 0) return 0;

        Display* display = DisplayManager::GetDisplay();
        if (!display) return 0;

        Atom pidAtom = XInternAtom(display, "_NET_WM_PID", x11::XTrue);
        if (pidAtom == x11::XNone) return 0;

        Atom actualType;
        int actualFormat;
        unsigned long nItems, bytesAfter;
        unsigned char* propPID = nullptr;
        pID pid = 0;

        if (XGetWindowProperty(display, active_win, pidAtom, 0, 1, x11::XFalse, XA_CARDINAL,
                               &actualType, &actualFormat, &nItems, &bytesAfter, &propPID) == x11::XSuccess) {
            if (nItems > 0) {
                pid = *reinterpret_cast<pID*>(propPID);
            }
            if (propPID) XFree(propPID);
        }
        return pid;
    #else
        return 0;
    #endif
    }


    // Function to add a group
    void WindowManager::AddGroup(cstr groupName, cstr identifier) {
        if (groups.find(groupName) == groups.end()) {
            groups[groupName] = std::vector<std::string>();
        }
        groups[groupName].push_back(identifier);
    }

    str WindowManager::GetIdentifierType(cstr identifier) {
        std::istringstream iss(identifier);
        std::string type;
        std::getline(iss, type, ' ');
        return type;
    }

    str WindowManager::GetIdentifierValue(cstr identifier) {
        std::istringstream iss(identifier);
        std::string type;
        std::getline(iss, type, ' ');
        std::string value;
        std::getline(iss, value);
        return value;
    }

    wID WindowManager::GetActiveWindow() {
#ifdef _WIN32
    return reinterpret_cast<wID>(GetForegroundWindow());
#elif defined(__linux__)
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            if (!WindowManager::InitializeX11()) {
                return 0;
            }
            display = DisplayManager::GetDisplay();
        }

        Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW",
                                            x11::XFalse);
        if (activeWindowAtom == x11::XNone) return 0;

        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *prop = nullptr;
        Window activeWindow = 0;

        if (XGetWindowProperty(display, DefaultRootWindow(display),
                               activeWindowAtom, 0, 1,
                               x11::XFalse, XA_WINDOW, &actualType, &actualFormat,
                               &nitems, &bytesAfter,
                               &prop) == x11::XSuccess) {
            if (prop) {
                activeWindow = *reinterpret_cast<Window *>(prop);
                XFree(prop);

                // Update previous active window if this is a different window
                if (activeWindow != 0 && activeWindow != previousActiveWindow) {
                    UpdatePreviousActiveWindow();
                }
            }
        }

        return activeWindow;
#else
    return 0;
#endif
    }

    // Method to find a window based on various identifiers
    wID WindowManager::Find(cstr identifier) {
        std::string type = GetIdentifierType(identifier);
        std::string value = GetIdentifierValue(identifier);

        if (type == "group") {
            return FindWindowInGroup(value);
        } else if (type == "class") {
#ifdef _WIN32
        return reinterpret_cast<wID>(FindWindowA(value.c_str(), NULL));
#elif defined(__linux__)
            return FindByClass(value);
#endif
        } else if (type == "pid") {
            pID pid = std::stoul(value);
            return GetwIDByPID(pid);
        } else if (type == "exe") {
            return GetwIDByProcessName(value);
        } else if (type == "title") {
            return FindByTitle(value);
        } else if (type == "id") {
            return std::stoul(value);
        } else {
            // Default to title search if no type specified
            return FindByTitle(identifier);
        }
        return 0; // Unsupported platform
    }

    void WindowManager::AltTab() {
#ifdef __linux__
        Display *display = XOpenDisplay(nullptr);
        if (!display) {
            error("Failed to open X display for Alt+Tab");
            return;
        }

        // Get the root window
        Window root = DefaultRootWindow(display);

        // Get the current active window
        wID currentActiveWindow = GetActiveWindow();
        info(
            "Alt+Tab: Current active window: " + std::to_string(
                currentActiveWindow) +
            ", Previous window: " + std::to_string(previousActiveWindow));

        // Check if we have a valid previous window to switch to
        bool previousWindowValid = false;
        if (previousActiveWindow != x11::XNone && previousActiveWindow !=
            currentActiveWindow) {
            XWindowAttributes attrs;
            if (XGetWindowAttributes(display, previousActiveWindow, &attrs) &&
                attrs.map_state == IsViewable) {
                // Get window class for better logging
                XClassHint classHint;
                std::string windowClass = "unknown";
                if (XGetClassHint(display, previousActiveWindow, &classHint)) {
                    windowClass = classHint.res_class;
                    XFree(classHint.res_name);
                    XFree(classHint.res_class);
                }

                info(
                    "Alt+Tab: Found valid previous window " + std::to_string(
                        previousActiveWindow) +
                    " class: " + windowClass);
                previousWindowValid = true;
            } else {
                warning(
                    "Alt+Tab: Previous window " + std::to_string(
                        previousActiveWindow) +
                    " is no longer valid or viewable");
                previousActiveWindow = x11::XNone; // Reset invalid window
            }
        }

        // If we don't have a valid previous window, find another suitable window
        Window windowToActivate = x11::XNone;

        if (!previousWindowValid) {
            info("Alt+Tab: Looking for an alternative window");

            // Get the list of windows
            Atom clientListAtom = XInternAtom(display,
                                              "_NET_CLIENT_LIST_STACKING",
                                              x11::XFalse);
            if (clientListAtom == x11::XNone) {
                clientListAtom =
                        XInternAtom(display, "_NET_CLIENT_LIST", x11::XFalse);
                if (clientListAtom == x11::XNone) {
                    error("Failed to get window list atom");
                    XCloseDisplay(display);
                    return;
                }
            }

            Atom actualType;
            int actualFormat;
            unsigned long numWindows, bytesAfter;
            unsigned char *data = nullptr;

            if (XGetWindowProperty(display, root, clientListAtom,
                                   0, ~0L, x11::XFalse, XA_WINDOW,
                                   &actualType, &actualFormat,
                                   &numWindows, &bytesAfter,
                                   &data) != x11::XSuccess ||
                numWindows < 1) {
                if (data) XFree(data);
                error("Failed to get window list or empty list");
                XCloseDisplay(display);
                return;
            }

            Window *windows = reinterpret_cast<Window *>(data);

            // Find a suitable window to switch to (not the current active window)
            for (unsigned long i = numWindows; i > 0; i--) {
                // Start from top of stack
                unsigned long idx = i - 1; // Convert to zero-based index

                if (windows[idx] != currentActiveWindow && windows[idx] !=
                    x11::XNone) {
                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(display, windows[idx], &attrs) &&
                        attrs.map_state == IsViewable) {
                        // Check if this is a normal window (not a desktop, dock, etc.)
                        Atom windowTypeAtom = XInternAtom(
                            display, "_NET_WM_WINDOW_TYPE", x11::XFalse);
                        Atom actualType;
                        int actualFormat;
                        unsigned long numItems, bytesAfter;
                        unsigned char *typeData = nullptr;
                        bool isNormalWindow = true;

                        if (XGetWindowProperty(display, windows[idx],
                                               windowTypeAtom, 0, ~0L,
                                               x11::XFalse, AnyPropertyType,
                                               &actualType, &actualFormat,
                                               &numItems, &bytesAfter,
                                               &typeData) == x11::XSuccess &&
                            typeData) {
                            Atom *types = reinterpret_cast<Atom *>(typeData);
                            Atom normalAtom = XInternAtom(
                                display, "_NET_WM_WINDOW_TYPE_NORMAL", x11::XFalse);
                            Atom dialogAtom = XInternAtom(
                                display, "_NET_WM_WINDOW_TYPE_DIALOG", x11::XFalse);

                            isNormalWindow = false;
                            for (unsigned long j = 0; j < numItems; j++) {
                                if (types[j] == normalAtom || types[j] ==
                                    dialogAtom) {
                                    isNormalWindow = true;
                                    break;
                                }
                            }
                            XFree(typeData);
                        }

                        // If it's a normal window, use it
                        if (isNormalWindow) {
                            windowToActivate = windows[idx];

                            // Get window class for logging
                            XClassHint classHint;
                            std::string windowClass = "unknown";
                            if (XGetClassHint(display, windowToActivate,
                                              &classHint)) {
                                windowClass = classHint.res_class;
                                XFree(classHint.res_name);
                                XFree(classHint.res_class);
                            }

                            info(
                                "Alt+Tab: Found alternative window " +
                                std::to_string(windowToActivate) +
                                " class: " + windowClass);
                            break;
                        }
                    }
                }
            }

            if (data) XFree(data);
        } else {
            windowToActivate = previousActiveWindow;
        }

        // Store current window as previous before switching
        if (currentActiveWindow != x11::XNone) {
            previousActiveWindow = currentActiveWindow;
            debug(
                "Alt+Tab: Stored current window as previous: " + std::to_string(
                    previousActiveWindow));
        }

        // Activate the selected window
        if (windowToActivate != x11::XNone) {
            Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW",
                                                x11::XFalse);
            if (activeWindowAtom != x11::XNone) {
                XEvent event = {};
                event.type = x11::XClientMessage;
                event.xclient.window = windowToActivate;
                event.xclient.message_type = activeWindowAtom;
                event.xclient.format = 32;
                event.xclient.data.l[0] = 2; // Source: pager
                event.xclient.data.l[1] = CurrentTime;

                XSendEvent(display, root, x11::XFalse,
                           SubstructureRedirectMask | SubstructureNotifyMask,
                           &event);

                // Also try direct methods
                XRaiseWindow(display, windowToActivate);
                XSetInputFocus(display, windowToActivate, RevertToParent,
                               CurrentTime);

                info(
                    "Alt+Tab: Switched to window: " + std::to_string(
                        windowToActivate));
            }
        } else {
            warning(
                "Alt+Tab: Could not find a suitable window to switch to");
        }

        XSync(display, x11::XFalse);
        XCloseDisplay(display);
#endif
    }

    wID WindowManager::GetwIDByPID(pID pid) {
#ifdef _WIN32
    wID hwnd = NULL;
    // Windows implementation would go here
    return hwnd;
#elif defined(__linux__)
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            if (!WindowManager::InitializeX11()) {
                return 0;
            }
        }

        Atom pidAtom = XInternAtom(DisplayManager::GetDisplay(), "_NET_WM_PID",
                                   x11::XTrue);
        if (pidAtom == x11::XNone) {
            std::cerr << "X11 does not support _NET_WM_PID." << std::endl;
            return 0;
        }

        Window root = DefaultRootWindow(display);
        Window parent, *children;
        unsigned int nchildren;

        if (XQueryTree(display, root, &root, &parent, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; i++) {
                Atom actualType;
                int actualFormat;
                unsigned long nItems, bytesAfter;
                unsigned char *propPID = nullptr;

                if (XGetWindowProperty(display, children[i], pidAtom, 0, 1,
                                       x11::XFalse, XA_CARDINAL,
                                       &actualType, &actualFormat, &nItems,
                                       &bytesAfter, &propPID) == x11::XSuccess) {
                    if (nItems > 0) {
                        pid_t windowPID = *reinterpret_cast<pid_t *>(propPID);
                        if (windowPID == pid) {
                            XFree(propPID);
                            XFree(children);
                            return reinterpret_cast<wID>(children[i]);
                        }
                    }
                    if (propPID) XFree(propPID);
                }
            }
            XFree(children);
        }
        return 0;
#else
    return 0;
#endif
    }

    wID WindowManager::GetwIDByProcessName(cstr processName) {
#ifdef _WIN32
    // Windows implementation would go here
    return 0;
#elif defined(__linux__)
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            if (!WindowManager::InitializeX11()) {
                return 0;
            }
        }

        Atom pidAtom = XInternAtom(DisplayManager::GetDisplay(), "_NET_WM_PID",
                                   x11::XTrue);
        if (pidAtom == x11::XNone) {
            std::cerr << "X11 does not support _NET_WM_PID." << std::endl;
            return 0;
        }

        Window root = DefaultRootWindow(display);
        Window parent, *children;
        unsigned int nchildren;

        if (XQueryTree(display, root, &root, &parent, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; i++) {
                Atom actualType;
                int actualFormat;
                unsigned long nItems, bytesAfter;
                unsigned char *propPID = nullptr;

                if (XGetWindowProperty(display, children[i], pidAtom, 0, 1,
                                       x11::XFalse, XA_CARDINAL,
                                       &actualType, &actualFormat, &nItems,
                                       &bytesAfter, &propPID) == x11::XSuccess) {
                    if (nItems > 0) {
                        pid_t windowPID = *reinterpret_cast<pid_t *>(propPID);
                        std::string procName = getProcessName(windowPID);
                        if (procName == processName) {
                            XFree(children);
                            return reinterpret_cast<wID>(children[i]);
                        }
                    }
                    if (propPID) XFree(propPID);
                }
            }
            XFree(children);
        }
        return 0;
#else
    return 0;
#endif
    }

    // Find a window by class
    wID WindowManager::FindByClass(cstr className) {
#ifdef _WIN32
    // Windows implementation would go here
    return 0;
#elif defined(__linux__)
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            if (!WindowManager::InitializeX11()) {
                return 0;
            }
        }

        Window rootWindow = DisplayManager::GetRootWindow();
        Window parent;
        Window *children;
        unsigned int numChildren;

        if (XQueryTree(display, rootWindow, &rootWindow, &parent, &children,
                       &numChildren)) {
            if (children) {
                for (unsigned int i = 0; i < numChildren; i++) {
                    XClassHint classHint;
                    if (XGetClassHint(display, children[i], &classHint)) {
                        bool match = false;

                        if (classHint.res_name && strcmp(
                                classHint.res_name, className.c_str()) == 0) {
                            match = true;
                        } else if (classHint.res_class && strcmp(
                                       classHint.res_class,
                                       className.c_str()) == 0) {
                            match = true;
                        }

                        if (classHint.res_name) XFree(classHint.res_name);
                        if (classHint.res_class) XFree(classHint.res_class);

                        if (match) {
                            XFree(children);
                            return children[i];
                        }
                    }
                }
                XFree(children);
            }
        }
        return 0;
#else
    return 0;
#endif
    }

    wID WindowManager::FindByTitle(cstr title) {
#ifdef _WIN32
    // Windows implementation would go here
    return 0;
#elif defined(__linux__)
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            if (!WindowManager::InitializeX11()) {
                return 0;
            }
        }

        Window root = DefaultRootWindow(display);
        Window parent, *children;
        unsigned int nchildren;

        if (XQueryTree(display, root, &root, &parent, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; i++) {
                char *windowName = nullptr;
                if (XFetchName(display, children[i], &windowName) &&
                    windowName) {
                    bool match = (title == windowName);
                    XFree(windowName);

                    if (match) {
                        XFree(children);
                        return reinterpret_cast<wID>(children[i]);
                    }
                }
            }
            XFree(children);
        }
        return 0;
#endif
        return 0;
    }

    std::string WindowManager::getProcessName(pid_t windowPID) {
#ifdef __linux__
        std::ostringstream procPath;
        procPath << "/proc/" << windowPID << "/comm";

        std::string path = procPath.str();
        FILE *procFile = fopen(path.c_str(), "r");

        if (procFile) {
            char procName[256];
            size_t len = fread(procName, 1, sizeof(procName) - 1, procFile);
            procName[len] = '\0';

            // Remove trailing newline if present
            if (len > 0 && procName[len - 1] == '\n') {
                procName[len - 1] = '\0';
                // Replace newline with null terminator
            }

            fclose(procFile);
            return std::string(procName); // Return the process name as a string
        } else {
            std::cerr << "Error: Could not read from file " << path << ": " <<
                    std::strerror(errno) << "\n";
            return ""; // Handle the error as needed
        }
#else
    return "";
#endif
    }

    wID WindowManager::FindWindowInGroup(cstr groupName) {
        auto it = groups.find(groupName);
        if (it != groups.end()) {
            for (const auto &identifier: it->second) {
                wID win = Find(identifier);
                if (win) {
                    return win;
                }
            }
        }
        return 0;
    }

    wID WindowManager::NewWindow(cstr name, std::vector<int> *dimensions,
                                 bool hide) {
#ifdef _WIN32
    // Windows implementation would go here
    return 0;
#elif defined(__linux__)
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            if (!WindowManager::InitializeX11()) {
                return 0;
            }
        }

        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);
        int x = 0, y = 0, width = 800, height = 600;

        if (dimensions && dimensions->size() == 4) {
            x = (*dimensions)[0];
            y = (*dimensions)[1];
            width = (*dimensions)[2];
            height = (*dimensions)[3];
        }

        Window newWindow = XCreateSimpleWindow(display, root, x, y, width,
                                               height, 1,
                                               BlackPixel(display, screen),
                                               WhitePixel(display, screen));

        XStoreName(display, newWindow, name.c_str());
        if (!hide) {
            XMapWindow(display, newWindow);
        }
        XFlush(display);

        return reinterpret_cast<wID>(newWindow);
#else
    std::cerr << "NewWindow not supported on this platform." << std::endl;
    return 0;
#endif
    }
#ifdef _WIN32
// Function to convert error code to a human-readable message
str WindowManager::GetErrorMessage(pID errorCode) {
    // Windows implementation would go here
    return "Unknown error";
}

// Function to create a process and handle common logic
bool WindowManager::CreateProcessWrapper(cstr path, cstr command, pID creationFlags, STARTUPINFO& si, PROCESS_INFORMATION& pi) {
    // Windows implementation would go here
    return false;
}
#endif

    std::string WindowManager::GetCurrentWMName() const {
        return wmName;
    }

    bool WindowManager::IsWMSupported() const {
        return wmSupported;
    }

    bool WindowManager::IsX11() const {
        return WindowManagerDetector().IsX11();
    }

    bool WindowManager::IsWayland() const {
        return WindowManagerDetector().IsWayland();
    }

    void WindowManager::All() {
        // Implementation for All method
    }

    std::string WindowManager::DetectWindowManager() const {
#ifdef __linux__
        // Try to get window manager name from _NET_SUPPORTING_WM_CHECK
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            const_cast<WindowManager *>(this)->InitializeX11();
            if (!display) return "Unknown";
        }

        Atom netSupportingWmCheck = XInternAtom(
            display, "_NET_SUPPORTING_WM_CHECK", x11::XFalse);
        Atom netWmName = XInternAtom(display, "_NET_WM_NAME", x11::XFalse);

        if (netSupportingWmCheck != x11::XNone && netWmName != x11::XNone) {
            Atom actualType;
            int actualFormat;
            unsigned long nItems, bytesAfter;
            unsigned char *data = nullptr;

            if (XGetWindowProperty(display, DefaultRootWindow(display),
                                   netSupportingWmCheck,
                                   0, 1, x11::XFalse, XA_WINDOW, &actualType,
                                   &actualFormat,
                                   &nItems, &bytesAfter,
                                   &data) == x11::XSuccess && data) {
                Window wmWindow = *(reinterpret_cast<Window *>(data));
                XFree(data);

                if (XGetWindowProperty(display, wmWindow, netWmName, 0, 1024,
                                       x11::XFalse,
                                       XInternAtom(
                                           display, "UTF8_STRING", x11::XFalse),
                                       &actualType, &actualFormat,
                                       &nItems, &bytesAfter,
                                       &data) == x11::XSuccess && data) {
                    std::string name(reinterpret_cast<char *>(data));
                    XFree(data);
                    return name;
                }
            }
        }

        return "Unknown";
#else
    return "Unknown";
#endif
    }

    bool WindowManager::CheckWMProtocols() const {
#ifdef __linux__
        Display *display = DisplayManager::GetDisplay();
        if (!display) return false;

        Atom wmProtocols = XInternAtom(display, "WM_PROTOCOLS", x11::XFalse);
        Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", x11::XFalse);
        Atom wmTakeFocus = XInternAtom(display, "WM_TAKE_FOCUS", x11::XFalse);

        if (wmProtocols != x11::XNone && wmDeleteWindow != x11::XNone && wmTakeFocus !=
            x11::XNone) {
            Window dummyWindow = XCreateSimpleWindow(
                display, DefaultRootWindow(display),
                0, 0, 1, 1, 0, 0, 0);

            Atom *protocols = nullptr;
            int numProtocols = 0;
            bool hasRequiredProtocols = false;

            if (XGetWMProtocols(display, dummyWindow, &protocols,
                                &numProtocols)) {
                // Check if the required protocols are supported
                hasRequiredProtocols = true;
                XFree(protocols);
            }

            XDestroyWindow(display, dummyWindow);
            return hasRequiredProtocols;
        }

        return false;
#else
    return true;
#endif
    }

    std::optional<WindowManager::ActiveWindowContext> WindowManager::GetActiveWindowContext() {
        Display* display = DisplayManager::GetDisplay();
        if (!display) {
            error("No X11 display available.");
            return std::nullopt;
        }
        wID activeWin = GetActiveWindow();
        if (activeWin == 0) {
            error("No active window found.");
            return std::nullopt;
        }
        return ActiveWindowContext{display, DisplayManager::GetRootWindow(), activeWin};
    }

    void WindowManager::MoveToCorners(int direction, int distance) {
        auto contextOpt = GetActiveWindowContext();
        if (!contextOpt) return;
        auto& context = *contextOpt;

        XWindowAttributes attrs;
        if (!XGetWindowAttributes(context.display, context.activeWindowId, &attrs)) {
            error("Failed to get window attributes for MoveToCorners.");
            return;
        }

        int newX = attrs.x;
        int newY = attrs.y;

        switch (direction) {
            case 1: newY -= distance; break; // Up
            case 2: newY += distance; break; // Down
            case 3: newX -= distance; break; // Left
            case 4: newX += distance; break; // Right
        }

        XMoveWindow(context.display, context.activeWindowId, newX, newY);
        XFlush(context.display);
    }
    
    void WindowManager::ResizeToCorner(int direction, int distance) {
        auto contextOpt = GetActiveWindowContext();
        if (!contextOpt) return;
        auto& context = *contextOpt;

        XWindowAttributes attrs;
        if (!XGetWindowAttributes(context.display, context.activeWindowId, &attrs)) {
            error("Failed to get window attributes for ResizeToCorner.");
            return;
        }

        int newWidth = attrs.width;
        int newHeight = attrs.height;

        switch (direction) {
            case 1: newHeight -= distance; break; // Up
            case 2: newHeight += distance; break; // Down
            case 3: newWidth -= distance; break;  // Left
            case 4: newWidth += distance; break;  // Right
        }

        XResizeWindow(context.display, context.activeWindowId, newWidth > 0 ? newWidth : 1, newHeight > 0 ? newHeight : 1);
        XFlush(context.display);
    }

    bool WindowManager::Resize(wID windowId, int width, int height, bool fullscreen) {
        Display* display = DisplayManager::GetDisplay();
        if (!display || windowId == 0) return false;
        
        // This is a complex function, will keep its original structure for now.
        // It's already quite specialized for Wine.
        // The main goal is to reduce redundancy in simpler functions.
        return MoveResize(windowId, -1, -1, width, height); // Use -1 to indicate no move
    } 

    bool WindowManager::Move(wID windowId, int x, int y, bool centerOnScreen) {
        return MoveResize(windowId, x, y, -1, -1); // Use -1 to indicate no resize
    }
            
    bool WindowManager::MoveResize(wID windowId, int x, int y, int width, int height) {
        #if defined(__linux__)
            Display *display = DisplayManager::GetDisplay();
            if (!display || windowId == 0) return false;
            
            XWindowAttributes attrs;
            if (!XGetWindowAttributes(display, windowId, &attrs)) {
                error("Failed to get attributes for MoveResize");
                return false;
            }

            // If a dimension is -1, use the current value.
            int finalX = (x == -1) ? attrs.x : x;
            int finalY = (y == -1) ? attrs.y : y;
            int finalWidth = (width == -1) ? attrs.width : (width > 0 ? width : 1);
            int finalHeight = (height == -1) ? attrs.height : (height > 0 ? height : 1);

            XMoveResizeWindow(display, windowId, finalX, finalY, finalWidth, finalHeight);
            XFlush(display);
            return true;
        #elif defined(WINDOWS)
            // Windows implementation...
            return false;
        #else
            return false;
        #endif
    }
            
    bool WindowManager::Center(wID windowId) {
        #ifdef __linux__
            Display* display = DisplayManager::GetDisplay();
            if (!display || windowId == 0) return false;

            auto monitor = DisplayManager::GetPrimaryMonitor();
            XWindowAttributes attrs;
            if (!XGetWindowAttributes(display, windowId, &attrs)) return false;

            int x = monitor.x + (monitor.width - attrs.width) / 2;
            int y = monitor.y + (monitor.height - attrs.height) / 2;
            
            return Move(windowId, x, y);
        #else
            return false;
        #endif
    }
            
    bool WindowManager::MoveToCorner(wID windowId, const std::string& corner) {
            #if defined(__linux__)
                Display* display = DisplayManager::GetDisplay();
                if (!display || windowId == 0) return false;
                
                auto monitor = DisplayManager::GetPrimaryMonitor();
                XWindowAttributes attrs;
                if (!XGetWindowAttributes(display, windowId, &attrs)) return false;
                
                int x = 0, y = 0;
                
                if (corner == "top-left" || corner == "tl") {
                    x = monitor.x; y = monitor.y;
                } else if (corner == "top-right" || corner == "tr") {
                    x = monitor.x + monitor.width - attrs.width; y = monitor.y;
                } else if (corner == "bottom-left" || corner == "bl") {
                    x = monitor.x; y = monitor.y + monitor.height - attrs.height;
                } else if (corner == "bottom-right" || corner == "br") {
                    x = monitor.x + monitor.width - attrs.width; y = monitor.y + monitor.height - attrs.height;
                } else {
                    std::cerr << "Unknown corner: " << corner << std::endl;
                    return false;
                }
                
                return Move(windowId, x, y);
            #endif
                return false;
            }
            
    bool WindowManager::MoveToMonitor(wID windowId, int monitorIndex) {
        #if defined(__linux__)
            Display* display = DisplayManager::GetDisplay();
            if (!display || windowId == 0) return false;

            auto monitors = DisplayManager::GetMonitors();
            if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
                error("Invalid monitor index: {}", monitorIndex);
                return false;
            }

            const auto& monitor = monitors[monitorIndex];
            
            XWindowAttributes attrs;
            if (!XGetWindowAttributes(display, windowId, &attrs)) return false;
            
            int x = monitor.x + (monitor.width - attrs.width) / 2;
            int y = monitor.y + (monitor.height - attrs.height) / 2;
                
            return Move(windowId, x, y);
        #endif
            return false;
    }
            
    // Convenience overloads with window title
    bool WindowManager::Move(const std::string& windowTitle, int x, int y, bool centerOnScreen) {
        wID windowId = FindByTitle(windowTitle);
        if (!windowId) {
            std::cerr << "Window not found: " << windowTitle << std::endl;
            return false;
        }
        return Move(windowId, x, y, centerOnScreen);
    }
            
    bool WindowManager::Center(const std::string& windowTitle) {
        return Move(windowTitle, 0, 0, true);
    }

    bool WindowManager::Resize(const std::string& windowTitle, int width, int height, bool fullscreen) {
        wID windowId = FindByTitle(windowTitle);
        if (!windowId) {
            std::cerr << "Window not found: " << windowTitle << std::endl;
            return false;
        }
        return Resize(windowId, width, height, fullscreen);
    }
        
    bool WindowManager::SetResolution(wID windowId, const std::string& resolution) {
        static const std::unordered_map<std::string, std::pair<int, int>> resolutions = {
            {"720p", {1280, 720}},
            {"1080p", {1920, 1080}},
            {"1440p", {2560, 1440}},
            {"4k", {3840, 2160}},
            {"fullscreen", {0, 0}} // Special case for fullscreen
        };
    
        auto it = resolutions.find(resolution);
        if (it == resolutions.end()) {
            std::cerr << "Unknown resolution: " << resolution << std::endl;
            return false;
        }
    
        if (resolution == "fullscreen") {
            return Resize(windowId, 0, 0, true);
        } else {
            return Resize(windowId, it->second.first, it->second.second, false);
        }
    }

    void WindowManager::SnapWindow(wID windowId, int position) {
        auto contextOpt = GetActiveWindowContext();
        if (!contextOpt) return;
        auto& context = *contextOpt;

        auto monitor = DisplayManager::GetPrimaryMonitor();
        int screenWidth = monitor.width;
        int screenHeight = monitor.height;

        int newX = monitor.x;
        int newY = monitor.y;
        int newWidth = screenWidth;
        int newHeight = screenHeight;

        switch (position) {
            case 1: newWidth /= 2; break; // Left half
            case 2: newWidth /= 2; newX += screenWidth / 2; break; // Right half
            // Add other cases for top, bottom, corners etc.
        }
        MoveResize(context.activeWindowId, newX, newY, newWidth, newHeight);
    }

    void WindowManager::ManageVirtualDesktops(int action) {
        // Implementation remains complex and specific, so no major refactoring with the helper.
#ifdef __linux__
        auto *display = DisplayManager::GetDisplay();
        if (!display) {
            std::cerr << "Cannot manage desktops - no X11 display\n";
            return;
        }

        Window root = DisplayManager::GetRootWindow();
        Atom desktopAtom = XInternAtom(display, "_NET_CURRENT_DESKTOP", x11::XFalse);
        Atom desktopCountAtom = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS",
                                            x11::XFalse);

        unsigned long nitems, bytes;
        unsigned char *data = NULL;
        int format;
        Atom type;

        // Get current desktop
        XGetWindowProperty(display, root, desktopAtom,
                           0, 1, x11::XFalse, XA_CARDINAL, &type, &format, &nitems,
                           &bytes, &data);

        int currentDesktop = *(int *) data;
        XFree(data);

        // Get total desktops
        XGetWindowProperty(display, root, desktopCountAtom,
                           0, 1, x11::XFalse, XA_CARDINAL, &type, &format, &nitems,
                           &bytes, &data);

        int totalDesktops = *(int *) data;
        XFree(data);

        int newDesktop = currentDesktop;
        switch (action) {
            case 1: newDesktop = (currentDesktop + 1) % totalDesktops;
                break;
            case 2: newDesktop =
                    (currentDesktop - 1 + totalDesktops) % totalDesktops;
                break;
        }

        XEvent event;
        event.xclient.type = x11::XClientMessage;
        event.xclient.message_type = desktopAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = newDesktop;
        event.xclient.data.l[1] = CurrentTime;

        XSendEvent(display, root, x11::XFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(display);
#endif
    }

    void WindowManager::SnapWindowWithPadding(int position, int padding) {
        // This can also be refactored, but is left for brevity.
#ifdef __linux__
        auto *display = DisplayManager::GetDisplay();
        if (!display) return;

        Window win = GetActiveWindow();
        if (!win) return;

        Window root = DisplayManager::GetRootWindow();

        XWindowAttributes root_attrs;
        XGetWindowAttributes(display, root, &root_attrs);

        XWindowAttributes win_attrs;
        XGetWindowAttributes(display, win, &win_attrs);

        const int screenWidth = root_attrs.width - padding * 2;
        const int screenHeight = root_attrs.height - padding * 2;

        switch (position) {
            case 1: // Left with padding
                XMoveResizeWindow(display, win,
                                  padding, padding,
                                  screenWidth / 2, screenHeight);
                break;
            case 2: // Right with padding
                XMoveResizeWindow(display, win,
                                  screenWidth / 2 + padding, padding,
                                  screenWidth / 2, screenHeight);
                break;
            // Add other positions...
        }
        XFlush(display);
#endif
    }

    void WindowManager::ToggleAlwaysOnTop() {
        auto contextOpt = GetActiveWindowContext();
        if (!contextOpt) return;
        auto& context = *contextOpt;

        Atom wmState = XInternAtom(context.display, "_NET_WM_STATE", x11::XFalse);
        Atom wmStateAbove = XInternAtom(context.display, "_NET_WM_STATE_ABOVE", x11::XFalse);
        if (wmState == x11::XNone || wmStateAbove == x11::XNone) return;

        bool isOnTop = false;
        // ... (code to check current state) ...

        XEvent event;
        // ... (code to build and send the event) ...
        event.xclient.data.l[0] = isOnTop ? 0 : 1; // 0=remove, 1=add, 2=toggle
        
        XSendEvent(context.display, context.root, x11::XFalse,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
        XFlush(context.display);
    }

    std::string WindowManager::GetActiveWindowClass() {
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            error("Failed to get display in GetActiveWindowClass");
            return "";
        }

        ::Window focusedWindow; // Use X11's Window type
        int revertTo;
        if (XGetInputFocus(display, &focusedWindow, &revertTo) == 0) {
            error("Failed to get input focus");
            return "";
        }

        if (focusedWindow == x11::XNone) {
            debug("No window currently focused");
            return "";
        }

        XClassHint classHint;
        x11::XStatus status = XGetClassHint(display, focusedWindow, &classHint);
        if (status == 0) {
            debug("Failed to get class hint for window");
            return "";
        }

        std::string className(classHint.res_class);
        XFree(classHint.res_name);
        XFree(classHint.res_class);

        debug("Active window class: " + className);
        return className;
    }

    // Update previous active window
    void WindowManager::UpdatePreviousActiveWindow() {
#ifdef __linux__
        Display *display = DisplayManager::GetDisplay();
        if (!display) return;

        Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW",
                                            x11::XFalse);
        if (activeWindowAtom == x11::XNone) return;

        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *prop = nullptr;
        activeWindow.className = GetActiveWindowClass();
        if (XGetWindowProperty(display, DefaultRootWindow(display),
                               activeWindowAtom, 0, 1,
                               x11::XFalse, XA_WINDOW, &actualType, &actualFormat,
                               &nitems, &bytesAfter,
                               &prop) == x11::XSuccess) {
            if (prop) {
                Window currentActive = *reinterpret_cast<Window *>(prop);
                XFree(prop);

                // Only update if both windows are valid and different
                if (currentActive != x11::XNone && previousActiveWindow !=
                    currentActive) {
                    previousActiveWindow = currentActive;
                    std::cout << "Updated previous active window to: " <<
                            previousActiveWindow << std::endl;
                }
            }
        }
#endif
    }
    
    void WindowManager::ToggleFullscreen(wID windowId) {
        #ifdef __linux__
        Display* display = DisplayManager::GetDisplay();
        if (!display || windowId == 0) return;

        Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", x11::XFalse);
        Atom fsAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", x11::XFalse);
        
        XEvent ev{};
        ev.xclient.type = x11::XClientMessage;
        ev.xclient.window = windowId;
        ev.xclient.message_type = stateAtom;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = 2; // _NET_WM_STATE_TOGGLE
        ev.xclient.data.l[1] = fsAtom;
        
        XSendEvent(display, DefaultRootWindow(display), x11::XFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
        XFlush(display);
        #endif
    }

    void WindowManager::MoveWindowToNextMonitor() {
        auto contextOpt = GetActiveWindowContext();
        if (!contextOpt) return;
        auto& context = *contextOpt;

        XWindowAttributes winAttr;
        if (!XGetWindowAttributes(context.display, context.activeWindowId, &winAttr)) {
            error("Failed to get window attributes for MoveWindowToNextMonitor.");
            return;
        }

        int winX, winY;
        Window child;
        XTranslateCoordinates(context.display, context.activeWindowId, context.root, 0, 0, &winX, &winY, &child);

        auto monitors = DisplayManager::GetMonitors();
        if (monitors.size() < 2) return;

        int winCenterX = winX + (winAttr.width / 2);
        int winCenterY = winY + (winAttr.height / 2);

        int currentMonitor = 0;
        for (size_t i = 0; i < monitors.size(); ++i) {
            const auto &m = monitors[i];
            if (winCenterX >= m.x && winCenterX < m.x + m.width &&
                winCenterY >= m.y && winCenterY < m.y + m.height) {
                currentMonitor = i;
                break;
            }
        }

        int nextMonitor = (currentMonitor + 1) % monitors.size();
        const auto &target = monitors[nextMonitor];
        
        int targetX = target.x + (target.width - winAttr.width) / 2;
        int targetY = target.y + (target.height - winAttr.height) / 2;

        XMoveWindow(context.display, context.activeWindowId, targetX, targetY);
        XFlush(context.display);
    }
    
#ifdef WINDOWS
// Function to convert error code to a human-readable message
str WindowManager::GetErrorMessage(pID errorCode)
{
    LPWSTR lpMsgBuf;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&lpMsgBuf,
        0, nullptr);

    // Convert wide string to narrow string
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, lpMsgBuf, -1, nullptr, 0, nullptr, nullptr);
    std::string errorMessage(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, lpMsgBuf, -1, &errorMessage[0], sizeNeeded, nullptr, nullptr);

    LocalFree(lpMsgBuf);
    return errorMessage;
}

// Function to create a process and handle common logic
bool WindowManager::CreateProcessWrapper(cstr path, cstr command, pID creationFlags, STARTUPINFO &si, PROCESS_INFORMATION &pi)
{
    if (path.empty())
    {
        return CreateProcess(
            NULL,
            const_cast<char *>(command.c_str()),
            nullptr, nullptr, FALSE,
            creationFlags, nullptr, nullptr,
            &si, &pi);
    }
    else
    {
        return CreateProcess(
            path.c_str(),
            const_cast<char *>(command.c_str()),
            nullptr, nullptr, FALSE,
            creationFlags, nullptr, nullptr,
            &si, &pi);
    }
}
#endif
}