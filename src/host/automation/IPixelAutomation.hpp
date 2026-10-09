// IPixelAutomation.hpp — the Qt-free half of pixel automation.
//
// PixelAutomationService (the Qt-using wrapper) lives in havel_gui, but the
// core and the pixel module used to name the concrete class directly:
// HostModules.cpp constructed it, PixelModule.cpp called its methods. That one
// reference was enough to pull havel_gui — and therefore Qt — into any binary
// that linked libhavel_core.a, no matter how many Qt symbols the guard found,
// because the referenced symbol ("havel::host::PixelAutomationService::...")
// is not Qt-mangled.
//
// So the concrete type moves behind a factory slot (PixelAutomationFactory.hpp),
// and the core-facing callers go through this interface instead. The value
// types stay here too, so an includer never needs the concrete header.
//
// Same shape as the other extraction seams: UIBackendFactory.hpp (UI backends)
// and core/automation/ScreenCapture.hpp (screen pixels).

#pragma once

#include "havel-lang/common/Export.hpp"

#include <memory>
#include <string>
#include <vector>

namespace havel::host {

/**
 * Color - RGB(A) color representation
 */
struct HAVEL_EXPORT Color {
    int r = 0, g = 0, b = 0, a = 255;

    Color() = default;
    Color(int r, int g, int b, int a = 255) : r(r), g(g), b(b), a(a) {}

    // Parse from hex string (#RRGGBB or #RRGGBBAA)
    static Color fromHex(const std::string& hex);

    // Convert to hex string
    std::string toHex() const;

    // Check if color is near another color with tolerance
    bool near(const Color& other, int tolerance = 0) const;
};

/**
 * Screen region for bounded operations
 */
struct HAVEL_EXPORT Region {
    int x = 0, y = 0, w = 0, h = 0;

    Region() = default;
    Region(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}

    // Full screen region
    static Region fullScreen();
};

/**
 * Image match result
 */
struct HAVEL_EXPORT ImageMatch {
    bool found = false;
    int x = 0, y = 0, w = 0, h = 0;
    float confidence = 0.0f;

    ImageMatch() = default;
    ImageMatch(bool found, int x, int y, int w, int h, float conf = 1.0f)
    : found(found), x(x), y(y), w(w), h(h), confidence(conf) {}

    // Center point of match
    int centerX() const { return x + w / 2; }
    int centerY() const { return y + h / 2; }
};

/**
 * IPixelAutomation - the operations the pixel module needs.
 *
 * Implemented by PixelAutomationService in havel_gui. A host with no Qt
 * registers no factory, so no service is available and the pixel module
 * degrades the same way it does when the screen provider is absent.
 */
class HAVEL_EXPORT IPixelAutomation {
public:
    virtual ~IPixelAutomation() = default;

    // Pixel operations
    virtual Color getPixel(int x, int y) = 0;
    virtual bool pixelMatch(int x, int y, const Color& expectedColor, int tolerance) = 0;
    virtual bool pixelMatch(int x, int y, const std::string& hexColor, int tolerance) = 0;
    virtual bool waitPixel(int x, int y, const Color& expectedColor, int tolerance, int timeout) = 0;
    virtual bool waitPixel(int x, int y, const std::string& hexColor, int tolerance, int timeout) = 0;

    // Image search
    virtual ImageMatch findImage(const std::string& imagePath, const Region& region, float threshold) = 0;
    virtual std::vector<ImageMatch> findAllImages(const std::string& imagePath, const Region& region, float threshold) = 0;
    virtual bool existsImage(const std::string& imagePath, const Region& region, float threshold) = 0;
    virtual int countImage(const std::string& imagePath, const Region& region, float threshold) = 0;
    virtual ImageMatch waitImage(const std::string& imagePath, const Region& region, int timeout, float threshold) = 0;

    // OCR
    virtual std::string readText(const Region& region) = 0;
    virtual std::string readText(const Region& region, const std::string& ocrEngine) = 0;

    // Screenshot
    virtual bool captureScreen(const std::string& filePath) = 0;
    virtual bool captureRegion(const Region& region, const std::string& filePath) = 0;
};

} // namespace havel::host
