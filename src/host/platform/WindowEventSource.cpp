#include "WindowEventSource.hpp"

#include <X11/Xlib.h>

#include "x11.h"
#include "utils/Logger.hpp"

namespace havel::compiler {

void WindowEventSource::start(EventRuntime &runtime) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) {
        return;
    }
    runtime_ = &runtime;
    running_.store(true);
    thread_ = std::thread(&WindowEventSource::loop, this);
}

void WindowEventSource::stop() {
    running_.store(false);
    // Always join: a thread whose loop exited early (e.g. XOpenDisplay
    // failed in a headless session) is still joinable — destroying a
    // joinable thread is std::terminate.
    if (thread_.joinable()) {
        thread_.join();
    }
}

void WindowEventSource::loop() {
    // Own X connection: the hotkey monitor owns the root's KeyPress mask;
    // a second connection selecting SubstructureNotify/FocusChange keeps
    // the two loops independent (X11 allows per-connection select masks).
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        ::havel::error("[WindowEventSource] XOpenDisplay failed — window "
                       "events disabled (headless?)");
        running_.store(false);
        return;
    }
    Window root = DefaultRootWindow(display);
    XSelectInput(display, root,
                 SubstructureNotifyMask | SubstructureRedirectMask |
                     FocusChangeMask | StructureNotifyMask);
    XSync(display, x11::XFalse);
    ::havel::info("[WindowEventSource] watching root window events");

    XEvent event;
    while (running_.load()) {
        // XEventsQueued with a bounded sleep keeps the loop responsive to
        // running_ = false (XNextEvent would block until an event).
        int pending = XEventsQueued(display, QueuedAfterFlush);
        if (pending == 0) {
            XSync(display, x11::XFalse);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (XNextEvent(display, &event) != 0) {
            ::havel::error("[WindowEventSource] XNextEvent failed — X11 "
                           "connection error");
            break;
        }

        std::string eventName;
        unsigned long windowId = 0;
        switch (event.type) {
        case x11::XMapNotify:
            eventName = "window.created";
            windowId = event.xmap.window;
            break;
        case x11::XUnmapNotify:
            eventName = "window.hidden";
            windowId = event.xunmap.window;
            break;
        case x11::XDestroyNotify:
            eventName = "window.destroyed";
            windowId = event.xdestroywindow.window;
            break;
        case x11::XConfigureNotify: {
            // ConfigureNotify cascades for the same window; identical
            // consecutive configure events are skipped (coalescing).
            static unsigned long lastWindow = 0;
            static int lastX = 0, lastY = 0, lastW = 0, lastH = 0;
            const XConfigureEvent &ce = event.xconfigure;
            if (ce.window == lastWindow && ce.x == lastX && ce.y == lastY &&
                ce.width == lastW && ce.height == lastH) {
                continue;
            }
            lastWindow = ce.window;
            lastX = ce.x;
            lastY = ce.y;
            lastW = ce.width;
            lastH = ce.height;
            if (!runtime_) {
                continue;
            }
            EventPayload payload;
            payload.fields["window"] = std::to_string(ce.window);
            payload.fields["x"] = std::to_string(ce.x);
            payload.fields["y"] = std::to_string(ce.y);
            payload.fields["width"] = std::to_string(ce.width);
            payload.fields["height"] = std::to_string(ce.height);
            runtime_->publish("window.moved", payload);
            runtime_->publish("window.resized", std::move(payload));
            continue;
        }
        case x11::XFocusIn:
            eventName = "window.focused";
            windowId = event.xfocus.window;
            break;
        case x11::XFocusOut:
            eventName = "window.unfocused";
            windowId = event.xfocus.window;
            break;
        default:
            continue;
        }

        if (!runtime_ || eventName.empty()) {
            continue;
        }
        EventPayload payload;
        payload.fields["window"] = std::to_string(windowId);
        runtime_->publish(eventName, std::move(payload));
    }

    XCloseDisplay(display);
}

} // namespace havel::compiler
