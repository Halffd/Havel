#pragma once

#include "host/screenshot/IScreenshotBackend.hpp"

#include <QImage>
#include <QScreen>
#include <QGuiApplication>
#include <QPixmap>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QCursor>
#include <cstring>
#include <memory>

namespace havel::host {

class QtScreenshotBackend : public IScreenshotBackend {
public:
    QtScreenshotBackend() {
        ensureQtApplication();
    }
    ~QtScreenshotBackend() override = default;

    std::vector<unsigned char> captureFullDesktop(const ScreenshotStyle& style = {}) override {
        if (!ensureQtApplication()) return {};
        QScreen* primary = QGuiApplication::primaryScreen();
        if (!primary) return {};
        QRect geo = primary->virtualGeometry();
        QPixmap px = primary->grabWindow(0, geo.x(), geo.y(), geo.width(), geo.height());
        return applyStyleAndConvert(px.toImage(), style);
    }

    std::vector<unsigned char> captureMonitor(int index, const ScreenshotStyle& style = {}) override {
        if (!ensureQtApplication()) return {};
        auto screens = QGuiApplication::screens();
        if (index < 0 || index >= static_cast<int>(screens.size())) return {};
        QScreen* screen = screens[index];
        if (!screen) return {};
        QPixmap px = screen->grabWindow(0);
        return applyStyleAndConvert(px.toImage(), style);
    }

    std::vector<unsigned char> captureActiveWindow(const ScreenshotStyle& style = {}) override {
        if (!ensureQtApplication()) return {};
        QWindow* window = QGuiApplication::focusWindow();
        if (!window) return {};
        QScreen* screen = window->screen();
        if (!screen) return {};
        
        QPixmap px;
        if (style.captureWindowFrame) {
            px = screen->grabWindow(window->winId());
        } else {
            QRect rect = window->geometry();
            px = screen->grabWindow(0, rect.x(), rect.y(), rect.width(), rect.height());
        }
        return applyStyleAndConvert(px.toImage(), style);
    }

    std::vector<unsigned char> captureRegion(int x, int y, int width, int height, const ScreenshotStyle& style = {}) override {
        if (!ensureQtApplication()) return {};
        QScreen* primary = QGuiApplication::primaryScreen();
        if (!primary) return {};
        QPixmap px = primary->grabWindow(0, x, y, width, height);
        return applyStyleAndConvert(px.toImage(), style);
    }

    int getMonitorCount() const override {
        if (!ensureQtApplication()) return 0;
        return static_cast<int>(QGuiApplication::screens().size());
    }

    std::vector<int> getMonitorGeometry(int index) const override {
        if (!ensureQtApplication()) return {};
        auto screens = QGuiApplication::screens();
        if (index < 0 || index >= static_cast<int>(screens.size())) return {};
        QRect geo = screens[index]->geometry();
        return {geo.x(), geo.y(), geo.width(), geo.height()};
    }

private:
    static bool ensureQtApplication() {
        if (QGuiApplication::instance()) return true;
        
        static int dummy_argc = 1;
        static char* dummy_argv[] = { const_cast<char*>("havel-screenshot"), nullptr };
        static std::unique_ptr<QGuiApplication> app;
        
        if (!app) {
            try {
                app = std::make_unique<QGuiApplication>(dummy_argc, dummy_argv);
                app->setQuitOnLastWindowClosed(false);
                return true;
            } catch (...) {
                return false;
            }
        }
        return true;
    }

    static std::vector<unsigned char> applyStyleAndConvert(const QImage& image, const ScreenshotStyle& style) {
        if (image.isNull()) return {};

        QImage result = image;
        
        // 1. Dim the entire image by dimFactor (default 0.8 = 20% darker)
        if (style.dimFactor < 1.0 && style.dimFactor > 0.0) {
            result = applyDim(result, style.dimFactor);
        }
        
        // 2. Draw blue cross at cursor position if enabled
        if (style.showCursorCross) {
            result = drawCursorCross(result, style);
        }
        
        // 3. Apply selection border if this is a region capture (has selectionBorderWidth > 0)
        if (style.selectionBorderWidth > 0) {
            result = drawSelectionBorder(result, style);
        }
        
        // 4. Apply other styling (rounded corners, border, shadow, background)
        if (style.cornerRadius > 0) {
            result = applyRoundedCorners(result, style.cornerRadius);
        }
        
        if (style.borderWidth > 0) {
            result = applyBorder(result, style.borderWidth, style.borderColor);
        }
        
        if (style.backgroundColor != 0xFFFFFFFF || style.cornerRadius > 0) {
            result = applyBackground(result, style.backgroundColor);
        }
        
        if (style.captureShadow && style.shadowBlur > 0) {
            result = applyShadow(result, style.shadowOffset, style.shadowBlur, style.shadowColor, style.backgroundColor);
        }
        
        // Convert to RGBA8888
        QImage rgba = result.convertToFormat(QImage::Format_RGBA8888);
        std::vector<unsigned char> data(rgba.sizeInBytes());
        std::memcpy(data.data(), rgba.bits(), data.size());
        return data;
    }

    static QImage applyDim(const QImage& image, double factor) {
        QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        int w = result.width();
        int h = result.height();
        
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb pixel = line[x];
                int r = qRed(pixel);
                int g = qGreen(pixel);
                int b = qBlue(pixel);
                int a = qAlpha(pixel);
                
                r = static_cast<int>(r * factor);
                g = static_cast<int>(g * factor);
                b = static_cast<int>(b * factor);
                
                line[x] = qRgba(r, g, b, a);
            }
        }
        return result;
    }

    static QImage drawCursorCross(QImage image, const ScreenshotStyle& style) {
        QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        
        // Get global cursor position
        QPoint cursorPos = QCursor::pos();
        
        // Adjust for virtual desktop offset if needed
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary) {
            QRect geo = primary->virtualGeometry();
            cursorPos -= geo.topLeft();
        }
        
        int crossSize = style.cursorCrossSize > 0 ? style.cursorCrossSize : 24;
        int crossWidth = style.cursorCrossWidth > 0 ? style.cursorCrossWidth : 3;
        uint32_t crossColor = style.cursorCrossColor != 0 ? style.cursorCrossColor : 0xFF0000FF;
        
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor::fromRgba(crossColor), crossWidth, Qt::SolidLine, Qt::RoundCap));
        
        int cx = cursorPos.x();
        int cy = cursorPos.y();
        int halfSize = crossSize / 2;
        
        // Draw horizontal line
        painter.drawLine(cx - halfSize, cy, cx + halfSize, cy);
        // Draw vertical line
        painter.drawLine(cx, cy - halfSize, cx, cy + halfSize);
        
        // Draw center circle
        painter.setBrush(QColor::fromRgba(crossColor));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(cx - crossWidth, cy - crossWidth, crossWidth * 2, crossWidth * 2);
        
        painter.end();
        return result;
    }

    static QImage drawSelectionBorder(QImage image, const ScreenshotStyle& style) {
        QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        
        int borderWidth = style.selectionBorderWidth > 0 ? style.selectionBorderWidth : 8;
        uint32_t borderColor = style.selectionBorderColor != 0 ? style.selectionBorderColor : 0xFFFF0000;
        double opacity = style.selectionOpacity > 0 ? style.selectionOpacity : 0.3;
        
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        
        // Draw border around the entire image (selection area)
        QPen pen(QColor::fromRgba(borderColor), borderWidth, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin);
        pen.setColor(QColor::fromRgba(borderColor));
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        
        // Draw border inset by half the border width
        int inset = borderWidth / 2;
        painter.drawRect(inset, inset, result.width() - borderWidth, result.height() - borderWidth);
        
        // Optional: fill selection with semi-transparent overlay
        if (opacity > 0 && opacity < 1.0) {
            QColor fillColor = QColor::fromRgba(borderColor);
            fillColor.setAlphaF(opacity);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawRect(inset, inset, result.width() - borderWidth, result.height() - borderWidth);
        }
        
        painter.end();
        return result;
    }

    static QImage applyRoundedCorners(const QImage& image, int radius) {
        QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(0, 0, image.width(), image.height(), radius, radius);
        painter.setClipPath(path);
        painter.drawImage(0, 0, image);
        painter.end();
        
        return result;
    }

    static QImage applyBorder(const QImage& image, int width, uint32_t color) {
        QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        
        QPen pen(QColor::fromRgba(color), width);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(width/2, width/2, image.width() - width, image.height() - width);
        
        painter.drawImage(0, 0, image);
        painter.end();
        
        return result;
    }

    static QImage applyShadow(const QImage& image, int offset, int blur, uint32_t shadowColor, uint32_t backgroundColor) {
        int shadowSize = blur * 2;
        int newWidth = image.width() + shadowSize * 2 + std::abs(offset);
        int newHeight = image.height() + shadowSize * 2 + std::abs(offset);
        
        QImage result(newWidth, newHeight, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        
        QImage shadow(image.size(), QImage::Format_ARGB32_Premultiplied);
        shadow.fill(QColor::fromRgba(shadowColor));
        
        QPainter shadowPainter(&shadow);
        shadowPainter.setRenderHint(QPainter::Antialiasing);
        shadowPainter.setCompositionMode(QPainter::CompositionMode_Source);
        shadowPainter.drawImage(0, 0, image);
        shadowPainter.end();
        
        int shadowX = shadowSize + (offset >= 0 ? offset : 0);
        int shadowY = shadowSize + (offset >= 0 ? offset : 0);
        painter.drawImage(shadowX, shadowY, shadow);
        painter.drawImage(shadowSize, shadowSize, image);
        painter.end();
        
        return result;
    }

    static QImage applyBackground(const QImage& image, uint32_t backgroundColor) {
        QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(QColor::fromRgba(backgroundColor));
        
        QPainter painter(&result);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, image);
        painter.end();
        
        return result;
    }
};

} // namespace havel::host
