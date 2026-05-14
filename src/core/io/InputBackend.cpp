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
    warn("EvdevAdapter not available (adapter excluded from build)");
    return nullptr;

  case InputBackendType::X11:
    warn("X11Adapter not available (adapter excluded from build)");
    return nullptr;

  case InputBackendType::Wayland:
    warn("WaylandAdapter not available (adapter excluded from build)");
    return nullptr;

  case InputBackendType::Windows:
    warn("WindowsAdapter not available (adapter excluded from build)");
    return nullptr;

  default:
    return nullptr;
  }
}

}
