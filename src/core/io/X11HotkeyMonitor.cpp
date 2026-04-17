#ifdef __linux__

#include "X11HotkeyMonitor.hpp"
#include "../IO.hpp"
#include "../../havel-lang/compiler/runtime/EventQueue.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace spdlog;

namespace havel {

X11HotkeyMonitor::X11HotkeyMonitor() {
    info("X11HotkeyMonitor created");
}

X11HotkeyMonitor::~X11HotkeyMonitor() {
    Stop();
}

bool X11HotkeyMonitor::Start(Display* disp) {
    if (running.load()) {
        warn("X11HotkeyMonitor already running");
        return false;
    }

    if (!disp) {
        error("Display is null, cannot start X11 hotkey monitoring");
        return false;
    }

    display = disp;

    if (!XInitThreads()) {
        error("Failed to initialize X11 threading support");
        return false;
    }

    rootWindow = DefaultRootWindow(display);
    XSelectInput(display, rootWindow, KeyPressMask | KeyReleaseMask);

    running = true;
    shutdown = false;

    monitorThread = std::thread(&X11HotkeyMonitor::MonitorLoop, this);

    info("X11HotkeyMonitor started");
    return true;
}

void X11HotkeyMonitor::Stop() {
    if (!running.load()) {
        return;
    }

    info("Stopping X11HotkeyMonitor...");
    running = false;
    shutdown = true;

    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    info("X11HotkeyMonitor stopped");
}

void X11HotkeyMonitor::RegisterHotkey(int id, const HotKey& hotkey) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    hotkeys[id] = hotkey;
    debug("X11 hotkey registered: {} (id: {})", hotkey.alias, id);
}

void X11HotkeyMonitor::UnregisterHotkey(int id) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    hotkeys.erase(id);
    debug("X11 hotkey unregistered: id {}", id);
}

void X11HotkeyMonitor::ClearHotkeys() {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    hotkeys.clear();
    info("All X11 hotkeys cleared");
}

size_t X11HotkeyMonitor::GetHotkeyCount() const {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    return hotkeys.size();
}

void X11HotkeyMonitor::MonitorLoop() {
    info("X11 hotkey monitoring loop started");

    XEvent event;
    std::vector<int> triggeredHotkeyIds;
    triggeredHotkeyIds.reserve(16);

    // Adaptive sleep: start with longer sleep, reduce on activity
    constexpr auto MAX_SLEEP = std::chrono::milliseconds(100);
    constexpr auto MIN_SLEEP = std::chrono::milliseconds(5);
    auto currentSleep = MAX_SLEEP;
    auto lastActivityTime = std::chrono::steady_clock::now();

    try {
        while (running.load() && !shutdown.load()) {
            if (!display) {
                error("Display connection lost");
                break;
            }

            // Use XEventsQueued with QueuedAfterFlush for better performance
            // This batches requests and reduces X11 round-trips
            int pendingEvents = XEventsQueued(display, QueuedAfterFlush);

            if (pendingEvents == 0) {
                // No events - use adaptive sleep
                // Increase sleep time gradually when idle
                auto idleTime = std::chrono::steady_clock::now() - lastActivityTime;
                if (idleTime > std::chrono::seconds(5)) {
                    currentSleep = MAX_SLEEP;  // Long sleep when truly idle
                } else if (idleTime > std::chrono::seconds(1)) {
                    currentSleep = std::chrono::milliseconds(50);
                }

                std::this_thread::sleep_for(currentSleep);
                continue;
            }

            // Activity detected - reduce sleep time for responsiveness
            currentSleep = MIN_SLEEP;
            lastActivityTime = std::chrono::steady_clock::now();

            // Process all pending events in batch
            for (int i = 0; i < pendingEvents && running.load(); ++i) {
                if (XNextEvent(display, &event) != 0) {
                    error("XNextEvent failed - X11 connection error");
                    running = false;
                    break;
                }

                try {
                    // Only process key events
                    if (event.type != x11::XKeyPress && event.type != x11::XKeyRelease) {
                        continue;
                    }

                    const bool isDown = (event.type == x11::XKeyPress);
                    const XKeyEvent* keyEvent = &event.xkey;

                    // Clean modifier state
                    const unsigned int cleanedState = CleanMask(keyEvent->state);
                    triggeredHotkeyIds.clear();

                    // Find matching hotkeys
                    {
                        std::lock_guard<std::mutex> lock(hotkeyMutex);
                        for (const auto& [id, hotkey] : hotkeys) {
                            if (!hotkey.enabled) continue;

                            // Match key and modifiers
                            if (hotkey.key == static_cast<int>(keyEvent->keycode) &&
                                static_cast<int>(cleanedState) == hotkey.modifiers) {

                                // Check event type
                                if ((hotkey.eventType == HotkeyEventType::Down && !isDown) ||
                                    (hotkey.eventType == HotkeyEventType::Up && isDown)) {
                                    continue;
                                }

                                // Context checks
                                if (!hotkey.contexts.empty()) {
                                    if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                                                   [](auto& ctx) { return ctx(); })) {
                                        continue;
                                    }
                                }

                                // Push hotkey event to EventQueue
                                if (eventQueue_) {
                                    info("X11 hotkey triggered: {} key: {} modifiers: {}",
                                         hotkey.alias, hotkey.key, hotkey.modifiers);
                                    triggeredHotkeyIds.push_back(id);
                                }
                            }
                        }
                    }

                    // Push hotkey events to EventQueue
                    if (eventQueue_) {
                        for (int hotkeyId : triggeredHotkeyIds) {
                            try {
                                eventQueue_->push(compiler::Event(compiler::EventType::HOTKEY_TRIGGER, hotkeyId));
                            } catch (const std::exception& e) {
                                error("Error pushing hotkey event to queue: {}", e.what());
                            }
                        }
                    }

                } catch (const std::exception& e) {
                    error("Error processing X11 event: {}", e.what());
                } catch (...) {
                    error("Unknown error processing X11 event");
                }
            }
        }
    } catch (const std::exception& e) {
        error("Fatal error in X11 hotkey monitoring: {}", e.what());
        running = false;
    } catch (...) {
        error("Unknown fatal error in X11 hotkey monitoring");
        running = false;
    }

    info("X11 hotkey monitoring loop stopped");
}

bool X11HotkeyMonitor::IsModifierKeySym(KeySym ks) {
    return ks == XK_Shift_L || ks == XK_Shift_R ||
           ks == XK_Control_L || ks == XK_Control_R ||
           ks == XK_Alt_L || ks == XK_Alt_R ||
           ks == XK_Meta_L || ks == XK_Meta_R ||
           ks == XK_Super_L || ks == XK_Super_R ||
           ks == XK_Hyper_L || ks == XK_Hyper_R ||
           ks == XK_Caps_Lock || ks == XK_Shift_Lock ||
           ks == XK_Num_Lock || ks == XK_Scroll_Lock;
}

unsigned int X11HotkeyMonitor::CleanMask(unsigned int mask) {
    return mask & RELEVANT_MODIFIERS;
}

} // namespace havel

#endif // __linux__
