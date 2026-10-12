/*
 * PixelAutomationService.cpp
 *
 * Pixel and image automation service implementation.
 */
#ifdef HAVE_QT_EXTENSION

#include "PixelAutomationService.hpp"
#include "core/automation/PixelAutomation.hpp"
#include "extensions/gui/screenshot_manager/ScreenshotManager.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "utils/HeadlessRuntime.hpp"
#include <cstring>
#include <QApplication>
#include <QImage>
#include <QString>

namespace havel::host {

// ============================================================================
// Lazy Qt app init - mirrors UIService::ensureApp
// ============================================================================

static void ensureApp() {
    // Qt aborts the process from the QApplication constructor when no display
    // server is reachable; sandboxed runs (hvtest/ctest) never construct one.
    if (havel::qtRuntimeUnavailable()) return;
    if (!QApplication::instance()) {
        static int argc = 1;
        static char *argv[] = {const_cast<char *>("havel"), nullptr};
        new QApplication(argc, argv);
        QApplication::setQuitOnLastWindowClosed(false);
    }
}

// Color and Region value-type methods are Qt-free and live in IPixelAutomation.cpp
// in the core archive, so the pixel module can use them without linking havel_gui.

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
    ensureApp();
    auto color = m_automation->getPixel(x, y);
    return Color(color.r, color.g, color.b, color.a);
}

bool PixelAutomationService::pixelMatch(int x, int y, const Color& expectedColor, int tolerance) {
    if (!m_automation) return false;
    ensureApp();
    havel::Color c(expectedColor.r, expectedColor.g, expectedColor.b, expectedColor.a);
    return m_automation->pixelMatch(x, y, c, tolerance);
}

bool PixelAutomationService::pixelMatch(int x, int y, const std::string& hexColor, int tolerance) {
    if (!m_automation) return false;
    ensureApp();
    return pixelMatch(x, y, Color::fromHex(hexColor), tolerance);
}

bool PixelAutomationService::waitPixel(int x, int y, const Color& expectedColor, int tolerance, int timeout) {
    if (!m_automation) return false;
    ensureApp();
    
    havel::Color c(expectedColor.r, expectedColor.g, expectedColor.b, expectedColor.a);
    return m_automation->waitPixel(x, y, c, tolerance, timeout);
}

bool PixelAutomationService::waitPixel(int x, int y, const std::string& hexColor, int tolerance, int timeout) {
    return waitPixel(x, y, Color::fromHex(hexColor), tolerance, timeout);
}

ImageMatch PixelAutomationService::findImage(const std::string& imagePath, const Region& region, float threshold) {
    if (!m_automation) return ImageMatch();
    ensureApp();

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
    ensureApp();

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
    ensureApp();

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->existsImage(imagePath, screenRegion, threshold);
}

int PixelAutomationService::countImage(const std::string& imagePath, const Region& region, float threshold) {
    if (!m_automation) return 0;
    ensureApp();

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->countImage(imagePath, screenRegion, threshold);
}

ImageMatch PixelAutomationService::waitImage(const std::string& imagePath, const Region& region, int timeout, float threshold) {
    if (!m_automation) return ImageMatch();
    ensureApp();

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
    ensureApp();

    havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
    return m_automation->readText(screenRegion);
}

std::string PixelAutomationService::readText(const Region& region, const std::string& ocrEngine) {
  if (!m_automation) return "";
  ensureApp();

  havel::ScreenRegion screenRegion(region.x, region.y, region.w, region.h);
  return m_automation->readText(screenRegion, ocrEngine);
}

bool PixelAutomationService::captureScreen(const std::string& filePath) {
    ensureApp();
    auto& screenshot = ScreenshotService::getInstance();
    auto result = screenshot.captureFullDesktop();
    if (!result) return false;

    int w = result.width;
    int h = result.height;
    auto& rgba = result.data;

    if (rgba.size() < static_cast<size_t>(w * h * 4)) return false;

    QImage img(reinterpret_cast<const uchar*>(rgba.data()), w, h, w * 4,
               QImage::Format_RGBA8888);
    return img.save(QString::fromStdString(filePath), "PNG");
}

bool PixelAutomationService::captureRegion(const Region& region, const std::string& filePath) {
    ensureApp();
    auto& screenshot = ScreenshotService::getInstance();
    auto result = screenshot.captureRegion(region.x, region.y, region.w, region.h);
    if (!result) return false;

    int w = result.width;
    int h = result.height;
    auto& rgba = result.data;

    if (rgba.size() < static_cast<size_t>(w * h * 4)) return false;

    QImage img(reinterpret_cast<const uchar*>(rgba.data()), w, h, w * 4,
               QImage::Format_RGBA8888);
    return img.save(QString::fromStdString(filePath), "PNG");
}

} // namespace havel::host

#endif // HAVE_QT_EXTENSION
