#include "DisplayManager.hpp"
#include "utils/Logger.hpp"
#include "x11.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <X11/extensions/Xrandr.h>

#ifdef __WAYLAND__
#include <wayland-client.h>
// Include your Wayland protocol headers
#include "xdg-output-unstable-v1-client-protocol.h"
#endif

namespace havel {
    Display* DisplayManager::display = nullptr;
    ::Window DisplayManager::root = 0;
    bool DisplayManager::initialized = false;
    std::vector<DisplayManager::MonitorInfo> DisplayManager::cached_monitors;
    
    void DisplayManager::Initialize() {
        if (!initialized) {
            display = XOpenDisplay(nullptr);
            if (display) {
                root = DefaultRootWindow(display);
                // Register cleanup on exit
                static Cleanup cleanup;
                XSetErrorHandler(X11ErrorHandler);
                XSetIOErrorHandler([](Display* display) -> int {
                    (void)display; // Mark as unused
                    std::cerr << "X11 I/O Error - Display connection lost\n";
                    std::exit(EXIT_FAILURE);
                    return 0;
                });
                
                // Initialize XRandR extension
                int xrandr_event_base, xrandr_error_base;
                if (!XRRQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
                    std::cerr << "Warning: XRandR extension not available\n";
                }
                
                initialized = true;
                RefreshMonitorCache(); // Cache monitor info on init
            }
        }
    }
    
    void DisplayManager::Close() {
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
            initialized = false;
            cached_monitors.clear();
        }
    }
    
    Display* DisplayManager::GetDisplay() {
        Initialize();
        return display;
    }
    
    ::Window DisplayManager::GetRootWindow() {
        Initialize();
        if (!display) {
            throw std::runtime_error("No X11 display available");
        }
        return root;
    }
    
    bool DisplayManager::IsInitialized() {
        return display != nullptr;
    }
    
    // ===== MONITOR MANAGEMENT METHODS =====
    
    std::vector<DisplayManager::MonitorInfo> DisplayManager::GetMonitors() {
        Initialize();
        if (cached_monitors.empty()) {
            RefreshMonitorCache();
        }
        return cached_monitors;
    }
    
    DisplayManager::MonitorInfo DisplayManager::GetMonitorAt(int x, int y) {
        auto monitors = GetMonitors();
        for (const auto& monitor : monitors) {
            if (IsPointOnMonitor(x, y, monitor)) {
                return monitor;
            }
        }
        // Return primary as fallback
        return GetPrimaryMonitor();
    }
    
    DisplayManager::MonitorInfo DisplayManager::GetPrimaryMonitor() {
        auto monitors = GetMonitors();
        
        // First, try to find explicitly marked primary
        for (const auto& monitor : monitors) {
            if (monitor.isPrimary) {
                return monitor;
            }
        }
        
        // Fallback: return first monitor or empty if none
        if (!monitors.empty()) {
            return monitors[0];
        }
        
        // Return empty monitor info if no monitors found
        return MonitorInfo{};
    }
    
    DisplayManager::MonitorInfo DisplayManager::GetMonitorByName(const std::string& name) {
        auto monitors = GetMonitors();
        for (const auto& monitor : monitors) {
            if (monitor.name == name) {
                return monitor;
            }
        }
        return MonitorInfo{}; // Return empty if not found
    }
    
    DisplayManager::MonitorInfo DisplayManager::GetMonitorByID(wID id) {
        auto monitors = GetMonitors();
        for (const auto& monitor : monitors) {
            if (monitor.id == id) {
                return monitor;
            }
        }
        return MonitorInfo{}; // Return empty if not found
    }
    
    DisplayManager::MonitorInfo DisplayManager::GetMonitorByIndex(int index) {
        auto monitors = GetMonitors();
        if (index >= 0 && index < static_cast<int>(monitors.size())) {
            return monitors[index];
        }
        return MonitorInfo{}; // Return empty if index out of range
    }
    
    bool DisplayManager::IsPointOnMonitor(int x, int y, const MonitorInfo& monitor) {
        return (x >= monitor.x && x < monitor.x + monitor.width &&
                y >= monitor.y && y < monitor.y + monitor.height);
    }
    
    std::string DisplayManager::GetMonitorNameAt(int x, int y) {
        auto monitor = GetMonitorAt(x, y);
        return monitor.name;
    }
    
    void DisplayManager::RefreshMonitorCache() {
        cached_monitors.clear();
        
        // Detect display method (could be made configurable)
        const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
        if (waylandDisplay && strlen(waylandDisplay) > 0) {
#ifdef __WAYLAND__
            cached_monitors = GetMonitorsWayland();
            if (!cached_monitors.empty()) {
                return;
            }
#endif
        }
        
        // Fall back to X11
        cached_monitors = GetMonitorsX11();
    }
    
    std::vector<DisplayManager::MonitorInfo> DisplayManager::GetMonitorsX11() {
        std::vector<MonitorInfo> monitors;
        
        if (!display) {
            std::cerr << "Error: X11 display not initialized\n";
            return monitors;
        }
        
        XRRScreenResources* screen_res = XRRGetScreenResourcesCurrent(display, root);
        if (!screen_res) {
            std::cerr << "Error: Failed to get X11 screen resources\n";
            return monitors;
        }
        
        // Get primary output
        RROutput primary_output = XRRGetOutputPrimary(display, root);
        
        for (int i = 0; i < screen_res->noutput; ++i) {
            XRROutputInfo* output_info = XRRGetOutputInfo(display, screen_res, screen_res->outputs[i]);
            if (output_info && output_info->connection == RR_Connected && output_info->crtc) {
                
                XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display, screen_res, output_info->crtc);
                if (crtc_info) {
                    MonitorInfo monitor;
                    monitor.name = std::string(output_info->name);
                    monitor.x = crtc_info->x;
                    monitor.y = crtc_info->y;
                    monitor.width = crtc_info->width;
                    monitor.height = crtc_info->height;
                    monitor.isPrimary = (screen_res->outputs[i] == primary_output);
                    monitor.id = screen_res->outputs[i];
                    monitor.crtc_id = output_info->crtc;
                    
                    monitors.push_back(monitor);
                    
                    // Debug logging
                    std::cout << "Found monitor: " << monitor.name 
                              << " (" << monitor.width << "x" << monitor.height 
                              << " at " << monitor.x << "," << monitor.y << ")"
                              << (monitor.isPrimary ? " [PRIMARY]" : "") << std::endl;
                    
                    XRRFreeCrtcInfo(crtc_info);
                }
            }
            if (output_info) {
                XRRFreeOutputInfo(output_info);
            }
        }
        
        XRRFreeScreenResources(screen_res);
        
        // If no primary was found, mark first monitor as primary
        if (!monitors.empty() && primary_output == None) {
            monitors[0].isPrimary = true;
        }
        
        return monitors;
    }

#ifdef __WAYLAND__
    std::vector<DisplayManager::MonitorInfo> DisplayManager::GetMonitorsWayland() {
        std::vector<MonitorInfo> monitors;
        
        // Note: Full Wayland implementation would require setting up
        // the Wayland registry, output manager, and handling async events.
        // This is a simplified placeholder that should be expanded based on
        // your specific Wayland protocol integration.
        
        std::cerr << "Warning: Wayland monitor detection not fully implemented\n";
        
        // You would implement this similar to how BrightnessManager handles Wayland:
        // 1. Connect to Wayland display
        // 2. Get registry and bind to xdg-output protocol
        // 3. Iterate through outputs and collect geometry info
        // 4. Handle async events to populate monitor info
        
        return monitors;
    }
#endif
    
    // ===== ERROR HANDLING =====
    
    int DisplayManager::X11ErrorHandler(Display* display, XErrorEvent* event) {
        char errorText[256];
        XGetErrorText(display, event->error_code, errorText, sizeof(errorText));
        std::cerr << "X11 Error: " << errorText 
                  << " (code: " << static_cast<int>(event->error_code)
                  << ", request: " << static_cast<int>(event->request_code)
                  << ", minor: " << static_cast<int>(event->minor_code) << ")"
                  << std::endl;
        return 0; // Continue execution
    }
    
    // ===== CLEANUP =====
    
    DisplayManager::Cleanup::~Cleanup() {
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
            initialized = false;
        }
    }
    std::vector<std::string> DisplayManager::GetMonitorNames(){
        auto monitors = GetMonitors();
        std::vector<std::string> names;
        for (const auto& monitor : monitors) {
            names.push_back(monitor.name);
        }
        return names;
    }
} // namespace havel