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

// True when a dlopen() target is an X11-family library that must not be loaded
// while sandboxed.
//
// This is the only thing standing between a self-hosted module and the live
// session. modules/app/keyboard.hv builds its own X11 stack entirely through
// FFI -- ffi.open("libX11.so.6") + ffi.sym(...) + XTestFakeKeyEvent -- with no
// reference to DisplayManager or IO, so neither of those headless gates can see
// it. XOpenDisplay(NULL) also falls back to ":0" when DISPLAY is empty, exactly
// like DisplayManager::Initialize() did, so an unsandboxed import of that module
// reaches the user's keyboard. Refusing the dlopen leaves every symbol null and
// the whole injection stack inert.
//
// Returns the base filename match; the caller supplies the path as passed to
// ffi.open, with or without a directory prefix.
inline bool isBlockedX11Library(const std::string &path) {
    if (!isHeadlessRuntime()) return false;
    const std::string base = path.substr(path.find_last_of('/') + 1);
    for (const char *pat : {"libX11", "libXtst", "libXrandr", "libXext",
                            "libXcomposite", "libXrender", "libXinerama",
                            "libXcursor", "libXfixes", "libXi"}) {
        if (base.rfind(pat, 0) == 0) return true;
    }
    return false;
}

} // namespace havel
