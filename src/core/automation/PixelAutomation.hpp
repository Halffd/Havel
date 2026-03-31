#pragma once

#include <string>
#include <memory>
#include <vector>
#include <chrono>

#ifdef HAVE_QT_EXTENSION
#include <qt.hpp>
#endif

// Undefine conflicting macros before including OpenCV
#ifdef COUNT
#undef COUNT
#endif
#ifdef EPS
#undef EPS
#endif

// Include OpenCV after Qt to avoid conflicts
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

namespace havel {

// Forward declarations
class ScreenshotManager;
class OCR;

/**
 * Color object representing RGB(A) color
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
    
    // Distance between colors (0-765)
    int distance(const Color& other) const;
};

/**
 * Screen region for bounded operations
 */
struct ScreenRegion {
    int x = 0, y = 0, w = 0, h = 0;
    
    ScreenRegion() = default;
    ScreenRegion(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}
    
    QRect toQRect() const;
    cv::Rect toCvRect() const;
    
    // Full screen region
    static ScreenRegion fullScreen();
};

/**
 * Image search result
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
    
    // Click on this match
    void click(const std::string& button = "left") const;
    
    // Move mouse to this match
    void moveTo() const;
};

/**
 * Pixel match result
 */
struct PixelMatch {
    bool matched = false;
    Color color;
    int x = 0, y = 0;
    
    PixelMatch() = default;
    PixelMatch(bool m, const Color& c, int x, int y)
        : matched(m), color(c), x(x), y(y) {}
};

/**
 * Template match result
 */
struct TemplateMatchResult {
    cv::Point location;
    float confidence;
};

/**
 * Screenshot cache for performance
 */
class ScreenshotCache {
public:
    ScreenshotCache();
    ~ScreenshotCache();
    
    // Capture and cache full screen
    void capture();
    
    // Capture and cache region
    void captureRegion(const ScreenRegion& region);
    
    // Get cached screenshot
    const cv::Mat& get() const { return cachedScreenshot; }
    
    // Check if cache is valid
    bool isValid() const { return !cachedScreenshot.empty(); }
    
    // Clear cache
    void clear();
    
    // Auto-expire after milliseconds
    void setExpiry(int ms);
    
    // Check if cache has expired
    bool isExpired() const;
    
private:
    cv::Mat cachedScreenshot;
    std::chrono::steady_clock::time_point captureTime;
    int expiryMs = 0;
};

/**
 * Pixel and Image Automation Module
 * 
 * Core primitives:
 * - pixel(x, y) - Get pixel color
 * - pixelMatch(x, y, color, tolerance) - Check if pixel matches color
 * - waitPixel(x, y, color, tolerance, timeout) - Wait for pixel color
 * - findImage(path, region) - Find image on screen
 * - waitImage(path, region, timeout) - Wait for image
 * - existsImage(path, region) - Check if image exists
 * - countImage(path, region) - Count image occurrences
 * - readText(region) - OCR text from region
 */
class PixelAutomation {
public:
    PixelAutomation();
    ~PixelAutomation();
    
    // === Pixel Operations ===
    
    /**
     * Get pixel color at position
     * @param x X coordinate
     * @param y Y coordinate
     * @return Color object with r, g, b, a values
     */
    Color getPixel(int x, int y);
    
    /**
     * Check if pixel matches color with tolerance
     * @param x X coordinate
     * @param y Y coordinate
     * @param expectedColor Expected color (hex string or Color)
     * @param tolerance Color tolerance (0-255, per channel)
     * @return true if pixel matches within tolerance
     */
    bool pixelMatch(int x, int y, const Color& expectedColor, int tolerance = 0);
    bool pixelMatch(int x, int y, const std::string& hexColor, int tolerance = 0);
    
    /**
     * Wait for pixel to match color
     * @param x X coordinate
     * @param y Y coordinate
     * @param expectedColor Expected color
     * @param tolerance Color tolerance
     * @param timeout Timeout in milliseconds
     * @return true if pixel matched within timeout
     */
    bool waitPixel(int x, int y, const Color& expectedColor, int tolerance = 0, int timeout = 5000);
    bool waitPixel(int x, int y, const std::string& hexColor, int tolerance = 0, int timeout = 5000);
    
    // === Image Search ===
    
    /**
     * Find image on screen
     * @param imagePath Path to image file
     * @param region Search region (optional, searches full screen if not specified)
     * @param threshold Match threshold (0.0-1.0, default 0.9)
     * @return ImageMatch object (found=false if not found)
     */
    ImageMatch findImage(const std::string& imagePath, 
                         const ScreenRegion& region = ScreenRegion(),
                         float threshold = 0.9f);
    
    /**
     * Wait for image to appear on screen
     * @param imagePath Path to image file
     * @param region Search region (optional)
     * @param timeout Timeout in milliseconds
     * @param threshold Match threshold
     * @return ImageMatch object (found=false if timeout)
     */
    ImageMatch waitImage(const std::string& imagePath,
                         const ScreenRegion& region = ScreenRegion(),
                         int timeout = 5000,
                         float threshold = 0.9f);
    
    /**
     * Check if image exists on screen
     * @param imagePath Path to image file
     * @param region Search region (optional)
     * @param threshold Match threshold
     * @return true if image found
     */
    bool existsImage(const std::string& imagePath,
                     const ScreenRegion& region = ScreenRegion(),
                     float threshold = 0.9f);
    
    /**
     * Count occurrences of image on screen
     * @param imagePath Path to image file
     * @param region Search region (optional)
     * @param threshold Match threshold
     * @return Number of matches found
     */
    int countImage(const std::string& imagePath,
                   const ScreenRegion& region = ScreenRegion(),
                   float threshold = 0.9f);
    
    /**
     * Find all occurrences of image on screen
     * @param imagePath Path to image file
     * @param region Search region (optional)
     * @param threshold Match threshold
     * @return Vector of ImageMatch objects
     */
    std::vector<ImageMatch> findAllImage(const std::string& imagePath,
                                         const ScreenRegion& region = ScreenRegion(),
                                         float threshold = 0.9f);
    
    // === OCR ===
    
    /**
     * Read text from screen region using OCR
     * @param region Region to read (full screen if not specified)
     * @return Recognized text
     */
    std::string readText(const ScreenRegion& region = ScreenRegion());
    
    /**
     * Read text with OCR configuration
     * @param region Region to read
     * @param language OCR language (e.g., "eng", "deu", "fra")
     * @param whitelist Allowed characters (optional)
     * @return Recognized text
     */
    std::string readText(const ScreenRegion& region, 
                         const std::string& language,
                         const std::string& whitelist = "");
    
    // === Screenshot Cache ===
    
    /**
     * Enable screenshot caching for performance
     * @param enabled Enable/disable caching
     * @param expiryMs Cache expiry time in milliseconds (0 = no expiry)
     */
    void setCacheEnabled(bool enabled, int expiryMs = 100);
    
    /**
     * Manually capture screenshot to cache
     */
    void captureScreen();
    
    /**
     * Clear screenshot cache
     */
    void clearCache();
    
    // === Region Helpers ===
    
    /**
     * Create a screen region
     * @param x X coordinate
     * @param y Y coordinate
     * @param w Width
     * @param h Height
     * @return ScreenRegion object
     */
    static ScreenRegion region(int x, int y, int w, int h);
    
    /**
     * Get full screen region
     * @return ScreenRegion covering entire screen
     */
    static ScreenRegion fullScreen();
    
private:
    std::unique_ptr<ScreenshotCache> cache;
    bool cacheEnabled = false;
    
    // Capture screen (uses cache if enabled)
    cv::Mat captureScreenInternal();
    
    // Template match with multiple results
    std::vector<TemplateMatchResult> matchTemplate(const cv::Mat& screen, 
                                                    const cv::Mat& templateImg,
                                                    float threshold);
};

} // namespace havel
