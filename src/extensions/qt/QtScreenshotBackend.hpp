#pragma once

#include "host/screenshot/IScreenshotBackend.hpp"

#include <QImage>
#include <QScreen>
#include <QGuiApplication>
#include <QPixmap>
#include <QWindow>
#include <cstring>

namespace havel::host {

class QtScreenshotBackend : public IScreenshotBackend {
public:
    QtScreenshotBackend() = default;
    ~QtScreenshotBackend() override = default;

    std::vector<unsigned char> captureFullDesktop() override {
        if (!QGuiApplication::instance()) return {};
        QScreen *primary = QGuiApplication::primaryScreen();
        if (!primary) return {};
        QRect geo = primary->virtualGeometry();
        QPixmap px = primary->grabWindow(0, geo.x(), geo.y(), geo.width(), geo.height());
        return imageToRGBA(px.toImage());
    }

    std::vector<unsigned char> captureMonitor(int index) override {
        if (!QGuiApplication::instance()) return {};
        auto screens = QGuiApplication::screens();
        if (index < 0 || index >= static_cast<int>(screens.size())) return {};
        QScreen *screen = screens[index];
        if (!screen) return {};
        QPixmap px = screen->grabWindow(0);
        return imageToRGBA(px.toImage());
    }

    std::vector<unsigned char> captureActiveWindow() override {
        if (!QGuiApplication::instance()) return {};
        QWindow *window = QGuiApplication::focusWindow();
        if (!window) return {};
        QScreen *screen = window->screen();
        if (!screen) return {};
        QPixmap px = screen->grabWindow(window->winId());
        return imageToRGBA(px.toImage());
    }

    std::vector<unsigned char> captureRegion(int x, int y, int width, int height) override {
        if (!QGuiApplication::instance()) return {};
        QScreen *primary = QGuiApplication::primaryScreen();
        if (!primary) return {};
        QPixmap px = primary->grabWindow(0, x, y, width, height);
        return imageToRGBA(px.toImage());
    }

    int getMonitorCount() const override {
        if (!QGuiApplication::instance()) return 0;
        return static_cast<int>(QGuiApplication::screens().size());
    }

    std::vector<int> getMonitorGeometry(int index) const override {
        if (!QGuiApplication::instance()) return {};
        auto screens = QGuiApplication::screens();
        if (index < 0 || index >= static_cast<int>(screens.size())) return {};
        QRect geo = screens[index]->geometry();
        return {geo.x(), geo.y(), geo.width(), geo.height()};
    }

private:
    static std::vector<unsigned char> imageToRGBA(const QImage &image) {
        if (image.isNull()) return {};
        QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        std::vector<unsigned char> data(rgba.sizeInBytes());
        std::memcpy(data.data(), rgba.bits(), data.size());
        return data;
    }
};

} // namespace havel::host
