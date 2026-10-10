// ScreenCapture.hpp — the platform half of pixel automation.
//
// PixelAutomation used to call QGuiApplication::primaryScreen() and
// grabWindow() directly, which is why src/core/automation/PixelAutomation.cpp
// carried Qt headers and symbols into libhavel_core.a. Pixel matching itself is
// pure OpenCV, so only the two "ask the platform for pixels" steps need Qt.
//
// The Qt implementation lives in src/extensions/qt/QtScreenCapture.cpp and
// registers itself here. The core calls through the slot and never includes a
// Qt header, the same way UIBackendFactory.hpp keeps the backends out of
// UIManager.
//
// No provider registered means no screen access, which is what a Qt-free host
// gets; the callers degrade exactly as they did when primaryScreen() returned
// null.

#pragma once

#include "havel-lang/common/Export.hpp"
#include <vector>

namespace havel {

// A screen, or a rectangle of one. w or h <= 0 means "the whole screen".
struct ScreenBounds {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct ScreenPixels {
    int w = 0;
    int h = 0;
    // Tightly packed, 4 bytes per pixel, BGRA order — the layout
    // cv::COLOR_BGRA2BGR expects. w * h * 4 bytes.
    std::vector<unsigned char> bgra;
};

struct ScreenProvider {
    // Geometry of the primary screen.
    bool (*bounds)(ScreenBounds &out);
    // Pixels of the given region; w or h <= 0 captures the whole screen.
    bool (*capture)(const ScreenBounds &region, ScreenPixels &out);
};

// Registration happens once, from the Qt side, before first use. Passing
// nullptr removes the provider.
HAVEL_EXPORT void setScreenProvider(const ScreenProvider *provider);
HAVEL_EXPORT const ScreenProvider *screenProvider();

namespace qt {

// Defined by the Qt toolkit plugin (src/extensions/qt/QtScreenCapture.cpp,
// compiled in from qt_toolkit_factories.cpp); havel_gui keeps a copy for
// legacy consumers. It has to be called explicitly: the implementation shares
// no symbol with anything else, so a static initialiser inside it would never
// be extracted from a static archive and the provider would silently never
// register. The toolkit's install_factories slot calls it, which is also
// early enough for the language host's pixel calls. Safe to call more than
// once. Declared here so the declaration stays Qt-free.
HAVEL_EXPORT void installQtScreenCapture();

} // namespace qt

} // namespace havel
