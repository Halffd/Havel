#pragma once
#include "x11.h"
#include "types.hpp"

namespace havel {
// In DisplayManager.hpp - Add monitor geometry methods:
class DisplayManager {
    public:
        struct MonitorInfo {
            std::string name;
            int x, y;           // Position
            int width, height;  // Resolution  
            bool isPrimary;
            wID id {0};            // For XRandR/Wayland output ID
            wID crtc_id {0};       // For XRandR
        };
        static Display* display;
        static ::Window root;
        static bool initialized;
        static void Initialize();
        static Display* GetDisplay();
        static ::Window GetRootWindow();
        
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
        static std::vector<MonitorInfo> cached_monitors;
        static void RefreshMonitorCache();
        
        // X11 implementation
        static std::vector<MonitorInfo> GetMonitorsX11();
        
        #ifdef __WAYLAND__
        // Wayland implementation  
        static std::vector<MonitorInfo> GetMonitorsWayland();
        #endif
    };
} // namespace havel