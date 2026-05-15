#include "InputBackend.hpp"
#include "EvdevAdapter.hpp"
#include "X11Adapter.hpp"
#include "WaylandAdapter.hpp"
#include "WindowsAdapter.hpp"
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
    return CreateEvdevAdapter();

  case InputBackendType::X11:
    return CreateX11Adapter();

  case InputBackendType::Wayland:
    return CreateWaylandAdapter();

  case InputBackendType::Windows:
    return CreateWindowsAdapter();

  default:
    return nullptr;
  }
}

}
