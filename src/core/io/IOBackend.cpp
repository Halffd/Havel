#include "IOBackend.hpp"
#include "EventListener.hpp"
#include "X11IOBackend.hpp"
#include "WaylandIOBackend.hpp"
#include "WindowsIOBackend.hpp"
#include "core/display/DisplayManager.hpp"
#include "core/wayland/WaylandProtocolClient.hpp"

namespace havel {

static bool isWaylandSession() {
  const char *session = getenv("XDG_SESSION_TYPE");
  if (session && strcmp(session, "wayland") == 0) return true;
  const char *wl = getenv("WAYLAND_DISPLAY");
  if (wl && strlen(wl) > 0) return true;
  return false;
}

std::unique_ptr<IOBackend> IOBackend::Create(EventListener *eventListener) {
#if defined(WINDOWS)
  return std::make_unique<WindowsIOBackend>();
#else
  if (isWaylandSession()) {
    auto &wpc = WaylandProtocolClient::instance();
    if (wpc.connect() && wpc.isConnected()) {
      auto backend = std::make_unique<WaylandIOBackend>(eventListener);
      if (backend->Initialize()) {
        return backend;
      }
    }
  }
  Display *display = DisplayManager::GetDisplay();
  if (display) {
    return std::make_unique<X11IOBackend>(display);
  }
  return std::make_unique<WaylandIOBackend>(eventListener);
#endif
}

} // namespace havel
