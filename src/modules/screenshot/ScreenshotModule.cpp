/*
 * ScreenshotModule.cpp
 *
 * Screenshot module for Havel language.
 * Host binding - connects language to ScreenshotManager.
 * 
 * Features:
 * - PNG format (lossless, default)
 * - JPEG format (lossy, configurable quality)
 * - Quality/compression settings
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "gui/ScreenshotManager.hpp"
#include <QImage>
#include <QBuffer>

namespace havel::modules {

void registerScreenshotModule(Environment& env, HostContext& ctx) {
    if (!ctx.isValid() || !ctx.screenshotManager) {
        return;  // Skip if no screenshot manager available
    }

    auto& sm = *ctx.screenshotManager;

    // Create screenshot module object
    auto screenshotObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Helper to determine format from file extension
    auto getFormatFromPath = [](const QString& path) -> QString {
        if (path.endsWith(".jpg", Qt::CaseInsensitive) || 
            path.endsWith(".jpeg", Qt::CaseInsensitive)) {
            return "JPG";
        }
        if (path.endsWith(".webp", Qt::CaseInsensitive)) {
            return "WEBP";
        }
        // Default to PNG for lossless quality
        return "PNG";
    };

    // Helper to encode image and create result object
    // Supports format and quality parameters
    auto createScreenshotResult = [](const QString& fullPath, 
                                      const QString& format = "PNG",
                                      int quality = -1) -> HavelValue {
        auto result = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*result)["path"] = HavelValue(fullPath.toStdString());
        (*result)["format"] = HavelValue(format.toStdString());
        (*result)["quality"] = HavelValue(quality >= 0 ? quality : 100.0);

        // Try to load and encode image data
        QImage img(fullPath);
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            
            // Save with specified format and quality
            // quality: 0-100 for JPEG/WEBP, -1 for PNG (uses compression level)
            if (img.save(&buffer, format.toUtf8().constData(), quality)) {
                (*result)["data"] = HavelValue(bytes.toBase64().toStdString());
                (*result)["width"] = HavelValue(static_cast<double>(img.width()));
                (*result)["height"] = HavelValue(static_cast<double>(img.height()));
                (*result)["size"] = HavelValue(static_cast<double>(bytes.size()));
            } else {
                (*result)["error"] = HavelValue("Failed to encode image as " + format.toStdString());
            }
        } else {
            (*result)["error"] = HavelValue("Failed to load image from " + fullPath.toStdString());
        }

        return HavelValue(result);
    };

    // Helper to parse quality argument
    // Returns quality value (0-100) or -1 for default
    auto parseQualityArg = [](const std::vector<HavelValue>& args, size_t index) -> int {
        if (args.size() <= index) return -1;  // Default
        if (args[index].isNull()) return -1;
        return static_cast<int>(args[index].asNumber());
    };

    // Helper to parse format argument
    auto parseFormatArg = [](const std::vector<HavelValue>& args, size_t index,
                              const QString& pathHint) -> QString {
        if (args.size() > index && args[index].isString()) {
            QString fmt = QString::fromStdString(args[index].asString()).toUpper();
            if (fmt == "JPG" || fmt == "JPEG") return "JPG";
            if (fmt == "WEBP") return "WEBP";
            if (fmt == "PNG") return "PNG";
            if (fmt == "BMP") return "BMP";
        }
        // Auto-detect from path extension
        return getFormatFromPath(pathHint);
    };

    // =========================================================================
    // Screenshot functions
    // =========================================================================

    // screenshot.full([path], [quality], [format])
    // - path: optional file path (default: auto-generated)
    // - quality: 0-100 for JPEG/WEBP, ignored for PNG (default: -1 = auto)
    // - format: "PNG", "JPG", "WEBP", "BMP" (default: auto-detect from path)
    (*screenshotObj)["full"] = HavelValue(BuiltinFunction([&sm, createScreenshotResult, 
                                                            parseQualityArg, parseFormatArg,
                                                            getFormatFromPath](const std::vector<HavelValue>& args) -> HavelResult {
        // Parse arguments
        QString path;
        if (!args.empty() && args[0].isString()) {
            path = QString::fromStdString(args[0].asString());
        }

        int quality = parseQualityArg(args, 1);
        QString format = parseFormatArg(args, 2, path);

        // Take screenshot synchronously
        QString fullPath = sm.takeScreenshot(path.isEmpty() ? QString() : path);
        
        return createScreenshotResult(fullPath, format, quality);
    }));

    // screenshot.region(x, y, width, height, [quality], [format])
    (*screenshotObj)["region"] = HavelValue(BuiltinFunction([&sm, createScreenshotResult,
                                                              parseQualityArg, parseFormatArg](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 4) {
            return HavelRuntimeError("screenshot.region() requires (x, y, width, height)");
        }

        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        int w = static_cast<int>(args[2].asNumber());
        int h = static_cast<int>(args[3].asNumber());

        int quality = parseQualityArg(args, 4);
        QString format = parseFormatArg(args, 5, QString());

        QRect region(x, y, w, h);
        QString fullPath = sm.captureRegion(region);
        
        return createScreenshotResult(fullPath, format, quality);
    }));

    // screenshot.monitor([quality], [format])
    (*screenshotObj)["monitor"] = HavelValue(BuiltinFunction([&sm, createScreenshotResult,
                                                               parseQualityArg, parseFormatArg](const std::vector<HavelValue>& args) -> HavelResult {
        int quality = parseQualityArg(args, 0);
        QString format = parseFormatArg(args, 1, QString());

        QString fullPath = sm.takeScreenshotOfCurrentMonitor();
        
        return createScreenshotResult(fullPath, format, quality);
    }));

    // screenshot.save(imageData, path, [quality], [format])
    // Save raw image data to file with specified format/quality
    (*screenshotObj)["save"] = HavelValue(BuiltinFunction([&sm](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("screenshot.save() requires (imageData, path)");
        }

        // Get image data (base64 or path)
        QString imageData;
        if (args[0].isString()) {
            imageData = QString::fromStdString(args[0].asString());
        } else {
            return HavelRuntimeError("screenshot.save() imageData must be a string (path or base64)");
        }

        QString path = QString::fromStdString(args[1].asString());
        int quality = -1;
        if (args.size() > 2 && !args[2].isNull()) {
            quality = static_cast<int>(args[2].asNumber());
        }

        QString format = getFormatFromPath(path);
        if (args.size() > 3 && args[3].isString()) {
            format = QString::fromStdString(args[3].asString()).toUpper();
        }

        // Load image from base64 or existing file
        QImage img;
        if (imageData.startsWith("/")) {
            // It's a file path
            img.load(imageData);
        } else {
            // It's base64 data
            QByteArray data = QByteArray::fromBase64(imageData.toUtf8());
            img.loadFromData(data);
        }

        if (img.isNull()) {
            return HavelRuntimeError("Failed to load image data");
        }

        // Save with specified format and quality
        if (img.save(path, format.toUtf8().constData(), quality)) {
            auto result = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*result)["success"] = HavelValue(true);
            (*result)["path"] = HavelValue(path.toStdString());
            (*result)["format"] = HavelValue(format.toStdString());
            return HavelValue(result);
        } else {
            return HavelRuntimeError("Failed to save image to " + path.toStdString());
        }
    }));

    // screenshot.encode(imagePath, [quality], [format])
    // Encode image to base64 with specified format/quality
    (*screenshotObj)["encode"] = HavelValue(BuiltinFunction([createScreenshotResult,
                                                              parseQualityArg, parseFormatArg,
                                                              getFormatFromPath](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("screenshot.encode() requires imagePath");
        }

        QString path = QString::fromStdString(args[0].asString());
        int quality = parseQualityArg(args, 1);
        QString format = parseFormatArg(args, 2, path);

        return createScreenshotResult(path, format, quality);
    }));

    // Register screenshot module
    env.Define("screenshot", HavelValue(screenshotObj));
}

} // namespace havel::modules
