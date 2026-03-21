/*
 * PixelAutomationService.hpp
 *
 * Pixel and image automation service.
 * Provides screen capture, pixel operations, image search, and OCR.
 * 
 * Uses Qt and OpenCV internally, but doesn't leak types to VM.
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace havel { class PixelAutomation; class ScreenshotManager; }

namespace havel::host {

/**
 * Color - RGB(A) color representation
 */
struct Color {
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
struct Region {
    int x = 0, y = 0, w = 0, h = 0;
    
    Region() = default;
    Region(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}
    
    // Full screen region
    static Region fullScreen();
};

/**
 * Image match result
 */
struct ImageMatch {
    bool found = false;
    int x = 0, y = 0, w = 0, h = 0;
    float confidence = 0.0f;
    
    ImageMatch() = default;
    ImageMatch(int x, int y, int w, int h, float conf = 1.0f)
        : found(true), x(x), y(y), w(w), h(h), confidence(conf) {}
    
    // Center point of match
    int centerX() const { return x + w / 2; }
    int centerY() const { return y + h / 2; }
};

/**
 * PixelAutomationService - Pixel and image automation
 * 
 * Uses Qt and OpenCV internally for screen capture and image processing.
 * Returns plain C++ types that HostBridge translates to VM types.
 */
class PixelAutomationService {
public:
    PixelAutomationService();
    ~PixelAutomationService();

    // =========================================================================
    // Pixel operations
    // =========================================================================

    /// Get pixel color at position
    Color getPixel(int x, int y);

    /// Check if pixel matches color with tolerance
    bool pixelMatch(int x, int y, const Color& expectedColor, int tolerance = 0);
    bool pixelMatch(int x, int y, const std::string& hexColor, int tolerance = 0);

    /// Wait for pixel to match color
    bool waitPixel(int x, int y, const Color& expectedColor, int tolerance = 0, int timeout = 5000);
    bool waitPixel(int x, int y, const std::string& hexColor, int tolerance = 0, int timeout = 5000);

    // =========================================================================
    // Image search
    // =========================================================================

    /// Find image on screen
    ImageMatch findImage(const std::string& imagePath, const Region& region = Region());

    /// Find all occurrences of image on screen
    std::vector<ImageMatch> findAllImages(const std::string& imagePath, const Region& region = Region());

    /// Check if image exists on screen
    bool existsImage(const std::string& imagePath, const Region& region = Region());

    /// Count occurrences of image on screen
    int countImage(const std::string& imagePath, const Region& region = Region());

    /// Wait for image to appear on screen
    ImageMatch waitImage(const std::string& imagePath, const Region& region = Region(), int timeout = 5000);

    // =========================================================================
    // OCR (Optical Character Recognition)
    // =========================================================================

    /// Read text from screen region
    std::string readText(const Region& region = Region());

    /// Read text from screen region with OCR engine
    std::string readText(const Region& region, const std::string& ocrEngine);

    // =========================================================================
    // Screenshot operations
    // =========================================================================

    /// Capture full screen and save to file
    bool captureScreen(const std::string& filePath);

    /// Capture region and save to file
    bool captureRegion(const Region& region, const std::string& filePath);

private:
    std::shared_ptr<PixelAutomation> m_automation;
    std::shared_ptr<ScreenshotManager> m_screenshotManager;
};

} // namespace havel::host
