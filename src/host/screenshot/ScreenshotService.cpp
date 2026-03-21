/*
 * ScreenshotService.cpp
 *
 * Screenshot service implementation using Qt.
 */
#include "ScreenshotService.hpp"
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>

namespace havel::host {

ScreenshotService::ScreenshotService() {
    // Qt will be lazily initialized by QGuiApplication
}

ScreenshotService::~ScreenshotService() {
}

std::vector<uint8_t> ScreenshotService::captureFullDesktop() {
    if (!QGuiApplication::instance()) {
        return {};
    }
    
    // Capture all screens
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        return {};
    }
    
    // Get virtual geometry (all monitors combined)
    QRect geometry = primaryScreen->virtualGeometry();
    
    // Grab the entire virtual desktop
    QPixmap pixmap = primaryScreen->grabWindow(0,  // 0 = root window
        geometry.x(),
        geometry.y(),
        geometry.width(),
        geometry.height()
    );
    
    QImage image = pixmap.toImage();
    return imageToRGBA(image);
}

std::vector<uint8_t> ScreenshotService::captureMonitor(int monitorIndex) {
    if (!QGuiApplication::instance()) {
        return {};
    }
    
    auto screens = QGuiApplication::screens();
    if (monitorIndex < 0 || monitorIndex >= static_cast<int>(screens.size())) {
        return {};
    }
    
    return captureFromScreen(screens[monitorIndex]);
}

std::vector<uint8_t> ScreenshotService::captureActiveWindow() {
    if (!QGuiApplication::instance()) {
        return {};
    }
    
    QWindow* window = QGuiApplication::focusWindow();
    if (!window) {
        return {};
    }
    
    QScreen* screen = window->screen();
    if (!screen) {
        return {};
    }
    
    QPixmap pixmap = screen->grabWindow(window->winId());
    QImage image = pixmap.toImage();
    return imageToRGBA(image);
}

std::vector<uint8_t> ScreenshotService::captureRegion(int x, int y, int width, int height) {
    if (!QGuiApplication::instance()) {
        return {};
    }
    
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        return {};
    }
    
    QPixmap pixmap = primaryScreen->grabWindow(0,  // 0 = root window
        x, y, width, height
    );
    
    QImage image = pixmap.toImage();
    return imageToRGBA(image);
}

int ScreenshotService::getMonitorCount() const {
    if (!QGuiApplication::instance()) {
        return 0;
    }
    return QGuiApplication::screens().size();
}

std::vector<int> ScreenshotService::getMonitorGeometry(int monitorIndex) const {
    if (!QGuiApplication::instance()) {
        return {};
    }
    
    auto screens = QGuiApplication::screens();
    if (monitorIndex < 0 || monitorIndex >= static_cast<int>(screens.size())) {
        return {};
    }
    
    QRect geometry = screens[monitorIndex]->geometry();
    return {
        geometry.x(),
        geometry.y(),
        geometry.width(),
        geometry.height()
    };
}

std::vector<uint8_t> ScreenshotService::captureFromScreen(QScreen* screen) {
    if (!screen) {
        return {};
    }
    
    QPixmap pixmap = screen->grabWindow(0);  // 0 = root window
    QImage image = pixmap.toImage();
    return imageToRGBA(image);
}

std::vector<uint8_t> ScreenshotService::imageToRGBA(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    
    // Convert to RGBA8888 format if not already
    QImage rgbaImage = image.convertToFormat(QImage::Format_RGBA8888);
    
    // Copy data to vector
    std::vector<uint8_t> data(rgbaImage.sizeInBytes());
    std::memcpy(data.data(), rgbaImage.bits(), data.size());
    
    return data;
}

} // namespace havel::host
