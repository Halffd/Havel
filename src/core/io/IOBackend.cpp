#include "IOBackend.hpp"
#include "EventListener.hpp"
#include "X11IOBackend.hpp"
#include "WaylandIOBackend.hpp"
#include "WindowsIOBackend.hpp"
#include "core/display/DisplayManager.hpp"

namespace havel {

std::unique_ptr<IOBackend> IOBackend::Create(EventListener *eventListener) {
    Display *display = DisplayManager::GetDisplay();
    if (display) {
        return std::make_unique<X11IOBackend>(display);
    }
#if defined(WINDOWS)
    return std::make_unique<WindowsIOBackend>();
#else
    return std::make_unique<WaylandIOBackend>(eventListener);
#endif
}

} // namespace havel
