/*
 * PixelAutomationService.hpp
 *
 * Pixel and image automation service.
 * Provides screen capture, pixel operations, image search, and OCR.
 *
 * Uses Qt internally for screenshot capture, but doesn't leak types to VM.
 * The Qt-free value types and the IPixelAutomation interface live in
 * IPixelAutomation.hpp; this header only adds the concrete implementation.
 * Core-facing callers must use IPixelAutomation, not this class.
 */
#pragma once

#include "IPixelAutomation.hpp"

#include <memory>
#include <string>
#include <vector>

namespace havel { class PixelAutomation; class ScreenshotManager; }

namespace havel::host {

/**
 * PixelAutomationService - Pixel and image automation
 *
 * Uses Qt and OpenCV internally for screen capture and image processing.
 * Returns plain C++ types that HostBridge translates to VM types.
 */
class HAVEL_EXPORT PixelAutomationService : public IPixelAutomation {
public:
    PixelAutomationService();
    ~PixelAutomationService() override;

    // =========================================================================
    // Pixel operations
    // =========================================================================

    /// Get pixel color at position
    Color getPixel(int x, int y) override;

    /// Check if pixel matches color with tolerance
    bool pixelMatch(int x, int y, const Color& expectedColor, int tolerance = 0) override;
    bool pixelMatch(int x, int y, const std::string& hexColor, int tolerance = 0) override;

    /// Wait for pixel to match color
    bool waitPixel(int x, int y, const Color& expectedColor, int tolerance = 0, int timeout = 5000) override;
    bool waitPixel(int x, int y, const std::string& hexColor, int tolerance = 0, int timeout = 5000) override;

    // =========================================================================
    // Image search
    // =========================================================================

    /// Find image on screen
    ImageMatch findImage(const std::string& imagePath, const Region& region = Region(), float threshold = 0.9f) override;

    /// Find all occurrences of image on screen
    std::vector<ImageMatch> findAllImages(const std::string& imagePath, const Region& region = Region(), float threshold = 0.9f) override;

    /// Check if image exists on screen
    bool existsImage(const std::string& imagePath, const Region& region = Region(), float threshold = 0.9f) override;

    /// Count occurrences of image on screen
    int countImage(const std::string& imagePath, const Region& region = Region(), float threshold = 0.9f) override;

    /// Wait for image to appear on screen
    ImageMatch waitImage(const std::string& imagePath, const Region& region = Region(), int timeout = 5000, float threshold = 0.9f) override;

    // =========================================================================
    // OCR (Optical Character Recognition)
    // =========================================================================

    /// Read text from screen region
    std::string readText(const Region& region = Region()) override;

    /// Read text from screen region with OCR engine
    std::string readText(const Region& region, const std::string& ocrEngine) override;

    // =========================================================================
    // Screenshot operations
    // =========================================================================

    /// Capture full screen and save to file
    bool captureScreen(const std::string& filePath) override;

    /// Capture region and save to file
    bool captureRegion(const Region& region, const std::string& filePath) override;

private:
    std::shared_ptr<PixelAutomation> m_automation;
    std::shared_ptr<ScreenshotManager> m_screenshotManager;
};

} // namespace havel::host
