#pragma once
#include "x11.h"
#include "types.hpp"
#include "havel-lang/common/Export.hpp"

namespace havel {
// In DisplayManager.hpp - Add monitor geometry methods:
class HAVEL_EXPORT DisplayManager {
    public:
        struct MonitorInfo {
            std::string name;
            int x, y;           // Position
            int width, height;  // Resolution  
            bool isPrimary;
            wID id {0};            // For XRandR/Wayland output ID
            wID crtc_id {0};       // For XRandR
        };
        // An X11 Display and its root Window have no Windows spelling, so they
        // exist only where they exist. Every Xlib-specific entry point is gated
        // with them; Windows enumerates monitors through GetMonitors() instead.
        // Stubbing them as void* would only move the failure out to every
        // caller.
        static bool initialized;
        static bool headlessMode;
        static void SetHeadlessMode(bool headless) { headlessMode = headless; }
        static bool IsHeadlessMode() { return headlessMode; }
        static void Initialize();
        static void Close();
#if HAVEL_PLATFORM_POSIX
        static Display* display;
        static ::Window root;
        static Display* GetDisplay();
        static ::Window GetRootWindow();
#endif
        static bool IsInitialized();
        
        static std::vector<MonitorInfo> GetMonitors();
        static MonitorInfo GetMonitorAt(int x, int y);  // Point-to-monitor
        static MonitorInfo GetPrimaryMonitor();
        static MonitorInfo GetMonitorByName(const std::string& name);
        static MonitorInfo GetMonitorByID(wID id);
        static MonitorInfo GetMonitorByIndex(int index);
        static std::vector<std::string> GetMonitorNames();
        
        // Utility methods
        static bool IsPointOnMonitor(int x, int y, const MonitorInfo& monitor);
        static std::string GetMonitorNameAt(int x, int y);
    
    private:
        struct Cleanup {
            ~Cleanup();
        };

        static std::vector<MonitorInfo> cached_monitors;
        static void RefreshMonitorCache();
        
#if HAVEL_PLATFORM_POSIX
        // X11 implementation
        static std::vector<MonitorInfo> GetMonitorsX11();
        static int X11ErrorHandler(Display* display, XErrorEvent* event);
#elif HAVEL_PLATFORM_WINDOWS
        // Win32 implementation
        static std::vector<MonitorInfo> GetMonitorsWindows();
#endif
        
        #ifdef __WAYLAND__
        // Wayland implementation  
        static std::vector<MonitorInfo> GetMonitorsWayland();
        #endif
    };
} // namespace havel