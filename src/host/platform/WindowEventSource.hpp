// WindowEventSource — the X11 window event backend.
//
// Dependency direction (architecture doc, never reversed):
//   EventSource (core) <- WindowEventSource (host/platform) <- X11
//
// X11 events -> this source -> normalized WindowEvent -> publish.
// The language sees neither X11 nor any WM:
//
//   on window.created { ... }
//   on window.focused { ... }
//   on window.destroyed { ... }
//   on window.moved { ... }
//   on window.resized { ... }
//
// Events are queued in the EventRuntime (never touching the VM from the
// X11 thread). Subscribing to any window.* event starts the source; the
// last unsubscribe stops it.

#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "../../havel-lang/runtime/events/EventRuntime.hpp"
#include "../../havel-lang/runtime/events/EventSource.hpp"

namespace havel::compiler {

class WindowEventSource : public EventSource {
public:
    WindowEventSource() = default;
    ~WindowEventSource() override { stop(); }

    // EventSource interface
    void start(EventRuntime &runtime) override;
    void stop() override;
    bool isRunning() const { return running_.load(); }

private:
    void loop();

    EventRuntime *runtime_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
};

} // namespace havel::compiler
