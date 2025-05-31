#include "Screen.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <cairo/cairo-xlib.h>
#include <algorithm>

namespace havel {

Screen::Screen() {
    UpdateMonitorInfo();
}

void Screen::CoverAllMonitors() {
    // Get X11 display
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        throw std::runtime_error("Failed to open X11 display");
    }
    
    // Get root window
    Window root = DefaultRootWindow(display);
    
    // Get screen dimensions
    int screenX = 0;
    int screenY = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    
    // Calculate total screen area
    for (const auto& monitor : monitors) {
        screenX = std::min(screenX, monitor.x);
        screenY = std::min(screenY, monitor.y);
        screenWidth = std::max(screenWidth, monitor.x + monitor.width);
        screenHeight = std::max(screenHeight, monitor.y + monitor.height);
    }
    
    // Create window attributes
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;  // Bypass window manager
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | ButtonPressMask | KeyPressMask |
                      PointerMotionMask | StructureNotifyMask;
    
    // Create window
    Window window = XCreateWindow(
        display,
        root,
        screenX, screenY,
        screenWidth, screenHeight,
        0,  // border width
        CopyFromParent,  // depth
        InputOutput,  // class
        CopyFromParent,  // visual
        CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
        &attrs
    );
    
    // Set window properties
    XStoreName(display, window, "Screen Overlay");
    
    // Make window semi-transparent if needed
    if (backgroundOpacity < 1.0f) {
        // Set _NET_WM_WINDOW_OPACITY
        unsigned long opacity = static_cast<unsigned long>(backgroundOpacity * 0xFFFFFFFF);
        Atom _NET_WM_WINDOW_OPACITY = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
        XChangeProperty(
            display, window,
            _NET_WM_WINDOW_OPACITY,
            XA_CARDINAL, 32,
            PropModeReplace,
            reinterpret_cast<unsigned char*>(&opacity), 1
        );
    }
    
    // Show window
    XMapWindow(display, window);
    XFlush(display);
    
    // Store window info
    bounds = {screenX, screenY, screenWidth, screenHeight};
    
    // Clean up
    XCloseDisplay(display);
}

void Screen::CoverMonitor(int monitorIndex) {
    auto it = std::find_if(monitors.begin(), monitors.end(),
        [monitorIndex](const Monitor& m) { return m.index == monitorIndex; });
    
    if (it != monitors.end()) {
        bounds = {it->x, it->y, it->width, it->height};
        NeedsRedraw();
    }
}

void Screen::SetBackgroundColor(uint32_t color) {
    if (backgroundColor == color) return;
    backgroundColor = color;
    NeedsRedraw();
}

void Screen::SetBackgroundOpacity(float opacity) {
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (backgroundOpacity == opacity) return;
    backgroundOpacity = opacity;
    NeedsRedraw();
}

void Screen::BlockInput(bool block) {
    if (blockInput == block) return;
    blockInput = block;
    
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    
    if (block) {
        // Grab keyboard and pointer
        XGrabKeyboard(display, DefaultRootWindow(display),
            True, GrabModeAsync, GrabModeAsync, CurrentTime);
        XGrabPointer(display, DefaultRootWindow(display),
            True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    } else {
        // Release grabs
        XUngrabKeyboard(display, CurrentTime);
        XUngrabPointer(display, CurrentTime);
    }
    
    XFlush(display);
    XCloseDisplay(display);
}

void Screen::AllowClickThrough(bool allow) {
    if (clickThrough == allow) return;
    clickThrough = allow;
    
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    
    // Update window input shape
    if (allow) {
        // Create empty region for input
        XRectangle rect = {0, 0, 1, 1};
        XShapeCombineRectangles(display, bounds.x, bounds.y,
            ShapeInput, 0, &rect, 1, ShapeSet, YXBanded);
    } else {
        // Reset input shape
        XShapeCombineMask(display, bounds.x, bounds.y,
            ShapeInput, 0, 0, None, ShapeSet);
    }
    
    XFlush(display);
    XCloseDisplay(display);
}

void Screen::Draw(cairo_t* cr) {
    if (!isVisible) return;
    
    // Draw background
    cairo_save(cr);
    
    // Set color with opacity
    cairo_set_source_rgba(cr,
        ((backgroundColor >> 16) & 0xFF) / 255.0,
        ((backgroundColor >> 8) & 0xFF) / 255.0,
        (backgroundColor & 0xFF) / 255.0,
        ((backgroundColor >> 24) & 0xFF) / 255.0 * backgroundOpacity);
    
    // Fill entire bounds
    cairo_rectangle(cr, bounds.x, bounds.y, bounds.width, bounds.height);
    cairo_fill(cr);
    
    cairo_restore(cr);
}

void Screen::HandleEvent(const GUIEvent& event) {
    if (!isEnabled) return;
    
    switch (event.type) {
        case GUIEvent::Type::KEY_PRESS:
            if (event.keycode == KEY_ESCAPE && !blockInput) {
                isVisible = false;
                NeedsRedraw();
            }
            break;
            
        case GUIEvent::Type::MOUSE_CLICK:
            if (!blockInput && !clickThrough) {
                isVisible = false;
                NeedsRedraw();
            }
            break;
            
        default:
            break;
    }
}

bool Screen::Contains(int x, int y) const {
    return x >= bounds.x && x < bounds.x + bounds.width &&
           y >= bounds.y && y < bounds.y + bounds.height;
}

void Screen::UpdateMonitorInfo() {
    monitors.clear();
    
    Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    
    // Get XRandR screen resources
    XRRScreenResources* res = XRRGetScreenResources(display, DefaultRootWindow(display));
    if (!res) {
        XCloseDisplay(display);
        return;
    }
    
    // Get monitor info
    for (int i = 0; i < res->ncrtc; i++) {
        XRRCrtcInfo* crtc = XRRGetCrtcInfo(display, res, res->crtcs[i]);
        if (!crtc) continue;
        
        if (crtc->width > 0 && crtc->height > 0) {
            monitors.push_back({
                static_cast<int>(crtc->x),
                static_cast<int>(crtc->y),
                static_cast<int>(crtc->width),
                static_cast<int>(crtc->height),
                i
            });
        }
        
        XRRFreeCrtcInfo(crtc);
    }
    
    XRRFreeScreenResources(res);
    XCloseDisplay(display);
}

} // namespace havel 