/*
 * ScreenshotModule.cpp
 *
 * Screenshot module for Havel language.
 * Host binding - connects language to ScreenshotManager.
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "gui/ScreenshotManager.hpp"
#include <QImage>
#include <QBuffer>
#include <QScreen>
#include <QGuiApplication>

namespace havel::modules {

void registerScreenshotModule(Environment& env, HostContext& ctx) {
    if (!ctx.isValid() || !ctx.screenshotManager) {
        return;  // Skip if no screenshot manager available
    }
    
    auto& sm = *ctx.screenshotManager;
    
    // Create screenshot module object
    auto screenshotObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to encode image as base64 and create result object
    auto createScreenshotResult = [](const QString& fullPath) -> HavelValue {
        auto result = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*result)["path"] = HavelValue(fullPath.toStdString());

        // Try to load and encode image data
        QImage img(fullPath);
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
            (*result)["data"] = HavelValue(bytes.toBase64().toStdString());
            (*result)["width"] = HavelValue(static_cast<double>(img.width()));
            (*result)["height"] = HavelValue(static_cast<double>(img.height()));
        }

        return HavelValue(result);
    };

    // Helper to get monitor info
    auto getMonitorInfo = []() -> HavelValue {
        auto monitors = std::make_shared<std::vector<HavelValue>>();
        QScreen *primaryScreen = QGuiApplication::primaryScreen();
        if (primaryScreen) {
            auto monitorObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*monitorObj)["x"] = HavelValue(static_cast<double>(primaryScreen->geometry().x()));
            (*monitorObj)["y"] = HavelValue(static_cast<double>(primaryScreen->geometry().y()));
            (*monitorObj)["width"] = HavelValue(static_cast<double>(primaryScreen->geometry().width()));
            (*monitorObj)["height"] = HavelValue(static_cast<double>(primaryScreen->geometry().height()));
            (*monitorObj)["name"] = HavelValue(primaryScreen->name().toStdString());
            monitors->push_back(HavelValue(monitorObj));
        }
        return HavelValue(monitors);
    };
    
    // =========================================================================
    // Screenshot functions
    // =========================================================================
    
    (*screenshotObj)["full"] = HavelValue(BuiltinFunction([&sm, createScreenshotResult](const std::vector<HavelValue>& args) -> HavelResult {
        // Get optional path argument
        QString path;
        if (!args.empty() && args[0].isString()) {
            path = QString::fromStdString(args[0].asString());
        }
        
        // Take screenshot synchronously
        QString fullPath = sm.takeScreenshot();
        return createScreenshotResult(fullPath);
    }));
    
    (*screenshotObj)["region"] = HavelValue(BuiltinFunction([&sm, createScreenshotResult](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 4) {
            return HavelRuntimeError("screenshot.region() requires (x, y, width, height)");
        }
        
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        int w = static_cast<int>(args[2].asNumber());
        int h = static_cast<int>(args[3].asNumber());
        
        QRect region(x, y, w, h);
        QString fullPath = sm.captureRegion(region);
        return createScreenshotResult(fullPath);
    }));
    
    (*screenshotObj)["monitor"] = HavelValue(BuiltinFunction([&sm, createScreenshotResult](const std::vector<HavelValue>&) -> HavelResult {
        QString fullPath = sm.takeScreenshotOfCurrentMonitor();
        return createScreenshotResult(fullPath);
    }));

    // =========================================================================
    // Monitor information
    // =========================================================================

    (*screenshotObj)["getMonitors"] = HavelValue(BuiltinFunction([getMonitorInfo](const std::vector<HavelValue>&) -> HavelResult {
        return getMonitorInfo();
    }));

    // Register screenshot module
    env.Define("screenshot", HavelValue(screenshotObj));
}

} // namespace havel::modules
