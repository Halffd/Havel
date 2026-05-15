/*
 * PixelAutomationService.cpp
 *
 * Pixel and image automation service implementation.
 */
#include "PixelAutomationService.hpp"
#include "core/automation/PixelAutomation.hpp"
#include "extensions/gui/screenshot_manager/ScreenshotManager.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include <cstring>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QImage>

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
    // Get actual screen size from Qt
    auto* app = QApplication::instance();
    if (app) {
        auto* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->geometry();
            return Region(geo.x(), geo.y(), geo.width(), geo.height());
        }
    }
    return Region(0, 0, 1920, 1080);
}

// ============================================================================
// PixelAutomationService implementation
// ============================================================================

PixelAutomationService::PixelAutomationService() {
    m_automation = std::make_shared<PixelAutomation>();
}

PixelAutomationService::~PixelAutomationService() {
}

Color PixelAutomationService::getPixel(int x, int y) {
    if (!m_automation) return Color();
    
    auto color = m_automation->getPixel(x, y);
    return Color(color.r, color.g, color.b, color.a);
}

bool PixelAutomationService::pixelMatch(int x, int y, const Color& expectedColor, int tolerance) {
    if (!m_automation) return false;
    
    havel::Color c(expectedColor.r, expectedColor.g, expectedColor.b, expectedColor.a);
    return m_automation->pixelMatch(x, y, c, tolerance);
}

bool PixelAutomationService::pixelMatch(int x, int y, const std::string& hexColor, int tolerance) {
    return pixelMatch(x, y, Color::fromHex(hexColor), tolerance);
}

bool PixelAutomationService::waitPixel(int x, int y, const Color& expectedColor, int tolerance, int timeout) {
    if (!m_automation) return false;
    
    havel::Color c(expectedColor.r, expectedColor.g, expectedColor.b, expectedColor.a);
    return m_automation->waitPixel(x, y, c, tolerance, timeout);
}

bool PixelAutomationService::waitPixel(int x, int y, const std::string& hexColor, int tolerance, int timeout) {
    return waitPixel(x, y, Color::fromHex(hexColor), tolerance, timeout);
}

ImageMatch PixelAutomationService::findImage(const std::string& imagePath, const Region& region, float threshold) {
    if (!m_automation) return ImageMatch();

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    auto match = m_automation->findImage(imagePath, screenRegion, threshold);

    ImageMatch result;
    result.found = match.found;
    result.x = match.x;
    result.y = match.y;
    result.w = match.w;
    result.h = match.h;
    result.confidence = match.confidence;
    return result;
}

std::vector<ImageMatch> PixelAutomationService::findAllImages(const std::string& imagePath, const Region& region, float threshold) {
    if (!m_automation) return {};

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    auto matches = m_automation->findAllImage(imagePath, screenRegion, threshold);

    std::vector<ImageMatch> results;
    for (const auto& m : matches) {
        ImageMatch result;
        result.found = m.found;
        result.x = m.x;
        result.y = m.y;
        result.w = m.w;
        result.h = m.h;
        result.confidence = m.confidence;
        results.push_back(result);
    }
    return results;
}

bool PixelAutomationService::existsImage(const std::string& imagePath, const Region& region, float threshold) {
    if (!m_automation) return false;

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->existsImage(imagePath, screenRegion, threshold);
}

int PixelAutomationService::countImage(const std::string& imagePath, const Region& region, float threshold) {
    if (!m_automation) return 0;

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->countImage(imagePath, screenRegion, threshold);
}

ImageMatch PixelAutomationService::waitImage(const std::string& imagePath, const Region& region, int timeout, float threshold) {
    if (!m_automation) return ImageMatch();

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    auto match = m_automation->waitImage(imagePath, screenRegion, timeout, threshold);

    ImageMatch result;
    result.found = match.found;
    result.x = match.x;
    result.y = match.y;
    result.w = match.w;
    result.h = match.h;
    result.confidence = match.confidence;
    return result;
}

std::string PixelAutomationService::readText(const Region& region) {
    if (!m_automation) return "";
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->readText(screenRegion);
}

std::string PixelAutomationService::readText(const Region& region, const std::string& ocrEngine) {
  if (!m_automation) return "";

  havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
  return m_automation->readText(screenRegion, ocrEngine);
}

bool PixelAutomationService::captureScreen(const std::string& filePath) {
    auto& screenshot = ScreenshotService::getInstance();
    auto rgba = screenshot.captureFullDesktop();
    if (rgba.empty()) return false;

    int w = 1920, h = 1080;
    auto* app = QApplication::instance();
    if (app) {
        auto* screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->geometry();
            w = geo.width();
            h = geo.height();
        }
    }

    if (rgba.size() < static_cast<size_t>(w * h * 4)) return false;

    QImage img(reinterpret_cast<const uchar*>(rgba.data()), w, h, w * 4,
               QImage::Format_RGBA8888);
    return img.save(QString::fromStdString(filePath), "PNG");
}

bool PixelAutomationService::captureRegion(const Region& region, const std::string& filePath) {
    auto& screenshot = ScreenshotService::getInstance();
    auto rgba = screenshot.captureRegion(region.x, region.y, region.w, region.h);
    if (rgba.empty()) return false;

    if (rgba.size() < static_cast<size_t>(region.w * region.h * 4)) return false;

    QImage img(reinterpret_cast<const uchar*>(rgba.data()), region.w, region.h,
               region.w * 4, QImage::Format_RGBA8888);
    return img.save(QString::fromStdString(filePath), "PNG");
}

} // namespace havel::host
