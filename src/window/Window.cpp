#include "Window.hpp"
#include "WindowManager.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cstring>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <cstdio>
#include <unistd.h>
#include <sys/types.h>
#include <cerrno>
#include <cstring>

#ifdef __linux__
#include <X11/Xatom.h>
#include <X11/Xutil.h>

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
    if(XGetWindowAttributes(display.get(), win, &attrs)) {
        return {attrs.x, attrs.y, attrs.width, attrs.height};
    }
    return {};
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
                Atom nameAtom = XInternAtom(display.get(), "_NET_WM_NAME", False);
                Atom utf8Atom = XInternAtom(display.get(), "UTF8_STRING", False);
                
                if (nameAtom != None && utf8Atom != None) {
                    Atom actualType;
                    int actualFormat;
                    unsigned long nitems, bytesAfter;
                    unsigned char* prop = nullptr;
                    
                    if (XGetWindowProperty(display.get(), children[i], nameAtom, 0, 1024, False, utf8Atom,
                                          &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success) {
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

    Atom wmName = XInternAtom(display.get(), "_NET_WM_NAME", True);
    if (wmName == None) {
        return "";
    }

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(display.get(), win, wmName, 0, (~0L), False, AnyPropertyType,
                           &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success) {
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
bool Window::Active(wID win) {
    if (!win) win = m_id;

#if defined(WINDOWS)
    return GetForegroundWindow() == reinterpret_cast<HWND>(win);
#elif defined(__linux__)
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return false;

    wID active = 0;
    Atom activeAtom = XInternAtom(localDisplay, "_NET_ACTIVE_WINDOW", True);
    if (activeAtom == None) {
        XCloseDisplay(localDisplay);
        return false;
    }

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(localDisplay, DefaultRootWindow(localDisplay), activeAtom, 0, (~0L), False, AnyPropertyType,
                           &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success) {
        if (prop) {
            active = *reinterpret_cast<wID*>(prop);
            XFree(prop);
            XCloseDisplay(localDisplay);
            return active == win;
        }
    }
    XCloseDisplay(localDisplay);
    return false;
#else
    return false;
#endif
}

// Function to check if a window exists
bool Window::Exists(wID win) {
    if (!win) win = m_id;

#ifdef WINDOWS
    return IsWindow(reinterpret_cast<HWND>(win)) != 0;
#elif defined(__linux__)
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return false;

    XWindowAttributes attr;
    bool exists = XGetWindowAttributes(localDisplay, win, &attr) != 0;
    XCloseDisplay(localDisplay);
    return exists;
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a direct API to check window existence.
    std::cerr << "Window existence check in Wayland is not implemented." << std::endl;
    return false;
#else
    return false;
#endif
}

void Window::Activate(wID win) {
    if (!win) win = m_id;

#ifdef WINDOWS
    // Windows implementation
    if (win) {
        SetForegroundWindow(reinterpret_cast<HWND>(win));
        std::cout << "Activated: " << win << std::endl;
    }
#elif defined(__linux__)
    // X11 implementation
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom activeAtom = XInternAtom(localDisplay, "_NET_ACTIVE_WINDOW", True);
    if (activeAtom != None) {
        XEvent event = {};
        event.xclient.type = ClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = activeAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // Source indication: 1 (application)
        event.xclient.data.l[1] = CurrentTime;

        XSendEvent(localDisplay, DefaultRootWindow(localDisplay), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(localDisplay);
    } else {
        std::cerr << "Failed to find _NET_ACTIVE_WINDOW atom." << std::endl;
    }
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland implementation using `wmctrl`
    if (win) {
        std::ostringstream command;
        command << "wmctrl -i -a " << std::hex << reinterpret_cast<uintptr_t>(win);
        int ret = system(command.str().c_str());
        if (ret == -1) {
            std::cerr << "Failed to execute wmctrl command to activate window." << std::endl;
        } else {
            std::cout << "Activated window via wmctrl: " << win << std::endl;
        }
    } else {
        std::cerr << "Invalid window ID for Wayland activation." << std::endl;
    }
#else
    std::cerr << "Platform not supported for Activate function." << std::endl;
#endif
}

// Close a window
void Window::Close(wID win) {
    if (!win) win = m_id;

#ifdef WINDOWS
    if (win) {
        SendMessage(reinterpret_cast<HWND>(win), WM_CLOSE, 0, 0);
        std::cout << "Closed: " << win << std::endl;
    }
#elif defined(__linux__)
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom wmDelete = XInternAtom(localDisplay, "WM_DELETE_WINDOW", True);
    if (wmDelete != None) {
        XEvent event = {};
        event.xclient.type = ClientMessage;
        event.xclient.message_type = wmDelete;
        event.xclient.format = 32;
        event.xclient.data.l[0] = CurrentTime;

        XSendEvent(localDisplay, win, False, NoEventMask, &event);
        XFlush(localDisplay);
    }
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a universal API for closing windows.
    std::cerr << "Window closing in Wayland is not implemented." << std::endl;
#endif
}

void Window::Min(wID win) {
    if (!win) win = m_id;
    if (!win) return; // Early return if no valid window ID

#ifdef WINDOWS
    if (win) {
        ShowWindow(reinterpret_cast<HWND>(win), SW_MINIMIZE);
        std::cout << "Minimized: " << win << std::endl;
    }
#elif defined(__linux__) 
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
void Window::Max(wID win) {
    if (!win) win = m_id;

#ifdef WINDOWS
    if (win) {
        ShowWindow(reinterpret_cast<HWND>(win), SW_MAXIMIZE);
        std::cout << "Maximized: " << win << std::endl;
    }
#elif defined(__linux__)
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom wmState = XInternAtom(localDisplay, "_NET_WM_STATE", True);
    Atom wmMaxVert = XInternAtom(localDisplay, "_NET_WM_STATE_MAXIMIZED_VERT", True);
    Atom wmMaxHorz = XInternAtom(localDisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", True);
    if (wmState != None && wmMaxVert != None && wmMaxHorz != None) {
        XEvent event = {};
        event.xclient.type = ClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1; // Add
        event.xclient.data.l[1] = wmMaxVert;
        event.xclient.data.l[2] = wmMaxHorz;

        XSendEvent(localDisplay, DefaultRootWindow(localDisplay), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(localDisplay);
    }
    XCloseDisplay(localDisplay);
#elif defined(__linux__) && defined(__WAYLAND__)
    // Wayland does not provide a universal API for maximizing windows.
    std::cerr << "Window maximization in Wayland is not implemented." << std::endl;
#endif
}

// Set the transparency of a window
void Window::Transparency(wID win, int alpha) {
    if (!win) win = m_id;

#ifdef WINDOWS
    if (win && alpha >= 0 && alpha <= 255) {
        SetWindowLong(reinterpret_cast<HWND>(win), GWL_EXSTYLE, GetWindowLong(reinterpret_cast<HWND>(win), GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(reinterpret_cast<HWND>(win), 0, alpha, LWA_ALPHA);
    }
#elif defined(__linux__)
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom opacityAtom = XInternAtom(localDisplay, "_NET_WM_WINDOW_OPACITY", False);
    if (opacityAtom != None) {
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
void Window::AlwaysOnTop(wID win, bool top) {
    if (!win) win = m_id;

#ifdef WINDOWS
    SetWindowPos(reinterpret_cast<HWND>(win), top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#elif defined(__linux__)
    Display* localDisplay = XOpenDisplay(nullptr);
    if (!localDisplay) return;

    Atom wmState = XInternAtom(localDisplay, "_NET_WM_STATE", True);
    Atom wmAbove = XInternAtom(localDisplay, "_NET_WM_STATE_ABOVE", True);
    if (wmState != None && wmAbove != None) {
        XEvent event = {};
        event.xclient.type = ClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = top ? 1 : 0; // Add or Remove
        event.xclient.data.l[1] = wmAbove;

        XSendEvent(localDisplay, DefaultRootWindow(localDisplay), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
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

    Atom wmState = XInternAtom(display.get(), "_NET_WM_STATE", True);
    Atom wmAbove = XInternAtom(display.get(), "_NET_WM_STATE_ABOVE", True);
    
    if(wmState != None && wmAbove != None) {
        XEvent event = {};
        event.xclient.type = ClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = wmState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = top ? 1 : 0;
        event.xclient.data.l[1] = wmAbove;
        
        XSendEvent(display.get(), DefaultRootWindow(display.get()), False,
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
            Atom pidAtom = XInternAtom(display.get(), "_NET_WM_PID", False);
            
            for (unsigned int i = 0; i < numChildren; i++) {
                Atom actualType;
                int actualFormat;
                unsigned long nitems, bytesAfter;
                unsigned char* prop = nullptr;
                
                if (XGetWindowProperty(display.get(), children[i], pidAtom, 0, 1, False, XA_CARDINAL,
                                      &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success) {
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
}