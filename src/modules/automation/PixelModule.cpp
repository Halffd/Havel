/*
 * PixelModule.cpp - Pixel and image automation module for Havel bytecode VM
 *
 * Exposes pixel, image search, and OCR operations via PixelAutomationService.
 * Uses the service registry for dependency injection.
 *
 * Functions:
 *   pixel.get(x, y)              -> {r, g, b, a}
 *   pixel.match(x, y, hex, tol)  -> bool
 *   pixel.wait(x, y, hex, tol, ms) -> bool
 *   pixel.findImage(path, region, threshold) -> {found, x, y, w, h, confidence}
 *   pixel.waitImage(path, region, timeout, threshold) -> match
 *   pixel.existsImage(path, region, threshold) -> bool
 *   pixel.countImage(path, region, threshold) -> int
 *   pixel.findAllImage(path, region, threshold) -> [match, ...]
 *   pixel.readText(region, language) -> string
 *   pixel.capture(path)           -> bool
 *   pixel.captureRegion(x, y, w, h, path) -> bool
 */
#include "PixelModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "modules/ModuleRegistry.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/automation/PixelAutomationService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::PixelAutomationService;

static const char* PIXEL_MODULE_MARKER = "__pixel_module";

static bool isPixelModuleObject(VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, PIXEL_MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isPixelModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static std::shared_ptr<PixelAutomationService> getPixelService() {
    auto& registry = host::ServiceRegistry::instance();
    auto svc = registry.get<PixelAutomationService>();
    if (!svc) {
        debug("PixelModule: PixelAutomationService not available");
    }
    return svc;
}

static host::Region parseRegion(VMApi& api, const Value& v) {
    if (v.isNull() || (!v.isObjectId() && !v.isArrayId())) {
        return host::Region();
    }
    if (v.isObjectId()) {
        auto x = api.getField(v, "x");
        auto y = api.getField(v, "y");
        auto w = api.getField(v, "w");
        auto h = api.getField(v, "h");
        int ix = x.isInt() ? static_cast<int>(x.asInt()) : 0;
        int iy = y.isInt() ? static_cast<int>(y.asInt()) : 0;
        int iw = w.isInt() ? static_cast<int>(w.asInt()) : 0;
        int ih = h.isInt() ? static_cast<int>(h.asInt()) : 0;
        return host::Region(ix, iy, iw, ih);
    }
    if (v.isArrayId()) {
        uint32_t len = api.length(v);
        if (len >= 4) {
            int ix = api.getAt(v, 0).isInt() ? static_cast<int>(api.getAt(v, 0).asInt()) : 0;
            int iy = api.getAt(v, 1).isInt() ? static_cast<int>(api.getAt(v, 1).asInt()) : 0;
            int iw = api.getAt(v, 2).isInt() ? static_cast<int>(api.getAt(v, 2).asInt()) : 0;
            int ih = api.getAt(v, 3).isInt() ? static_cast<int>(api.getAt(v, 3).asInt()) : 0;
            return host::Region(ix, iy, iw, ih);
        }
    }
    return host::Region();
}

static Value colorToValue(VMApi& api, const host::Color& c) {
    auto obj = api.makeObject();
    api.setField(obj, "r", Value(static_cast<int64_t>(c.r)));
    api.setField(obj, "g", Value(static_cast<int64_t>(c.g)));
    api.setField(obj, "b", Value(static_cast<int64_t>(c.b)));
    api.setField(obj, "a", Value(static_cast<int64_t>(c.a)));
    api.setField(obj, "hex", api.makeString(c.toHex()));
    return obj;
}

static Value matchToValue(VMApi& api, const host::ImageMatch& m) {
    auto obj = api.makeObject();
    api.setField(obj, "found", Value::makeBool(m.found));
    if (m.found) {
        api.setField(obj, "x", Value(static_cast<int64_t>(m.x)));
        api.setField(obj, "y", Value(static_cast<int64_t>(m.y)));
        api.setField(obj, "w", Value(static_cast<int64_t>(m.w)));
        api.setField(obj, "h", Value(static_cast<int64_t>(m.h)));
        api.setField(obj, "confidence", Value::makeDouble(static_cast<double>(m.confidence)));
        api.setField(obj, "centerX", Value(static_cast<int64_t>(m.centerX())));
        api.setField(obj, "centerY", Value(static_cast<int64_t>(m.centerY())));
    }
    return obj;
}

static int toInt(const Value& v, int def = 0) {
    if (v.isInt()) return static_cast<int>(v.asInt());
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return def;
}

static float toFloat(const Value& v, float def = 0.0f) {
    if (v.isDouble()) return static_cast<float>(v.asDouble());
    if (v.isInt()) return static_cast<float>(v.asInt());
    return def;
}

static std::string toString(VMApi& api, const Value& v) {
    if (v.isStringId() || v.isStringValId()) return api.toString(v);
    if (v.isNull()) return "";
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isDouble()) return std::to_string(v.asDouble());
    if (v.isBool()) return v.asBool() ? "true" : "false";
    return "";
}

static host::Color parseColor(VMApi& api, const Value& v) {
    if (v.isStringId() || v.isStringValId()) {
        return host::Color::fromHex(api.toString(v));
    }
    if (v.isObjectId()) {
        auto r = api.getField(v, "r");
        auto g = api.getField(v, "g");
        auto b = api.getField(v, "b");
        auto a = api.getField(v, "a");
        return host::Color(
            toInt(r, 0),
            toInt(g, 0),
            toInt(b, 0),
            toInt(a, 255)
        );
    }
    return host::Color();
}

void registerPixelModule(VMApi& api) {
    HAVEL_BEGIN_MODULE("Pixel");

    // pixel.get(x, y) -> {r, g, b, a, hex}
    HAVEL_REGISTER_FUNCTION(api, "pixel.get", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeNull();
        auto svc = getPixelService();
        if (!svc) return Value::makeNull();
        int x = toInt(args[0]);
        int y = toInt(args[1]);
        auto color = svc->getPixel(x, y);
        return colorToValue(api, color);
    });

    // pixel.match(x, y, color, tolerance?) -> bool
    HAVEL_REGISTER_FUNCTION(api, "pixel.match", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 3) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);
        int x = toInt(args[0]);
        int y = toInt(args[1]);
        int tolerance = args.size() > 3 ? toInt(args[3], 0) : 0;
        if (args[2].isStringId() || args[2].isStringValId()) {
            return Value::makeBool(svc->pixelMatch(x, y, toString(api, args[2]), tolerance));
        }
        auto color = parseColor(api, args[2]);
        return Value::makeBool(svc->pixelMatch(x, y, color, tolerance));
    });

    // pixel.wait(x, y, color, tolerance?, timeout?) -> bool
    HAVEL_REGISTER_FUNCTION(api, "pixel.wait", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 3) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);
        int x = toInt(args[0]);
        int y = toInt(args[1]);
        int tolerance = args.size() > 3 ? toInt(args[3], 0) : 0;
        int timeout = args.size() > 4 ? toInt(args[4], 5000) : 5000;
        if (args[2].isStringId() || args[2].isStringValId()) {
            return Value::makeBool(svc->waitPixel(x, y, toString(api, args[2]), tolerance, timeout));
        }
        auto color = parseColor(api, args[2]);
        return Value::makeBool(svc->waitPixel(x, y, color, tolerance, timeout));
    });

    // pixel.findImage(path, region?, threshold?) -> {found, x, y, w, h, confidence, centerX, centerY}
    HAVEL_REGISTER_FUNCTION(api, "pixel.findImage", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return matchToValue(api, host::ImageMatch());
        auto svc = getPixelService();
        if (!svc) return matchToValue(api, host::ImageMatch());
        std::string path = toString(api, args[0]);
        host::Region region = args.size() > 1 ? parseRegion(api, args[1]) : host::Region();
        float threshold = args.size() > 2 ? toFloat(args[2], 0.9f) : 0.9f;
        auto match = svc->findImage(path, region, threshold);
        return matchToValue(api, match);
    });

    // pixel.waitImage(path, region?, timeout?, threshold?) -> match
    HAVEL_REGISTER_FUNCTION(api, "pixel.waitImage", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return matchToValue(api, host::ImageMatch());
        auto svc = getPixelService();
        if (!svc) return matchToValue(api, host::ImageMatch());
        std::string path = toString(api, args[0]);
        host::Region region = args.size() > 1 ? parseRegion(api, args[1]) : host::Region();
        int timeout = args.size() > 2 ? toInt(args[2], 5000) : 5000;
        float threshold = args.size() > 3 ? toFloat(args[3], 0.9f) : 0.9f;
        auto match = svc->waitImage(path, region, timeout, threshold);
        return matchToValue(api, match);
    });

    // pixel.existsImage(path, region?, threshold?) -> bool
    HAVEL_REGISTER_FUNCTION(api, "pixel.existsImage", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);
        std::string path = toString(api, args[0]);
        host::Region region = args.size() > 1 ? parseRegion(api, args[1]) : host::Region();
        float threshold = args.size() > 2 ? toFloat(args[2], 0.9f) : 0.9f;
        return Value::makeBool(svc->existsImage(path, region, threshold));
    });

    // pixel.countImage(path, region?, threshold?) -> int
    HAVEL_REGISTER_FUNCTION(api, "pixel.countImage", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getPixelService();
        if (!svc) return Value::makeInt(0);
        std::string path = toString(api, args[0]);
        host::Region region = args.size() > 1 ? parseRegion(api, args[1]) : host::Region();
        float threshold = args.size() > 2 ? toFloat(args[2], 0.9f) : 0.9f;
        return Value::makeInt(svc->countImage(path, region, threshold));
    });

    // pixel.findAllImage(path, region?, threshold?) -> [{found, x, y, w, h, confidence}, ...]
    HAVEL_REGISTER_FUNCTION(api, "pixel.findAllImage", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return api.makeArray();
        auto svc = getPixelService();
        if (!svc) return api.makeArray();
        std::string path = toString(api, args[0]);
        host::Region region = args.size() > 1 ? parseRegion(api, args[1]) : host::Region();
        float threshold = args.size() > 2 ? toFloat(args[2], 0.9f) : 0.9f;
        auto matches = svc->findAllImages(path, region, threshold);
        auto arr = api.makeArray();
        for (const auto& m : matches) {
            api.push(arr, matchToValue(api, m));
        }
        return arr;
    });

    // pixel.readText(region?, language?) -> string
    HAVEL_REGISTER_FUNCTION(api, "pixel.readText", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getPixelService();
        if (!svc) return api.makeString("");
        host::Region region = args.size() > 0 ? parseRegion(api, args[0]) : host::Region();
        if (args.size() > 1 && (args[1].isStringId() || args[1].isStringValId())) {
            return api.makeString(svc->readText(region, toString(api, args[1])));
        }
        return api.makeString(svc->readText(region));
    });

    // pixel.capture(path) -> bool
    HAVEL_REGISTER_FUNCTION(api, "pixel.capture", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);
        std::string path = toString(api, args[0]);
        return Value::makeBool(svc->captureScreen(path));
    });

    // pixel.captureRegion(x, y, w, h, path) -> bool
    HAVEL_REGISTER_FUNCTION(api, "pixel.captureRegion", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 5) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);
        int x = toInt(args[0]);
        int y = toInt(args[1]);
        int w = toInt(args[2]);
        int h = toInt(args[3]);
        std::string path = toString(api, args[4]);
        return Value::makeBool(svc->captureRegion(host::Region(x, y, w, h), path));
    });

    // Build module object — pin as GC root to prevent collection during registration
    auto pixelObj = api.makeObject();
    api.setGlobal("pixel", pixelObj);
    api.setField(pixelObj, PIXEL_MODULE_MARKER, Value::makeBool(true));
    api.setField(pixelObj, "get", api.makeFunctionRef("pixel.get"));
    api.setField(pixelObj, "match", api.makeFunctionRef("pixel.match"));
    api.setField(pixelObj, "wait", api.makeFunctionRef("pixel.wait"));
    api.setField(pixelObj, "findImage", api.makeFunctionRef("pixel.findImage"));
    api.setField(pixelObj, "waitImage", api.makeFunctionRef("pixel.waitImage"));
    api.setField(pixelObj, "existsImage", api.makeFunctionRef("pixel.existsImage"));
    api.setField(pixelObj, "countImage", api.makeFunctionRef("pixel.countImage"));
    api.setField(pixelObj, "findAllImage", api.makeFunctionRef("pixel.findAllImage"));
    api.setField(pixelObj, "readText", api.makeFunctionRef("pixel.readText"));
    api.setField(pixelObj, "capture", api.makeFunctionRef("pixel.capture"));
    api.setField(pixelObj, "captureRegion", api.makeFunctionRef("pixel.captureRegion"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules
