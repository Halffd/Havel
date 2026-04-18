#include "InputBackend.hpp"
#include "utils/Logger.hpp"

#ifdef __linux__
#include <cstdlib>
#include <cstring>
#endif

namespace havel {

InputBackendType InputBackend::DetectBestBackend() {
#ifdef __linux__
    const char *waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char *xdgSessionType = std::getenv("XDG_SESSION_TYPE");

    if (waylandDisplay && strlen(waylandDisplay) > 0) {
        return InputBackendType::Wayland;
    }

    if (xdgSessionType && strcmp(xdgSessionType, "wayland") == 0) {
        return InputBackendType::Wayland;
    }

    const char *display = std::getenv("DISPLAY");
    if (display && strlen(display) > 0) {
        return InputBackendType::X11;
    }

    return InputBackendType::Evdev;

#elif defined(_WIN32)
    return InputBackendType::Windows;
#else
    return InputBackendType::Unknown;
#endif
}

std::unique_ptr<InputBackend> InputBackend::Create(InputBackendType type) {
    switch (type) {
        case InputBackendType::Evdev:
#ifdef __linux__
            return std::make_unique<EvdevAdapter>();
#else
            warn("EvdevAdapter not available on this platform");
            return nullptr;
#endif

        case InputBackendType::X11:
#ifdef __linux__
            return std::make_unique<X11Adapter>();
#else
            warn("X11Adapter not available on this platform");
            return nullptr;
#endif

        case InputBackendType::Wayland:
#ifdef __linux__
            return std::make_unique<WaylandAdapter>();
#else
            warn("WaylandAdapter not available on this platform");
            return nullptr;
#endif

        case InputBackendType::Windows:
#ifdef _WIN32
            return std::make_unique<WindowsAdapter>();
#else
            warn("WindowsAdapter not available on this platform");
            return nullptr;
#endif

        default:
            return nullptr;
    }
}

}
