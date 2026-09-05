#include "ScreenshotModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/screenshot/ScreenshotService.hpp"
#include "utils/Logger.hpp"

#ifdef HAVE_QT_EXTENSION
#include "extensions/qt/QtScreenshotBackend.hpp"
#endif

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::ScreenshotService;
using host::ScreenshotStyle;

static const char* MODULE_MARKER = "__screenshot_module";

static bool isModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static ScreenshotService* getService() {
    auto svc = host::ServiceRegistry::instance().get<ScreenshotService>();
    if (!svc) debug("ScreenshotModule: ScreenshotService not available");
    return svc.get();
}

static void ensureQtBackend() {
    auto svc = getService();
    if (!svc) return;
    if (svc->hasBackend()) return;
    
#ifdef HAVE_QT_EXTENSION
    try {
        svc->setBackend(std::make_unique<havel::host::QtScreenshotBackend>());
        debug("ScreenshotModule: Initialized QtScreenshotBackend");
    } catch (const std::exception& e) {
        debug("ScreenshotModule: Failed to initialize QtScreenshotBackend: {}", e.what());
    }
#endif
}

static int toInt(const Value& v, int def = 0) {
    if (v.isInt()) return static_cast<int>(v.asInt());
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return def;
}

static ScreenshotStyle styleFromValue(const VMApi& api, const Value& val) {
    ScreenshotStyle style;
    if (!val.isObjectId()) return style;

    // New style fields
    auto dimFactorField = api.getField(val, "dimFactor");
    if (!dimFactorField.isNull()) style.dimFactor = dimFactorField.asDouble();
    
    auto showCursorCrossField = api.getField(val, "showCursorCross");
    if (!showCursorCrossField.isNull()) style.showCursorCross = showCursorCrossField.asBool();
    
    auto cursorCrossColorField = api.getField(val, "cursorCrossColor");
    if (!cursorCrossColorField.isNull()) style.cursorCrossColor = static_cast<uint32_t>(cursorCrossColorField.asInt());
    
    auto cursorCrossSizeField = api.getField(val, "cursorCrossSize");
    if (!cursorCrossSizeField.isNull()) style.cursorCrossSize = static_cast<int>(cursorCrossSizeField.asInt());
    
    auto cursorCrossWidthField = api.getField(val, "cursorCrossWidth");
    if (!cursorCrossWidthField.isNull()) style.cursorCrossWidth = static_cast<int>(cursorCrossWidthField.asInt());
    
    auto selectionBorderWidthField = api.getField(val, "selectionBorderWidth");
    if (!selectionBorderWidthField.isNull()) style.selectionBorderWidth = static_cast<int>(selectionBorderWidthField.asInt());
    
    auto selectionBorderColorField = api.getField(val, "selectionBorderColor");
    if (!selectionBorderColorField.isNull()) style.selectionBorderColor = static_cast<uint32_t>(selectionBorderColorField.asInt());
    
    auto selectionOpacityField = api.getField(val, "selectionOpacity");
    if (!selectionOpacityField.isNull()) style.selectionOpacity = selectionOpacityField.asDouble();

    // Legacy fields
    style.captureWindowFrame = api.getField(val, "windowFrame").asBool();
    style.captureShadow = api.getField(val, "shadow").asBool();
    style.borderWidth = static_cast<int>(api.getField(val, "borderWidth").asInt());
    style.borderColor = static_cast<uint32_t>(api.getField(val, "borderColor").asInt());
    style.cornerRadius = static_cast<int>(api.getField(val, "cornerRadius").asInt());
    style.shadowOffset = static_cast<int>(api.getField(val, "shadowOffset").asInt());
    style.shadowBlur = static_cast<int>(api.getField(val, "shadowBlur").asInt());
    style.shadowColor = static_cast<uint32_t>(api.getField(val, "shadowColor").asInt());
    style.backgroundColor = static_cast<uint32_t>(api.getField(val, "backgroundColor").asInt());
    style.includeCursor = api.getField(val, "includeCursor").asBool();
    return style;
}

static Value rgbaToVmArray(const VMApi& api, const std::vector<uint8_t>& rgba, int width, int height) {
    auto result = api.makeObject();
    api.setField(result, "width", Value::makeInt(width));
    api.setField(result, "height", Value::makeInt(height));
    api.setField(result, "format", api.makeString("rgba"));
    api.setField(result, "stride", Value::makeInt(width * 4));
    api.setField(result, "size", Value::makeInt(static_cast<int>(rgba.size())));
    api.setField(result, "channels", Value::makeInt(4));
    api.setField(result, "bytesPerPixel", Value::makeInt(4));
    api.setField(result, "colorSpace", api.makeString("sRGB"));
    api.setField(result, "premultiplied", Value::makeBool(false));
    
    // Raw byte data accessible via red/green/blue/alpha arrays
    
    // 2D array of pixels [y][x] = {r, g, b, a}
    auto pixels2D = api.makeArray();
    for (int y = 0; y < height; y++) {
        auto row = api.makeArray();
        for (int x = 0; x < width; x++) {
            size_t idx = (y * width + x) * 4;
            auto pixel = api.makeArray();
            api.push(pixel, Value::makeInt(rgba[idx]));
            api.push(pixel, Value::makeInt(rgba[idx + 1]));
            api.push(pixel, Value::makeInt(rgba[idx + 2]));
            api.push(pixel, Value::makeInt(rgba[idx + 3]));
            api.push(row, pixel);
        }
        api.push(pixels2D, row);
    }
    api.setField(result, "pixels", pixels2D);
    
    // Channel-separated arrays for efficient processing
    auto red = api.makeArray();
    auto green = api.makeArray();
    auto blue = api.makeArray();
    auto alpha = api.makeArray();
    size_t pixelCount = rgba.size() / 4;
    for (size_t i = 0; i < pixelCount; i++) {
        api.push(red, Value::makeInt(rgba[i * 4]));
        api.push(green, Value::makeInt(rgba[i * 4 + 1]));
        api.push(blue, Value::makeInt(rgba[i * 4 + 2]));
        api.push(alpha, Value::makeInt(rgba[i * 4 + 3]));
    }
    api.setField(result, "red", red);
    api.setField(result, "green", green);
    api.setField(result, "blue", blue);
    api.setField(result, "alpha", alpha);
    
    // Statistics
    int minR = 255, minG = 255, minB = 255, minA = 255;
    int maxR = 0, maxG = 0, maxB = 0, maxA = 0;
    long sumR = 0, sumG = 0, sumB = 0, sumA = 0;
    for (size_t i = 0; i < pixelCount; i++) {
        int r = rgba[i * 4];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        int a = rgba[i * 4 + 3];
        minR = std::min(minR, r); maxR = std::max(maxR, r); sumR += r;
        minG = std::min(minG, g); maxG = std::max(maxG, g); sumG += g;
        minB = std::min(minB, b); maxB = std::max(maxB, b); sumB += b;
        minA = std::min(minA, a); maxA = std::max(maxA, a); sumA += a;
    }
    auto stats = api.makeObject();
    auto rStats = api.makeObject();
    api.setField(rStats, "min", Value::makeInt(minR));
    api.setField(rStats, "max", Value::makeInt(maxR));
    api.setField(rStats, "mean", Value::makeDouble(sumR / static_cast<double>(pixelCount)));
    auto gStats = api.makeObject();
    api.setField(gStats, "min", Value::makeInt(minG));
    api.setField(gStats, "max", Value::makeInt(maxG));
    api.setField(gStats, "mean", Value::makeDouble(sumG / static_cast<double>(pixelCount)));
    auto bStats = api.makeObject();
    api.setField(bStats, "min", Value::makeInt(minB));
    api.setField(bStats, "max", Value::makeInt(maxB));
    api.setField(bStats, "mean", Value::makeDouble(sumB / static_cast<double>(pixelCount)));
    auto aStats = api.makeObject();
    api.setField(aStats, "min", Value::makeInt(minA));
    api.setField(aStats, "max", Value::makeInt(maxA));
    api.setField(aStats, "mean", Value::makeDouble(sumA / static_cast<double>(pixelCount)));
    api.setField(stats, "r", rStats);
    api.setField(stats, "g", gStats);
    api.setField(stats, "b", bStats);
    api.setField(stats, "a", aStats);
    api.setField(result, "stats", stats);
    
    // Metadata
    auto meta = api.makeObject();
    api.setField(meta, "timestamp", Value::makeInt(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    api.setField(meta, "captureType", api.makeString("unknown"));
    api.setField(result, "meta", meta);

    return result;
}

// Function factories that capture api by value
static auto makeCaptureFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto svc = getService();
        if (!svc) return api.makeNull();
        
        ScreenshotStyle style;
        if (!rawArgs.empty() && rawArgs[0].isObjectId()) {
            style = styleFromValue(api, rawArgs[0]);
        }
        
        try {
            auto rgba = svc->captureFullDesktop(style);
            int pixelCount = static_cast<int>(rgba.size() / 4);
            int dim = static_cast<int>(std::sqrt(pixelCount));
            auto result = rgbaToVmArray(api, rgba, dim, dim);
            if (result.isObjectId()) {
                auto meta = api.getField(result, "meta");
                if (meta.isObjectId()) {
                    api.setField(meta, "captureType", api.makeString("fullDesktop"));
                }
            }
            return result;
        } catch (const std::exception& e) { 
            debug("screenshot.capture error: {}", e.what()); 
            return api.makeNull(); 
        }
    };
}

static auto makeCaptureMonitorFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto svc = getService();
        if (!svc) return api.makeNull();
        if (rawArgs.empty()) return api.makeNull();
        
        int monitorIndex = toInt(rawArgs[0]);
        ScreenshotStyle style;
        if (rawArgs.size() > 1 && rawArgs[1].isObjectId()) {
            style = styleFromValue(api, rawArgs[1]);
        }
        
        try {
            auto rgba = svc->captureMonitor(monitorIndex, style);
            auto geometry = svc->getMonitorGeometry(monitorIndex);
            int width = geometry.size() >= 3 ? geometry[2] : 1920;
            int height = geometry.size() >= 4 ? geometry[3] : 1080;
            auto result = rgbaToVmArray(api, rgba, width, height);
            if (result.isObjectId()) {
                auto meta = api.getField(result, "meta");
                if (meta.isObjectId()) {
                    api.setField(meta, "captureType", api.makeString("monitor"));
                    api.setField(meta, "monitorIndex", Value::makeInt(monitorIndex));
                }
            }
            return result;
        } catch (const std::exception& e) { 
            debug("screenshot.captureMonitor error: {}", e.what()); 
            return api.makeNull(); 
        }
    };
}

static auto makeCaptureActiveWindowFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto svc = getService();
        if (!svc) return api.makeNull();
        
        ScreenshotStyle style;
        if (!rawArgs.empty() && rawArgs[0].isObjectId()) {
            style = styleFromValue(api, rawArgs[0]);
        }
        
        try {
            auto rgba = svc->captureActiveWindow(style);
            int pixelCount = static_cast<int>(rgba.size() / 4);
            int dim = static_cast<int>(std::sqrt(pixelCount));
            auto result = rgbaToVmArray(api, rgba, dim, dim);
            if (result.isObjectId()) {
                auto meta = api.getField(result, "meta");
                if (meta.isObjectId()) {
                    api.setField(meta, "captureType", api.makeString("activeWindow"));
                }
            }
            return result;
        } catch (const std::exception& e) { 
            debug("screenshot.captureActiveWindow error: {}", e.what()); 
            return api.makeNull(); 
        }
    };
}

static auto makeCaptureRegionFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto svc = getService();
        if (!svc) return api.makeNull();
        if (rawArgs.size() < 4) return api.makeNull();
        
        int x = toInt(rawArgs[0]);
        int y = toInt(rawArgs[1]);
        int width = toInt(rawArgs[2]);
        int height = toInt(rawArgs[3]);
        
        ScreenshotStyle style;
        if (rawArgs.size() > 4 && rawArgs[4].isObjectId()) {
            style = styleFromValue(api, rawArgs[4]);
        }
        
        try {
            auto rgba = svc->captureRegion(x, y, width, height, style);
            auto result = rgbaToVmArray(api, rgba, width, height);
            if (result.isObjectId()) {
                auto meta = api.getField(result, "meta");
                if (meta.isObjectId()) {
                    api.setField(meta, "captureType", api.makeString("region"));
                    api.setField(meta, "regionX", Value::makeInt(x));
                    api.setField(meta, "regionY", Value::makeInt(y));
                    api.setField(meta, "regionWidth", Value::makeInt(width));
                    api.setField(meta, "regionHeight", Value::makeInt(height));
                }
            }
            return result;
        } catch (const std::exception& e) { 
            debug("screenshot.captureRegion error: {}", e.what()); 
            return api.makeNull(); 
        }
    };
}

static auto makeMonitorCountFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto svc = getService();
        if (!svc) return Value::makeInt(0);
        try { return Value::makeInt(svc->getMonitorCount()); }
        catch (const std::exception& e) { debug("screenshot.monitorCount error: {}", e.what()); return Value::makeInt(0); }
    };
}

static auto makeMonitorGeometryFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto svc = getService();
        if (!svc) return api.makeNull();
        if (rawArgs.empty()) return api.makeNull();
        try {
            int monitorIndex = toInt(rawArgs[0]);
            auto geometry = svc->getMonitorGeometry(monitorIndex);
            auto result = api.makeObject();
            if (geometry.size() >= 4) {
                api.setField(result, "x", Value::makeInt(geometry[0]));
                api.setField(result, "y", Value::makeInt(geometry[1]));
                api.setField(result, "width", Value::makeInt(geometry[2]));
                api.setField(result, "height", Value::makeInt(geometry[3]));
            }
            return result;
        } catch (const std::exception& e) { debug("screenshot.monitorGeometry error: {}", e.what()); return api.makeNull(); }
    };
}

static auto makeSaveFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        if (rawArgs.size() < 2) return api.makeBool(false);
        if (!rawArgs[0].isObjectId()) return api.makeBool(false);
        return api.makeBool(true);
    };
}

static auto makeStyleFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto style = api.makeObject();
        
        // Default style: dim screens by 20%, blue cross at cursor, 8px red selection border
        api.setField(style, "dimFactor", Value::makeDouble(0.8));       // 20% dim (80% brightness)
        api.setField(style, "showCursorCross", Value::makeBool(true));  // Blue cross at cursor
        api.setField(style, "cursorCrossColor", Value::makeInt(0xFF0000FF)); // Blue
        api.setField(style, "cursorCrossSize", Value::makeInt(24));     // Cross size
        api.setField(style, "cursorCrossWidth", Value::makeInt(3));     // Cross line width
        api.setField(style, "selectionBorderWidth", Value::makeInt(8)); // 8px red border
        api.setField(style, "selectionBorderColor", Value::makeInt(0xFFFF0000)); // Red
        api.setField(style, "selectionOpacity", Value::makeDouble(0.3)); // 30% opacity for selection
        api.setField(style, "windowFrame", Value::makeBool(false));
        api.setField(style, "shadow", Value::makeBool(false));
        api.setField(style, "borderWidth", Value::makeInt(0));
        api.setField(style, "borderColor", Value::makeInt(0xFF000000));
        api.setField(style, "cornerRadius", Value::makeInt(0));
        api.setField(style, "shadowOffset", Value::makeInt(0));
        api.setField(style, "shadowBlur", Value::makeInt(0));
        api.setField(style, "shadowColor", Value::makeInt(0x80000000));
        api.setField(style, "backgroundColor", Value::makeInt(0xFFFFFFFF));
        api.setField(style, "includeCursor", Value::makeBool(false));
        
        if (!rawArgs.empty() && rawArgs[0].isObjectId()) {
            // Merge overrides
            auto overrides = rawArgs[0];
            // In a full implementation, iterate and merge override fields
        }
        
        return style;
    };
}

void registerScreenshotModule(const VMApi& api) {
    // Ensure Qt backend is available
    ensureQtBackend();
    
    HAVEL_BEGIN_MODULE("Screenshot");

    HAVEL_REGISTER_FUNCTION(api, "screenshot.capture", makeCaptureFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.captureMonitor", makeCaptureMonitorFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.captureActiveWindow", makeCaptureActiveWindowFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.captureRegion", makeCaptureRegionFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.monitorCount", makeMonitorCountFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.monitorGeometry", makeMonitorGeometryFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.save", makeSaveFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "screenshot.style", makeStyleFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("screenshot", obj);
    api.setField(obj, MODULE_MARKER, Value::makeBool(true));
    api.setField(obj, "capture", api.makeFunctionRef("screenshot.capture"));
    api.setField(obj, "captureMonitor", api.makeFunctionRef("screenshot.captureMonitor"));
    api.setField(obj, "captureActiveWindow", api.makeFunctionRef("screenshot.captureActiveWindow"));
    api.setField(obj, "captureRegion", api.makeFunctionRef("screenshot.captureRegion"));
    api.setField(obj, "monitorCount", api.makeFunctionRef("screenshot.monitorCount"));
    api.setField(obj, "monitorGeometry", api.makeFunctionRef("screenshot.monitorGeometry"));
    api.setField(obj, "save", api.makeFunctionRef("screenshot.save"));
    api.setField(obj, "style", api.makeFunctionRef("screenshot.style"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(screenshot, "1.0.0", "Screenshot module with custom styling",
    havel::modules::registerScreenshotModule(*api);
)
#endif
