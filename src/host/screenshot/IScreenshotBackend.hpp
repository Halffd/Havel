#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace havel::host {

// Style options for screenshots
struct ScreenshotStyle {
    // Dim/brightness factor (0.0 to 1.0, default 0.8 = 20% dimmer)
    double dimFactor = 0.8;
    
    // Cursor crosshair
    bool showCursorCross = true;
    uint32_t cursorCrossColor = 0xFF0000FF;  // Blue
    int cursorCrossSize = 24;
    int cursorCrossWidth = 3;
    
    // Selection border (for region captures)
    int selectionBorderWidth = 8;             // 8px red border
    uint32_t selectionBorderColor = 0xFFFF0000; // Red
    double selectionOpacity = 0.3;            // 30% opacity fill
    
    // Legacy styling (mostly disabled by default)
    bool captureWindowFrame = false;
    bool captureShadow = false;
    int borderWidth = 0;
    uint32_t borderColor = 0xFF000000;
    int cornerRadius = 0;
    int shadowOffset = 0;
    int shadowBlur = 0;
    uint32_t shadowColor = 0x80000000;
    uint32_t backgroundColor = 0xFFFFFFFF;
    bool includeCursor = false;
};

// Result of a screenshot capture including dimensions
struct ScreenshotResult {
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;
    
    ScreenshotResult() = default;
    ScreenshotResult(std::vector<unsigned char>&& d, int w, int h) 
        : data(std::move(d)), width(w), height(h) {}
    ScreenshotResult(const std::vector<unsigned char>& d, int w, int h) 
        : data(d), width(w), height(h) {}
    
    explicit operator bool() const { return !data.empty() && width > 0 && height > 0; }
};

class IScreenshotBackend {
public:
    virtual ~IScreenshotBackend() = default;

    virtual ScreenshotResult captureFullDesktop(const ScreenshotStyle& style = {}) = 0;
    virtual ScreenshotResult captureMonitor(int index, const ScreenshotStyle& style = {}) = 0;
    virtual ScreenshotResult captureActiveWindow(const ScreenshotStyle& style = {}) = 0;
    virtual ScreenshotResult captureRegion(int x, int y, int width, int height, const ScreenshotStyle& style = {}) = 0;
    virtual int getMonitorCount() const = 0;
    virtual std::vector<int> getMonitorGeometry(int index) const = 0;
};

} // namespace havel::host
