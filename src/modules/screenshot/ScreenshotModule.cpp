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

void registerScreenshotModule(Environment& env, IHostAPI* hostAPI) {
    // Create screenshot module object
    auto screenshotObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Helper to check if screenshot manager is available at runtime
    auto requireScreenshotManager = [hostAPI](const std::string& fn) -> std::optional<HavelRuntimeError> {
        if (!hostAPI->GetIO() || !hostAPI->GetAudioManager()) {
            return HavelRuntimeError("screenshot." + fn + " requires GUI application (QApplication)");
        }
        return std::nullopt;
    };

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
    // Screenshot functions - check manager at runtime, not registration time
    // =========================================================================

    (*screenshotObj)["full"] = HavelValue(BuiltinFunction([&hostAPI, &requireScreenshotManager, createScreenshotResult](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireScreenshotManager("full")) return *err;
        auto& sm = *hostAPI->GetScreenshotManager();

        // Get optional path argument
        QString path;
        if (!args.empty() && args[0].isString()) {
            path = QString::fromStdString(args[0].asString());
        }

        // Take screenshot synchronously
        QString fullPath = sm.takeScreenshot();
        return createScreenshotResult(fullPath);
    }));

    (*screenshotObj)["region"] = HavelValue(BuiltinFunction([&hostAPI, &requireScreenshotManager, createScreenshotResult](const std::vector<HavelValue>& args) -> HavelResult {
        if (auto err = requireScreenshotManager("region")) return *err;
        auto& sm = *hostAPI->GetScreenshotManager();

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

    (*screenshotObj)["monitor"] = HavelValue(BuiltinFunction([&hostAPI, &requireScreenshotManager, createScreenshotResult](const std::vector<HavelValue>&) -> HavelResult {
        if (auto err = requireScreenshotManager("monitor")) return *err;
        auto& sm = *hostAPI->GetScreenshotManager();

        QString fullPath = sm.takeScreenshotOfCurrentMonitor();
        return createScreenshotResult(fullPath);
    }));

    // =========================================================================
    // Monitor information
    // =========================================================================

    (*screenshotObj)["getMonitors"] = HavelValue(BuiltinFunction([&hostAPI, &requireScreenshotManager, getMonitorInfo](const std::vector<HavelValue>&) -> HavelResult {
        if (auto err = requireScreenshotManager("getMonitors")) return *err;
        (void)hostAPI;  // Suppress unused warning
        return getMonitorInfo();
    }));

    // Register screenshot module
    env.Define("screenshot", HavelValue(screenshotObj));
}

} // namespace havel::modules
