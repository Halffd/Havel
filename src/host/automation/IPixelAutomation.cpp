// IPixelAutomation.cpp — Qt-free half of the pixel service surface.
//
// The value types (Color, Region, ImageMatch) are used by the pixel module in
// havel_modules, which must not pull havel_gui. Their out-of-line methods
// (fromHex/toHex/near, fullScreen) therefore live here, in the core archive,
// and call only Qt-free code: havel::Color in core/automation/PixelAutomation
// and the screen provider slot in core/automation/ScreenCapture.

#include "IPixelAutomation.hpp"

#include "core/automation/PixelAutomation.hpp"
#include "core/automation/ScreenCapture.hpp"

namespace havel::host {

// ============================================================================
// Color implementation
// ============================================================================

Color Color::fromHex(const std::string& hex) {
    havel::Color c = havel::Color::fromHex(hex);
    return Color(c.r, c.g, c.b, c.a);
}

std::string Color::toHex() const {
    havel::Color c(r, g, b, a);
    return c.toHex();
}

bool Color::near(const Color& other, int tolerance) const {
    havel::Color c1(r, g, b, a);
    havel::Color c2(other.r, other.g, other.b, other.a);
    return c1.near(c2, tolerance);
}

// ============================================================================
// Region implementation
// ============================================================================

Region Region::fullScreen() {
    // Ask the platform through the same slot pixel automation uses. No provider
    // registered (Qt-free host) means the same fallback the Qt version used
    // when no screen was attached.
    if (const havel::ScreenProvider* provider = havel::screenProvider()) {
        havel::ScreenBounds bounds;
        if (provider->bounds(bounds)) {
            return Region(bounds.x, bounds.y, bounds.w, bounds.h);
        }
    }
    return Region(0, 0, 1920, 1080);
}

} // namespace havel::host
