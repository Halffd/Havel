#include "WindowManager.hpp"
#include "types.hpp"
#include "core/DisplayManager.hpp"
#include "../utils/Logger.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>

#ifdef __linux__
#include "x11.h"
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
    XWindow havel::WindowManager::previousActiveWindow = None;
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
                                            False);
        if (activeWindowAtom == None) return 0;

        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *prop = nullptr;
        Window activeWindow = 0;

        if (XGetWindowProperty(display, DefaultRootWindow(display),
                               activeWindowAtom, 0, 1,
                               False, XA_WINDOW, &actualType, &actualFormat,
                               &nitems, &bytesAfter,
                               &prop) == Success) {
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
        if (previousActiveWindow != None && previousActiveWindow !=
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
                previousActiveWindow = None; // Reset invalid window
            }
        }

        // If we don't have a valid previous window, find another suitable window
        Window windowToActivate = None;

        if (!previousWindowValid) {
            info("Alt+Tab: Looking for an alternative window");

            // Get the list of windows
            Atom clientListAtom = XInternAtom(display,
                                              "_NET_CLIENT_LIST_STACKING",
                                              False);
            if (clientListAtom == None) {
                clientListAtom =
                        XInternAtom(display, "_NET_CLIENT_LIST", False);
                if (clientListAtom == None) {
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
                                   0, ~0L, False, XA_WINDOW,
                                   &actualType, &actualFormat,
                                   &numWindows, &bytesAfter,
                                   &data) != Success ||
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
                    None) {
                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(display, windows[idx], &attrs) &&
                        attrs.map_state == IsViewable) {
                        // Check if this is a normal window (not a desktop, dock, etc.)
                        Atom windowTypeAtom = XInternAtom(
                            display, "_NET_WM_WINDOW_TYPE", False);
                        Atom actualType;
                        int actualFormat;
                        unsigned long numItems, bytesAfter;
                        unsigned char *typeData = nullptr;
                        bool isNormalWindow = true;

                        if (XGetWindowProperty(display, windows[idx],
                                               windowTypeAtom, 0, ~0L,
                                               False, AnyPropertyType,
                                               &actualType, &actualFormat,
                                               &numItems, &bytesAfter,
                                               &typeData) == Success &&
                            typeData) {
                            Atom *types = reinterpret_cast<Atom *>(typeData);
                            Atom normalAtom = XInternAtom(
                                display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
                            Atom dialogAtom = XInternAtom(
                                display, "_NET_WM_WINDOW_TYPE_DIALOG", False);

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
        if (currentActiveWindow != None) {
            previousActiveWindow = currentActiveWindow;
            debug(
                "Alt+Tab: Stored current window as previous: " + std::to_string(
                    previousActiveWindow));
        }

        // Activate the selected window
        if (windowToActivate != None) {
            Atom activeWindowAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW",
                                                False);
            if (activeWindowAtom != None) {
                XEvent event = {};
                event.type = ClientMessage;
                event.xclient.window = windowToActivate;
                event.xclient.message_type = activeWindowAtom;
                event.xclient.format = 32;
                event.xclient.data.l[0] = 2; // Source: pager
                event.xclient.data.l[1] = CurrentTime;

                XSendEvent(display, root, False,
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

        XSync(display, False);
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
                                   True);
        if (pidAtom == None) {
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
                                       False, XA_CARDINAL,
                                       &actualType, &actualFormat, &nItems,
                                       &bytesAfter, &propPID) == Success) {
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
                                   True);
        if (pidAtom == None) {
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
                                       False, XA_CARDINAL,
                                       &actualType, &actualFormat, &nItems,
                                       &bytesAfter, &propPID) == Success) {
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
            display, "_NET_SUPPORTING_WM_CHECK", False);
        Atom netWmName = XInternAtom(display, "_NET_WM_NAME", False);

        if (netSupportingWmCheck != None && netWmName != None) {
            Atom actualType;
            int actualFormat;
            unsigned long nItems, bytesAfter;
            unsigned char *data = nullptr;

            if (XGetWindowProperty(display, DefaultRootWindow(display),
                                   netSupportingWmCheck,
                                   0, 1, False, XA_WINDOW, &actualType,
                                   &actualFormat,
                                   &nItems, &bytesAfter,
                                   &data) == Success && data) {
                Window wmWindow = *(reinterpret_cast<Window *>(data));
                XFree(data);

                if (XGetWindowProperty(display, wmWindow, netWmName, 0, 1024,
                                       False,
                                       XInternAtom(
                                           display, "UTF8_STRING", False),
                                       &actualType, &actualFormat,
                                       &nItems, &bytesAfter,
                                       &data) == Success && data) {
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

        Atom wmProtocols = XInternAtom(display, "WM_PROTOCOLS", False);
        Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
        Atom wmTakeFocus = XInternAtom(display, "WM_TAKE_FOCUS", False);

        if (wmProtocols != None && wmDeleteWindow != None && wmTakeFocus !=
            None) {
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

    // Implementation of AHK-like features
    void WindowManager::MoveWindow(int direction, int distance) {
#ifdef __linux__
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            error("No X11 display available");
            return;
        }

        ::Window win = GetActiveWindow(); // Use X11's Window type
        if (win == 0) {
            error("No active window to move");
            return;
        }

        std::string windowClass = GetActiveWindowClass();
        debug(
            "Moving window of class '" + windowClass + "' in direction " +
            std::to_string(direction));

        XWindowAttributes attrs;
        if (!XGetWindowAttributes(display, win, &attrs)) {
            error("Failed to get window attributes");
            return;
        }

        int newX = attrs.x;
        int newY = attrs.y;

        switch (direction) {
            case 1: // Up
                newY -= distance;
                break;
            case 2: // Down
                newY += distance;
                break;
            case 3: // Left
                newX -= distance;
                break;
            case 4: // Right
                newX += distance;
                break;
        }

        XMoveWindow(display, win, newX, newY);
        XFlush(display);
        debug(
            "Window moved to position: x=" + std::to_string(newX) + ", y=" +
            std::to_string(newY));
#endif
    }

    void WindowManager::ResizeWindow(int direction, int distance) {
#ifdef __linux__
        Display *display = DisplayManager::GetDisplay();
        Window win = GetActiveWindow();

        XWindowAttributes attrs;
        XGetWindowAttributes(display, win, &attrs);

        int newWidth = attrs.width;
        int newHeight = attrs.height;

        switch (direction) {
            case 1: newHeight -= distance;
                break; // Up
            case 2: newHeight += distance;
                break; // Down
            case 3: newWidth -= distance;
                break; // Left
            case 4: newWidth += distance;
                break; // Right
        }

        XResizeWindow(display, win, newWidth, newHeight);
        XFlush(display);
#elif _WIN32
    HWND hwnd = GetForegroundWindow();
    RECT rect;
    GetWindowRect(hwnd, &rect);

    switch(direction) {
        case 1: rect.bottom -= distance; break;
        case 2: rect.bottom += distance; break;
        case 3: rect.right -= distance; break;
        case 4: rect.right += distance; break;
    }

    MoveWindow(hwnd, rect.left, rect.top,
              rect.right - rect.left, rect.bottom - rect.top, TRUE);
#endif
    }

    void WindowManager::SnapWindow(int position) {
        // 1=Left, 2=Right, 3=Top, 4=Bottom, 5=TopLeft, 6=TopRight, 7=BottomLeft, 8=BottomRight
#ifdef __linux__
        auto *display = DisplayManager::GetDisplay();
        if (!display) return;

        Window root = DisplayManager::GetRootWindow();
        Window win = GetActiveWindow();

        XWindowAttributes root_attrs;
        if (!XGetWindowAttributes(display, root, &root_attrs)) {
            std::cerr << "Failed to get root window attributes\n";
            return;
        }

        XWindowAttributes win_attrs;
        if (!XGetWindowAttributes(display, win, &win_attrs)) {
            std::cerr << "Failed to get window attributes\n";
            return;
        }

        int screenWidth = root_attrs.width;
        int screenHeight = root_attrs.height;

        int newX = win_attrs.x;
        int newY = win_attrs.y;
        int newWidth = win_attrs.width;
        int newHeight = win_attrs.height;

        switch (position) {
            case 1: // Left half
                newWidth = screenWidth / 2;
                newHeight = screenHeight;
                newX = 0;
                newY = 0;
                break;
            case 2: // Right half
                newWidth = screenWidth / 2;
                newHeight = screenHeight;
                newX = screenWidth / 2;
                newY = 0;
                break;
            // Add other positions...
        }

        XMoveResizeWindow(display, win, newX, newY, newWidth, newHeight);
        XFlush(display);
#endif
    }

    void WindowManager::ManageVirtualDesktops(int action) {
#ifdef __linux__
        auto *display = DisplayManager::GetDisplay();
        if (!display) {
            std::cerr << "Cannot manage desktops - no X11 display\n";
            return;
        }

        Window root = DisplayManager::GetRootWindow();
        Atom desktopAtom = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
        Atom desktopCountAtom = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS",
                                            False);

        unsigned long nitems, bytes;
        unsigned char *data = NULL;
        int format;
        Atom type;

        // Get current desktop
        XGetWindowProperty(display, root, desktopAtom,
                           0, 1, False, XA_CARDINAL, &type, &format, &nitems,
                           &bytes, &data);

        int currentDesktop = *(int *) data;
        XFree(data);

        // Get total desktops
        XGetWindowProperty(display, root, desktopCountAtom,
                           0, 1, False, XA_CARDINAL, &type, &format, &nitems,
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
        event.xclient.type = ClientMessage;
        event.xclient.message_type = desktopAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = newDesktop;
        event.xclient.data.l[1] = CurrentTime;

        XSendEvent(display, root, False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(display);
#endif
    }

    // Add similar implementations for other AHK functions...

    void WindowManager::SnapWindowWithPadding(int position, int padding) {
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

    // Toggle always on top for the active window
    void WindowManager::ToggleAlwaysOnTop() {
        // Get the active window
        wID activeWindow = GetActiveWindow();
        if (!activeWindow) {
            std::cerr << "No active window to toggle always-on-top state" <<
                    std::endl;
            return;
        }

#ifdef __linux__
        // Check if the window is already on top using X11
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            std::cerr << "X11 display not available" << std::endl;
            return;
        }

        // Get the current state
        Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
        Atom wmStateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);

        if (wmState == None || wmStateAbove == None) {
            std::cerr << "Required X11 atoms not available" << std::endl;
            return;
        }

        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *propData = NULL;
        bool isOnTop = false;

        if (XGetWindowProperty(display, activeWindow, wmState, 0, 64, False,
                               XA_ATOM,
                               &actualType, &actualFormat, &nitems, &bytesAfter,
                               &propData) == Success) {
            if (propData) {
                Atom *atoms = (Atom *) propData;
                for (unsigned long i = 0; i < nitems; i++) {
                    if (atoms[i] == wmStateAbove) {
                        isOnTop = true;
                        break;
                    }
                }
                XFree(propData);
            }
        }

        // Toggle the state
        Window root = DefaultRootWindow(display);
        XEvent event;
        memset(&event, 0, sizeof(event));

        event.type = ClientMessage;
        event.xclient.window = activeWindow;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = isOnTop ? 0 : 1; // 0 = remove, 1 = add
        event.xclient.data.l[1] = wmStateAbove;
        event.xclient.data.l[2] = 0;
        event.xclient.data.l[3] = 1; // source is application

        XSendEvent(display, root, False,
                   SubstructureNotifyMask | SubstructureRedirectMask, &event);
        XFlush(display);

        std::cout << "Toggled always-on-top state for window " << activeWindow
                << std::endl;
#endif
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

        if (focusedWindow == None) {
            debug("No window currently focused");
            return "";
        }

        XClassHint classHint;
        Status status = XGetClassHint(display, focusedWindow, &classHint);
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
                                            False);
        if (activeWindowAtom == None) return;

        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char *prop = nullptr;
        activeWindow.className = GetActiveWindowClass();
        if (XGetWindowProperty(display, DefaultRootWindow(display),
                               activeWindowAtom, 0, 1,
                               False, XA_WINDOW, &actualType, &actualFormat,
                               &nitems, &bytesAfter,
                               &prop) == Success) {
            if (prop) {
                Window currentActive = *reinterpret_cast<Window *>(prop);
                XFree(prop);

                // Only update if both windows are valid and different
                if (currentActive != None && previousActiveWindow !=
                    currentActive) {
                    previousActiveWindow = currentActive;
                    std::cout << "Updated previous active window to: " <<
                            previousActiveWindow << std::endl;
                }
            }
        }
#endif
    }

    void WindowManager::MoveWindowToNextMonitor() {
        Display *display = DisplayManager::GetDisplay();
        if (!display) {
            std::cerr << "No display found.\n";
            return;
        }

        Window root = DefaultRootWindow(display);

        // Get active window
        Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
        if (activeAtom == None) {
            std::cerr << "No _NET_ACTIVE_WINDOW atom.\n";
            return;
        }

        Atom actualType;
        int actualFormat;
        unsigned long nItems, bytesAfter;
        unsigned char *prop = nullptr;

        if (XGetWindowProperty(display, root, activeAtom, 0, 1, False,
                               AnyPropertyType,
                               &actualType, &actualFormat, &nItems, &bytesAfter,
                               &prop) != Success || !prop) {
            std::cerr << "Failed to get active window.\n";
            return;
        }

        Window activeWin = *(Window *) prop;
        XFree(prop);

        if (!activeWin || activeWin == None) {
            std::cerr << "No active window.\n";
            return;
        }

        // Get window attributes - we need the size from here
        XWindowAttributes winAttr;
        if (!XGetWindowAttributes(display, activeWin, &winAttr)) {
            std::cerr << "Failed to get window attributes.\n";
            return;
        }

        int winW = winAttr.width;
        int winH = winAttr.height;

        // Get ACTUAL window position in global coordinates
        Window child;
        int winX, winY;

        // This translates local coordinates to root coordinates
        XTranslateCoordinates(display, activeWin, root, 0, 0, &winX, &winY,
                              &child);

        // Debug output
        std::cout << "Window position: " << winX << ", " << winY << "\n";
        std::cout << "Window dimensions: " << winW << "x" << winH << "\n";

        // Check if window is fullscreen
        bool isFullscreen = false;
        Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
        Atom fsAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
        Atom typeRet;
        int formatRet;
        unsigned long nItemsRet, bytesAfterRet;
        unsigned char *propRet = nullptr;

        if (XGetWindowProperty(display, activeWin, stateAtom, 0, (~0L), False,
                               AnyPropertyType,
                               &typeRet, &formatRet, &nItemsRet, &bytesAfterRet,
                               &propRet) == Success && propRet) {
            Atom *states = (Atom *) propRet;
            for (unsigned long i = 0; i < nItemsRet; ++i) {
                if (states[i] == fsAtom) {
                    isFullscreen = true;
                    break;
                }
            }
            XFree(propRet);
        }

        if (isFullscreen) {
            ToggleFullscreen(display, activeWin, stateAtom, fsAtom, false);
        }

        // Get monitor info
        XRRScreenResources *screenRes = XRRGetScreenResources(display, root);
        if (!screenRes) {
            std::cerr << "Failed to get screen resources.\n";
            return;
        }

        struct Monitor {
            unsigned int x;
            unsigned int y;
            unsigned int width;
            unsigned int height;
        };

        std::vector<Monitor> monitors;

        // Validate screen resources
        if (!screenRes || screenRes->ncrtc == 0) {
            std::cerr << "No CRTCs available or invalid screen resources\n";
            if (screenRes) XRRFreeScreenResources(screenRes);
            return;
        }

        // Enumerate all CRTCs
        for (int i = 0; i < screenRes->ncrtc; ++i) {
            XRRCrtcInfo *crtc = XRRGetCrtcInfo(display, screenRes,
                                               screenRes->crtcs[i]);
            if (!crtc) {
                std::cerr << "Failed to get CRTC info for CRTC " << i << "\n";
                continue;
            }

            // Only consider active monitors (with valid mode)
            if (crtc->mode != None) {
                monitors.emplace_back(Monitor{
                    static_cast<unsigned int>(crtc->x),
                    static_cast<unsigned int>(crtc->y),
                    static_cast<unsigned int>(crtc->width),
                    static_cast<unsigned int>(crtc->height)
                });

                std::cout << "Monitor " << i << ": "
                        << crtc->x << "," << crtc->y << " "
                        << crtc->width << "x" << crtc->height << "\n";
            }

            XRRFreeCrtcInfo(crtc);
        }

        XRRFreeScreenResources(screenRes);

        // Validate we have enough monitors
        if (monitors.size() < 2) {
            std::cerr << "Error: Need at least 2 active monitors (found "
                    << monitors.size() << ")\n";
            return;
        }
        // SIMPLIFIED MONITOR DETECTION FOR COMPIZ
        // Just check which monitor contains the center point of the window
        int winCenterX = winX + (winW / 2);
        int winCenterY = winY + (winH / 2);

        int currentMonitor = 0; // Default to first monitor
        for (size_t i = 0; i < monitors.size(); ++i) {
            const auto &m = monitors[i];
            if (winCenterX >= m.x && winCenterX < m.x + m.width &&
                winCenterY >= m.y && winCenterY < m.y + m.height) {
                currentMonitor = i;
                break;
            }
        }

        std::cout << "Window center: " << winCenterX << "," << winCenterY
                << " is on monitor " << currentMonitor << "\n";

        int nextMonitor = (currentMonitor + 1) % monitors.size();
        const auto &target = monitors[nextMonitor];

        // Center the window on target monitor
        int targetX = target.x + (target.width - winW) / 2;
        int targetY = target.y + (target.height - winH) / 2;

        std::cout << "Moving window to monitor " << nextMonitor
                << " at (" << targetX << "," << targetY << ") "
                << winW << "x" << winH << "\n";

        XMoveResizeWindow(display, activeWin, targetX, targetY, winW, winH);
        XRaiseWindow(display, activeWin);
        XSetInputFocus(display, activeWin, RevertToPointerRoot, CurrentTime);
        XFlush(display);

        if (isFullscreen) {
            ToggleFullscreen(display, activeWin, stateAtom, fsAtom, true);
        }

        std::cout << "Moved window from monitor " << currentMonitor << " to " <<
                nextMonitor << "\n";
    }

    void WindowManager::ToggleFullscreen(Display *display, Window win,
                                         Atom stateAtom, Atom fsAtom,
                                         bool enable) {
        XEvent ev = {0};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = win;
        ev.xclient.message_type = stateAtom;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = enable ? 1 : 0; // _NET_WM_STATE_ADD : REMOVE
        ev.xclient.data.l[1] = fsAtom;
        ev.xclient.data.l[2] = 0;
        ev.xclient.data.l[3] = 1;
        ev.xclient.data.l[4] = 0;
        XSendEvent(display, DefaultRootWindow(display), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
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