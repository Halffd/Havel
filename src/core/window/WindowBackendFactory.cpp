#include "WindowBackendFactory.hpp"
#include "backends/X11Backend.hpp"
#include "backends/WaylandBackend.hpp"
#include "backends/WindowsBackend.hpp"
#include "backends/MacOSBackend.hpp"
#include "WindowManagerDetector.hpp"

namespace havel {

std::string WindowBackendFactory::GetDetectedBackendName() {
  if (WindowManagerDetector::IsWayland()) return "wayland";
  if (WindowManagerDetector::IsX11()) return "x11";
#ifdef _WIN32
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "x11";
#endif
}

std::unique_ptr<WindowBackend> WindowBackendFactory::Create() {
  return Create(GetDetectedBackendName());
}

std::unique_ptr<WindowBackend> WindowBackendFactory::Create(const std::string &backendName) {
  if (backendName == "x11") {
    return std::make_unique<X11Backend>();
  } else if (backendName == "wayland") {
    return std::make_unique<WaylandBackend>();
  } else if (backendName == "windows") {
    return std::make_unique<WindowsBackend>();
  } else if (backendName == "macos") {
    return std::make_unique<MacOSBackend>();
  }
  return std::make_unique<X11Backend>();
}

} // namespace havel
