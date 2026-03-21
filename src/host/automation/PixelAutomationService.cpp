/*
 * PixelAutomationService.cpp
 *
 * Pixel and image automation service implementation.
 */
#include "PixelAutomationService.hpp"
#include "core/automation/PixelAutomation.hpp"
#include "gui/ScreenshotManager.hpp"
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>

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

ImageMatch PixelAutomationService::findImage(const std::string& imagePath, const Region& region) {
    if (!m_automation) return ImageMatch();
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    auto match = m_automation->findImage(imagePath, screenRegion);
    
    return ImageMatch(match.x, match.y, match.w, match.h, match.confidence);
}

std::vector<ImageMatch> PixelAutomationService::findAllImages(const std::string& imagePath, const Region& region) {
    if (!m_automation) return {};
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    auto matches = m_automation->findAllImage(imagePath, screenRegion);
    
    std::vector<ImageMatch> results;
    for (const auto& m : matches) {
        results.emplace_back(m.x, m.y, m.w, m.h, m.confidence);
    }
    return results;
}

bool PixelAutomationService::existsImage(const std::string& imagePath, const Region& region) {
    if (!m_automation) return false;
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->existsImage(imagePath, screenRegion);
}

int PixelAutomationService::countImage(const std::string& imagePath, const Region& region) {
    if (!m_automation) return 0;
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->countImage(imagePath, screenRegion);
}

ImageMatch PixelAutomationService::waitImage(const std::string& imagePath, const Region& region, int timeout) {
    if (!m_automation) return ImageMatch();
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    auto match = m_automation->waitImage(imagePath, screenRegion, timeout);
    
    return ImageMatch(match.x, match.y, match.w, match.h, match.confidence);
}

std::string PixelAutomationService::readText(const Region& region) {
    if (!m_automation) return "";
    
    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->readText(screenRegion);
}

std::string PixelAutomationService::readText(const Region& region, const std::string& ocrEngine) {
    // For now, ignore ocrEngine parameter and use default
    (void)ocrEngine;
    return readText(region);
}

bool PixelAutomationService::captureScreen(const std::string& filePath) {
    // ScreenshotManager requires IO pointer - skip for now
    (void)filePath;
    return false;
}

bool PixelAutomationService::captureRegion(const Region& region, const std::string& filePath) {
    // ScreenshotManager requires IO pointer - skip for now
    (void)region;
    (void)filePath;
    return false;
}

} // namespace havel::host
