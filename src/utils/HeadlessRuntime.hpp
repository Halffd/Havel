// HeadlessRuntime.hpp — single source of truth for "this process may not
// touch a display, X server, or input device".
//
// The flag is set by the engine next to the IO / DisplayManager headless
// gates (HavelEngine::initializeMinimal and Havel::initialize). Every code
// path that would otherwise connect to a display — Qt application creation,
// X11 FFI library loading, screenshot backends — consults these helpers so
// sandboxed children (hvtest, ctest) stay completely inert instead of
// injecting input into or capturing the live desktop session.
#pragma once

#include <cstdlib>
#include <string>

namespace havel {

// True when the process runs under the headless sandbox.
inline bool isHeadlessRuntime() {
    return ::getenv("HAVEL_HEADLESS") != nullptr;
}

// True when no display server is reachable (no X11 or Wayland session).
// Qt aborts the process from QApplication/QGuiApplication construction when
// this holds, so callers must check before creating one.
inline bool hasNoDisplayServer() {
    const char* x11 = ::getenv("DISPLAY");
    if (x11 != nullptr && !std::string(x11).empty()) return false;
    const char* wayland = ::getenv("WAYLAND_DISPLAY");
    return wayland == nullptr || std::string(wayland).empty();
}

// True when it is unsafe to construct a Qt application object.
inline bool qtRuntimeUnavailable() {
    return isHeadlessRuntime() || hasNoDisplayServer();
}

} // namespace havel
