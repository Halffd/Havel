/*
 * ScreenshotService.hpp
 *
 * Screenshot service - uses Qt internally, returns VM-safe types.
 * 
 * This service demonstrates the correct pattern:
 * - Uses Qt (QScreen, QImage) internally
 * - HostBridge translates to VMImage (VM-safe type)
 * - No Qt types leak to VM
 */
#pragma once

#include <memory>
#include <vector>
#include <cstdint>

// Forward declare Qt types - service implementation includes them
class QScreen;
class QImage;

namespace havel::compiler { struct VMImage; }

namespace havel::host {

/**
 * ScreenshotService - Screenshot capture using Qt
 * 
 * Uses Qt internally for platform abstraction.
 * Returns raw image data that HostBridge translates to VMImage.
 */
class ScreenshotService {
public:
    ScreenshotService();
    ~ScreenshotService();

    // =========================================================================
    // Screenshot capture - returns raw image data
    // =========================================================================

    /// Capture full desktop (all monitors)
    /// @return Raw RGBA image data (width * height * 4 bytes)
    std::vector<uint8_t> captureFullDesktop();

    /// Capture specific monitor
    /// @param monitorIndex Monitor index (0 = primary)
    /// @return Raw RGBA image data
    std::vector<uint8_t> captureMonitor(int monitorIndex);

    /// Capture current active window
    /// @return Raw RGBA image data
    std::vector<uint8_t> captureActiveWindow();

    /// Capture screen region
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param width Width in pixels
    /// @param height Height in pixels
    /// @return Raw RGBA image data
    std::vector<uint8_t> captureRegion(int x, int y, int width, int height);

    // =========================================================================
    // Monitor information
    // =========================================================================

    /// Get number of monitors
    int getMonitorCount() const;

    /// Get monitor geometry
    /// @param monitorIndex Monitor index (0 = primary)
    /// @return {x, y, width, height} or empty if invalid
    std::vector<int> getMonitorGeometry(int monitorIndex) const;

private:
    // Helper: capture from QScreen
    std::vector<uint8_t> captureFromScreen(QScreen* screen);
    
    // Helper: convert QImage to RGBA vector
    std::vector<uint8_t> imageToRGBA(const QImage& image);
};

} // namespace havel::host
