#include "Window.hpp"
#include "WindowManager.hpp"
#include "core/DisplayManager.hpp"
#include "types.hpp"
#include <X11/Xlib.h>
#include <iostream>
#include <sstream>
#include <memory>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>
#include <cerrno>
#include <cstring>

#ifdef __linux__
#include "x11.h"
// Initialize static members
std::shared_ptr<Display> havel::Window::display;
havel::DisplayServer havel::Window::displayServer = havel::DisplayServer::X11;

// Custom deleter for Display
struct DisplayDeleter {
    void operator()(Display* d) {
        if (d) {
            XCloseDisplay(d);
        }
    }
};
#endif

namespace havel {

// Constructor
Window::Window(cstr title, wID id) : m_title(title), m_id(id) {
    #ifdef __linux__
    if (!display) {
        Display* rawDisplay = XOpenDisplay(nullptr);
        if (!rawDisplay) {
            std::cerr << "Failed to open X11 display" << std::endl;
            return;
        }
        display = std::shared_ptr<Display>(rawDisplay, DisplayDeleter());
    }
    #endif
}

Window::Window(wID id) : m_id(id) {
    #ifdef __linux__
    if (!display) {
        Display* rawDisplay = XOpenDisplay(nullptr);
        if (!rawDisplay) {
            std::cerr << "Failed to open X11 display" << std::endl;
            return;
        }
        display = std::shared_ptr<Display>(rawDisplay, DisplayDeleter());
    }
    #endif
    m_id = id;
    m_title = Title(id); // Populate title
}

// Get the position of a window
Rect Window::Pos() const {
    return Window::Pos(m_id);
}

Rect Window::Pos(wID win) {
    if (!win) return {};

#if defined(WINDOWS)
    return GetPositionWindows(win);
#elif defined(__linux__)
    switch (displayServer) {
        case DisplayServer::X11:
            return GetPositionX11(win);
        case DisplayServer::Wayland:
            return GetPositionWayland(win);
        default:
            return {};
    }
#else
    return {};
#endif
}

#if defined(WINDOWS)
// Windows implementation of GetPosition
Rect Window::GetPositionWindows(wID win) {
    RECT rect;
    if (GetWindowRect(reinterpret_cast<HWND>(win), &rect)) {
        return havel::Rect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }
    return havel::Rect(0, 0, 0, 0);
}
#endif

// X11 implementation of GetPosition
Rect Window::GetPositionX11(wID win) {
    if (!display) return {};
    
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display.get(), win, &attrs)) {
        return {};
    }
    
    // Get ACTUAL screen coordinates
    int screenX, screenY;
    ::Window child;
    if (!XTranslateCoordinates(display.get(), win, DefaultRootWindow(display.get()), 
                              0, 0, &screenX, &screenY, &child)) {
        // Fallback to parent-relative if translation fails
        return {attrs.x, attrs.y, attrs.width, attrs.height};
    }
    
    return {screenX, screenY, attrs.width, attrs.height};
}

// Wayland implementation of GetPosition
Rect Window::GetPositionWayland(wID /* win */) {
    // Wayland implementation not yet available
    return {0, 0, 0, 0};
}

// Find window using method 2
wID Window::Find2(cstr identifier, cstr type) {
    wID win = 0;

    if (type == "title") {
        win = FindByTitle(identifier.c_str());
    } else if (type == "class") {
        // Use the FindByClass method
        win = havel::WindowManager::FindByClass(identifier);
    } else if (type == "pid") {
        pID pid = std::stoi(identifier);
        win = GetwIDByPID(pid);
    }
    if (win) {
        std::cout << "Found window ID: " << win << std::endl;
    } else {
        std::cerr << "Window not found!" << std::endl;
    }
    return win;
}

// Find a window by its identifier string
wID Window::Find(cstr identifier) {
    wID win = 0;
    
    // Check if it's a title
    if (identifier.find("title=") == 0) {
        std::string title = identifier.substr(6);
        win = FindByTitle(title);
    }
    // Check if it's a class
    else if (identifier.find("class=") == 0) {
        std::string className = identifier.substr(6);
        win = havel::WindowManager::FindByClass(className);
    }
    // Check if it's a PID
    else if (identifier.find("pid=") == 0) {
        try {
            pID pid = std::stoi(identifier.substr(4));
            win = GetwIDByPID(pid);
        } catch (const std::exception&) {
            std::cerr << "Invalid PID format" << std::endl;
        }
    }
    // Assume it's a title if no prefix is given
    else {
        win = FindByTitle(identifier);
    }
    
    return win;
}

// Find a window by its title
wID Window::FindByTitle(cstr title) {
    #ifdef __linux__
    if (!display) return 0;
    
    ::Window rootWindow = DefaultRootWindow(display.get());
    ::Window parent;
    ::Window* children;
    unsigned int numChildren;
    
    if (XQueryTree(display.get(), rootWindow, &rootWindow, &parent, &children, &numChildren)) {
        if (children) {
            for (unsigned int i = 0; i < numChildren; i++) {
                XTextProperty windowName;
                if (XGetWMName(display.get(), children[i], &windowName) && windowName.value) {
                    std::string windowTitle = reinterpret_cast<char*>(windowName.value);
                    XFree(windowName.value);
                    
                    if (windowTitle.find(title) != std::string::npos) {
                        ::Window result = children[i];
                        XFree(children);
                        return static_cast<wID>(result);
                    }
                }
                
                // Try NET_WM_NAME for modern window managers
                Atom nameAtom = XInternAtom(display.get(), "_NET_WM_NAME", x11::XFalse);
                Atom utf8Atom = XInternAtom(display.get(), "UTF8_STRING", x11::XFalse);
                
                if (nameAtom != x11::XNone && utf8Atom != x11::XNone) {
                    Atom actualType;
                    int actualFormat;
                    unsigned long nitems, bytesAfter;
                    unsigned char* prop = nullptr;
                    
                    if (XGetWindowProperty(display.get(), children[i], nameAtom, 0, 1024, x11::XFalse, utf8Atom,
                                          &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == x11::XSuccess) {
                        if (prop) {
                            std::string windowTitle(reinterpret_cast<char*>(prop));
                            if (windowTitle.find(title) != std::string::npos) {
                                ::Window result = children[i];
                                XFree(prop);
                                XFree(children);
                                return static_cast<wID>(result);
                            }
                            XFree(prop);
                        }
                    }
                }
            }
            XFree(children);
        }
    }
    #endif
    return 0;
}

// Find a window by its class
wID Window::FindByClass(cstr className) {
    #ifdef __linux__
    if (!display) return 0;
    
    ::Window rootWindow = DefaultRootWindow(display.get());
    ::Window parent;
    ::Window* children;
    unsigned int numChildren;
    
    if (XQueryTree(display.get(), rootWindow, &rootWindow, &parent, &children, &numChildren)) {
        if (children) {
            for (unsigned int i = 0; i < numChildren; i++) {
                XClassHint classHint;
                if (XGetClassHint(display.get(), children[i], &classHint)) {
                    bool match = false;
                    
                    if (classHint.res_name && strstr(classHint.res_name, className.c_str()) != nullptr) {
                        match = true;
                    }
                    else if (classHint.res_class && strstr(classHint.res_class, className.c_str()) != nullptr) {
                        match = true;
                    }
                    
                    // Debug logging
                    if (match) {
                        std::cout << "Found window with class matching '" << className 
                                  << "': res_name='" << (classHint.res_name ? classHint.res_name : "NULL") 
                                  << "', res_class='" << (classHint.res_class ? classHint.res_class : "NULL") 
                                  << "'" << std::endl;
                    }
                    
                    if (classHint.res_name) XFree(classHint.res_name);
                    if (classHint.res_class) XFree(classHint.res_class);
                    
                    if (match) {
                        ::Window result = children[i];
                        XFree(children);
                        return static_cast<wID>(result);
                    }
                }
            }
            XFree(children);
        }
    }
    #endif
    return 0;
}

// Template specializations for FindT
// These are already defined in the header file, so we don't need to redefine them here

// Title retrieval
std::string Window::Title(wID win) {
    if (!win) win = m_id;
    if (!win) return "";

#ifdef WINDOWS
    char title[256];
    if (GetWindowTextA(reinterpret_cast<HWND>(win), title, sizeof(title))) {
        return std::string(title);
    }
    return "";
#elif defined(__linux__)
    if (!display) {
        std::cerr << "Failed to open X11 display." << std::endl;
        return "";
    }

    Atom wmName = XInternAtom(display.get(), "_NET_WM_NAME", x11::XTrue);
    if (wmName == x11::XNone) {
        wmName = XInternAtom(display.get(), "WM_NAME", x11::XTrue);
        if (wmName == x11::XNone) {
            return "";
        }
    }

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(display.get(), win, wmName, 0, (~0L), x11::XFalse, AnyPropertyType,
                           &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == x11::XSuccess) {
        if (prop) {
            std::string title(reinterpret_cast<char*>(prop));
            XFree(prop);
            return title;
        }
    }
    return "";
#elif defined(__linux__) && defined(__WAYLAND__)
    // Placeholder for Wayland: Use wmctrl as a fallback
    std::ostringstream command;
    command << "wmctrl -l | grep " << reinterpret_cast<uintptr_t>(win);

    FILE* pipe = popen(command.str().c_str(), "r");
    if (!pipe) return "";
    
    char buffer[128];
    std::string result = "";
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
    }
    pclose(pipe);
    return result;
#else
    return "";
#endif
}

// Check if a window is active
bool Window::Active() {
    return Active(m_id);
}

bool Window::Active(wID win) {
#if defined(WINDOWS)
    return GetForegroundWindow() == reinterpret_cast<HWND>(win);
#elif defined(__linux__)
    return WindowManager::GetActiveWindow() == win;
#else
    return false;
#endif
}

// Function to check if a window exists
bool Window::Exists() {
    return Exists(m_id);
}
bool Window::Exists(wID win) {
#ifdef WINDOWS
    return IsWindow(reinterpret_cast<HWND>(win)) != 0;
#elif defined(__linux__)
    if (!display) return false;
    XWindowAttributes attr;
    return XGetWindowAttributes(display.get(), win, &attr) != 0;
#else
    return false;
#endif
}

void Window::Activate() {
    Activate(m_id);
}

void Window::Activate(wID win) {
#ifdef WINDOWS
    // Windows implementation
    if (win) {
        SetForegroundWindow(reinterpret_cast<HWND>(win));
        std::cout << "Activated: " << win << std::endl;
    }
#elif defined(__linux__)
if(WindowManager::get().IsX11()) {
    if (!win) return;
    // X11 implementation
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom activeAtom = XInternAtom(localDisplay, "_NET_ACTIVE_WINDOW", x11::XTrue);
    if (activeAtom != x11::XNone) {
        XEvent event = {};
        event.xclient.type = x11::XClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = activeAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // Source indication: 1 (application)
        event.xclient.data.l[1] = CurrentTime;

        XSendEvent(localDisplay, DefaultRootWindow(localDisplay), x11::XFalse, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(localDisplay);
    } else {
        error("Failed to find _NET_ACTIVE_WINDOW atom.");
    }
    XCloseDisplay(localDisplay);
} else if(WindowManager::get().IsWayland()) {
    // Wayland implementation using `wmctrl`
    if (win) {
        std::ostringstream command;
        command << "wmctrl -i -a " << std::hex << reinterpret_cast<uintptr_t>(win);
        auto ret = Launcher::runShell(command.str());
        if (!ret.success) {
            error("Command failed to activate window: " + ret.error);
        } else {
            debug("Activated window via wmctrl: " + std::to_string(win));
        }
    } else {
        error("Invalid window ID for Wayland activation.");
    }
} else {
    error("Platform not supported for Activate function.");
}
#else
    error("Platform not supported for Activate function.");
#endif
}

// Close a window
void Window::Close() {
    Close(m_id);
}
void Window::Close(wID win) {
#ifdef WINDOWS
    if (win) {
        SendMessage(reinterpret_cast<HWND>(win), WM_CLOSE, 0, 0);
        std::cout << "Closed: " << win << std::endl;
    }
#elif defined(__linux__)
    if (!win) return;
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom wmDelete = XInternAtom(localDisplay, "WM_DELETE_WINDOW", x11::XTrue);
    if (wmDelete != x11::XNone) {
        XEvent event = {};
        event.xclient.type = x11::XClientMessage;
        event.xclient.message_type = wmDelete;
        event.xclient.format = 32;
        event.xclient.data.l[0] = CurrentTime;

        XSendEvent(localDisplay, win, x11::XFalse, NoEventMask, &event);
        XFlush(localDisplay);
    }
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a universal API for closing windows.
    error("Window closing in Wayland is not implemented.");
#endif
}

void Window::Min() {
    Min(m_id);
}
void Window::Min(wID win) {
#ifdef WINDOWS
    if (win) {
        ShowWindow(reinterpret_cast<HWND>(win), SW_MINIMIZE);
        std::cout << "Minimized: " << win << std::endl;
    }
#elif defined(__linux__) 
    if (!win) return;
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) {
        std::cerr << "Failed to open X display" << std::endl;
        return;
    }
    // XIconifyWindow takes (Display*, Window, int screen_number)
    XIconifyWindow(localDisplay, win, DefaultScreen(localDisplay));
    XFlush(localDisplay);  // Ensure the command is sent to the server
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    std::cerr << "Window minimization in Wayland is not implemented." << std::endl;
#endif
}

// Maximize a window
void Window::Max() {
    Max(m_id);
}
void Window::Max(wID win) {
#ifdef WINDOWS
    if (win) {
        ShowWindow(reinterpret_cast<HWND>(win), SW_MAXIMIZE);
        std::cout << "Maximized: " << win << std::endl;
    }
#elif defined(__linux__)
    if (!win) return;
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom wmState = XInternAtom(localDisplay, "_NET_WM_STATE", x11::XTrue);
    Atom wmMaxVert = XInternAtom(localDisplay, "_NET_WM_STATE_MAXIMIZED_VERT", x11::XTrue);
    Atom wmMaxHorz = XInternAtom(localDisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", x11::XTrue);
    if (wmState != x11::XNone && wmMaxVert != x11::XNone && wmMaxHorz != x11::XNone) {
        XEvent event = {};
        event.xclient.type = x11::XClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // Add
        event.xclient.data.l[1] = wmMaxVert;
        event.xclient.data.l[2] = wmMaxHorz;

        XSendEvent(localDisplay, DefaultRootWindow(localDisplay), x11::XFalse, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(localDisplay);
    }
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a universal API for maximizing windows.
    std::cerr << "Window maximization in Wayland is not implemented." << std::endl;
#endif
}

// Set the transparency of a window
void Window::Transparency(int alpha) {
    Transparency(m_id, alpha);
}
void Window::Transparency(wID win, int alpha) {
#ifdef WINDOWS
    if (win && alpha >= 0 && alpha <= 255) {
        SetWindowLong(reinterpret_cast<HWND>(win), GWL_EXSTYLE, GetWindowLong(reinterpret_cast<HWND>(win), GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(reinterpret_cast<HWND>(win), 0, alpha, LWA_ALPHA);
    }
#elif defined(__linux__)
    if (!win) return;
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom opacityAtom = XInternAtom(localDisplay, "_NET_WM_WINDOW_OPACITY", x11::XFalse);
    if (opacityAtom != x11::XNone) {
        unsigned long opacity = static_cast<unsigned long>((alpha / 255.0) * 0xFFFFFFFF);
        XChangeProperty(localDisplay, win, opacityAtom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&opacity), 1);
    }
    XFlush(localDisplay);
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a universal API for setting transparency.
    std::cerr << "Transparency control in Wayland is not implemented." << std::endl;
#endif
}

// Set a window to always be on top
void Window::AlwaysOnTop(bool top) {
    AlwaysOnTop(m_id, top);
}
void Window::AlwaysOnTop(wID win, bool top) {
#ifdef WINDOWS
    SetWindowPos(reinterpret_cast<HWND>(win), top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#elif defined(__linux__)
    if (!win) return;
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom wmState = XInternAtom(localDisplay, "_NET_WM_STATE", x11::XTrue);
    Atom wmAbove = XInternAtom(localDisplay, "_NET_WM_STATE_ABOVE", x11::XTrue);
    if (wmState != x11::XNone && wmAbove != x11::XNone) {
        XEvent event = {};
        event.xclient.type = x11::XClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = top ? 1 : 0; // Add or Remove
        event.xclient.data.l[1] = wmAbove;

        XSendEvent(localDisplay, DefaultRootWindow(localDisplay), x11::XFalse, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(localDisplay);
    }
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a universal API for setting windows on top.
    std::cerr << "AlwaysOnTop in Wayland is not implemented." << std::endl;
#endif
}

void Window::SetAlwaysOnTopX11(wID win, bool top) {
    if (!display) return;

    Atom wmState = XInternAtom(display.get(), "_NET_WM_STATE", x11::XTrue);
    Atom wmAbove = XInternAtom(display.get(), "_NET_WM_STATE_ABOVE", x11::XTrue);
    
    if(wmState != x11::XNone && wmAbove != x11::XNone) {
        XEvent event = {};
        event.xclient.type = x11::XClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = top ? 1 : 0;
        event.xclient.data.l[1] = wmAbove;
        
        XSendEvent(display.get(), DefaultRootWindow(display.get()), x11::XFalse,
                  SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(display.get());
    }
}

// Find a window by its process ID
wID Window::GetwIDByPID(pID pid) {
    if (!display) return 0;
    
    ::Window rootWindow = DefaultRootWindow(display.get());
    ::Window parent;
    ::Window* children;
    unsigned int numChildren;
    
    if (XQueryTree(display.get(), rootWindow, &rootWindow, &parent, &children, &numChildren)) {
        if (children) {
            Atom pidAtom = XInternAtom(display.get(), "_NET_WM_PID", x11::XFalse);
            
            for (unsigned int i = 0; i < numChildren; i++) {
                Atom actualType;
                int actualFormat;
                unsigned long nitems, bytesAfter;
                unsigned char* prop = nullptr;
                
                if (XGetWindowProperty(display.get(), children[i], pidAtom, 0, 1, x11::XFalse, XA_CARDINAL,
                                      &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == x11::XSuccess) {
                    if (prop && nitems == 1) {
                        pID windowPid = *reinterpret_cast<pID*>(prop);
                        XFree(prop);
                        
                        if (windowPid == pid) {
                            ::Window result = children[i];
                            XFree(children);
                            return static_cast<wID>(result);
                        }
                    }
                    
                    if (prop) {
                        XFree(prop);
                    }
                }
            }
            XFree(children);
        }
    }
    
    return 0;
}

// === New Instance Methods for Window Manipulation ===

bool Window::Move(int x, int y, bool centerOnScreen) {
    if (!m_id) return false;
    return WindowManager::Move(m_id, x, y, centerOnScreen);
}

bool Window::Resize(int width, int height, bool fullscreen) {
    if (!m_id) return false;
    return WindowManager::Resize(m_id, width, height, fullscreen);
}

bool Window::MoveResize(int x, int y, int width, int height) {
    if (!m_id) return false;
    return WindowManager::MoveResize(m_id, x, y, width, height);
}

bool Window::Center() {
    if (!m_id) return false;
    return WindowManager::Center(m_id);
}

bool Window::MoveToCorner(const std::string& corner) {
    if (!m_id) return false;
    return WindowManager::MoveToCorner(m_id, corner);
}

bool Window::MoveToMonitor(int monitorIndex) {
    if (!m_id) return false;
    return WindowManager::MoveToMonitor(m_id, monitorIndex);
}

void Window::Snap(int position) {
    if (!m_id) return;
    WindowManager::SnapWindow(m_id, position);
}

void Window::ToggleFullscreen() {
    if (!m_id) return;
    WindowManager::ToggleFullscreen(m_id);
}

}