#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <map>
#include <vector>
#ifndef Q_MOC_RUN
#include <cstdlib> // For getenv()

// Platform detection and macro definitions
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <process.h>
    #include <psapi.h>
    #include <winuser.h>
    #define WINDOWS 1
    #define OS_NAME "Windows"
    using wID = HWND;
    using pID = DWORD;  
    using Key = int;
    // #define DESKTOP_ENVIRONMENT "Unknown" // Placeholder for Windows
    // #define WINDOW_MANAGER "Unknown" // Placeholder for Windows
#elif defined(__linux__)
    #include <unistd.h>
    #include <pwd.h>
    #include <sys/wait.h>
    #include <sys/resource.h>
    #include <csignal>
    #include <cstdlib>
    #include <sstream>
    #include <fstream>
    #define LINUX_USED
    #define __X11__ 1
    #define OS_NAME "Linux"
    // Macros to retrieve desktop environment and window manager
    #define DESKTOP_ENVIRONMENT (getenv("XDG_CURRENT_DESKTOP") ? getenv("XDG_CURRENT_DESKTOP") : "Unknown")
    #define WINDOW_MANAGER (getenv("WM_NAME") ? getenv("WM_NAME") : "Unknown") // WM_NAME is not standard; adjust as needed
    #ifdef WAYLAND
    #include <wayland-client.h>
    #include <wayland-cursor.h>
    #include <xkbcommon/xkbcommon.h>
    using wID = void*; // Use void* or an appropriate type for Linux
    #else
    #include "x11_defs.h" // Include X11 macro undefinitions first
    #include "x11.h"
    #ifdef __X11__
    #undef Window
    #undef None
    #endif
    using wID = unsigned long; // X11 Window type
    using Key = unsigned long;
    #endif
    using pID = pid_t; // Example type for process ID
#elif defined(__APPLE__)
    #include <unistd.h>
    #define MAC 3
    #define OS_NAME "macOS"
    #define DESKTOP_ENVIRONMENT "Aqua" // Placeholder for macOS
    #define WINDOW_MANAGER "Unknown" // Placeholder for macOS
    using wID = void*; // Use void* or an appropriate type for macOS
    using pID = pid_t; // Example type for process ID
#else
    #error "Unsupported platform"
#endif
#endif
using str = std::string; // Alias for string type
using cstr = const str&; // Alias for const string reference
using group = std::map<str, std::vector<str>>; // Alias for a map of string to vector of strings
using null = decltype(nullptr); // Use nullptr instead of NULL for better type safety
namespace havel {
    enum class DisplayServer {
        X11,
        Wayland,
        Unknown
    };
}
#endif // TYPES_H