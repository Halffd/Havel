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
#include "host/ServiceRegistry.hpp"
#include "host/automation/PixelAutomationService.hpp"
#include "core/io/IO.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::PixelAutomationService;

static std::pair<int, int> currentMousePos() {
    auto io = host::ServiceRegistry::instance().get<IO>();
    if (!io) return {0, 0};
    return io->GetMousePosition();
}

static const char* PIXEL_MODULE_MARKER = "__pixel_module";

static bool isPixelModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, PIXEL_MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isPixelModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static std::shared_ptr<PixelAutomationService> getPixelService() {
    auto& registry = host::ServiceRegistry::instance();
    auto svc = registry.get<PixelAutomationService>();
    if (!svc) {
        ::havel::debug("PixelModule: PixelAutomationService not available in registry");
    } else {
        ::havel::debug("PixelModule: PixelAutomationService available");
    }
    return svc;
}

static host::Region parseRegion(const VMApi& api, const Value& v) {
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

static Value colorToValue(const VMApi& api, const host::Color& c) {
    auto obj = api.makeObject();
    api.setField(obj, "r", Value(static_cast<int64_t>(c.r)));
    api.setField(obj, "g", Value(static_cast<int64_t>(c.g)));
    api.setField(obj, "b", Value(static_cast<int64_t>(c.b)));
    api.setField(obj, "a", Value(static_cast<int64_t>(c.a)));
    api.setField(obj, "hex", api.makeString(c.toHex()));
    return obj;
}

static Value matchToValue(const VMApi& api, const host::ImageMatch& m) {
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

static std::string toString(const VMApi& api, const Value& v) {
    if (v.isStringId() || v.isStringValId()) return api.toString(v);
    if (v.isNull()) return "";
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isDouble()) return std::to_string(v.asDouble());
    if (v.isBool()) return v.asBool() ? "true" : "false";
    return "";
}

static host::Color parseColor(const VMApi& api, const Value& v) {
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

void registerPixelModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("Pixel");

    // pixel.get - defaults to cursor if no coords
    HAVEL_REGISTER_FUNCTION(api, "pixel.get", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getPixelService();
        if (!svc) return Value::makeNull();
        int px = 0;
        int py = 0;
        if (args.size() >= 2) {
            px = toInt(args[0]);
            py = toInt(args[1]);
        } else {
            auto pos = currentMousePos();
            px = pos.first;
            py = pos.second;
        }
        auto color = svc->getPixel(px, py);
        return colorToValue(api, color);
    });

    // pixel.match - at cursor if first arg is color; explicit if first two are coords
    HAVEL_REGISTER_FUNCTION(api, "pixel.match", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);

        int px = 0;
        int py = 0;
        int argIdx = 0;
        if (args[0].isStringId() || args[0].isStringValId() || args[0].isObjectId()) {
            auto pos = currentMousePos();
            px = pos.first;
            py = pos.second;
            argIdx = 0;
        } else if (args.size() >= 3) {
            px = toInt(args[0]);
            py = toInt(args[1]);
            argIdx = 2;
        } else {
            return Value::makeBool(false);
        }

        int tolerance = args.size() > static_cast<size_t>(argIdx) + 1 ? toInt(args[argIdx + 1], 0) : 0;

        if (args[argIdx].isStringId() || args[argIdx].isStringValId()) {
            return Value::makeBool(svc->pixelMatch(px, py, toString(api, args[argIdx]), tolerance));
        }
        auto color = parseColor(api, args[argIdx]);
        return Value::makeBool(svc->pixelMatch(px, py, color, tolerance));
    });

    // pixel.wait - at cursor if first arg is color; explicit if first two are coords
    HAVEL_REGISTER_FUNCTION(api, "pixel.wait", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.empty()) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);

        int px = 0;
        int py = 0;
        int argIdx = 0;
        if (args[0].isStringId() || args[0].isStringValId() || args[0].isObjectId()) {
            auto pos = currentMousePos();
            px = pos.first;
            py = pos.second;
            argIdx = 0;
        } else if (args.size() >= 3) {
            px = toInt(args[0]);
            py = toInt(args[1]);
            argIdx = 2;
        } else {
            return Value::makeBool(false);
        }

        int tolerance = args.size() > static_cast<size_t>(argIdx) + 1 ? toInt(args[argIdx + 1], 0) : 0;
        int timeout = args.size() > static_cast<size_t>(argIdx) + 2 ? toInt(args[argIdx + 2], 5000) : 5000;

        if (args[argIdx].isStringId() || args[argIdx].isStringValId()) {
            return Value::makeBool(svc->waitPixel(px, py, toString(api, args[argIdx]), tolerance, timeout));
        }
        auto color = parseColor(api, args[argIdx]);
        return Value::makeBool(svc->waitPixel(px, py, color, tolerance, timeout));
    });

    // pixel.findImage(path, region?, threshold?) -> {found, x, y, w, h, confidence, centerX, centerY}
    HAVEL_REGISTER_FUNCTION(api, "pixel.findImage", [api](const auto& rawArgs) {
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
    HAVEL_REGISTER_FUNCTION(api, "pixel.waitImage", [api](const auto& rawArgs) {
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
    HAVEL_REGISTER_FUNCTION(api, "pixel.existsImage", [api](const auto& rawArgs) {
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
    HAVEL_REGISTER_FUNCTION(api, "pixel.countImage", [api](const auto& rawArgs) {
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
    HAVEL_REGISTER_FUNCTION(api, "pixel.findAllImage", [api](const auto& rawArgs) {
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
    HAVEL_REGISTER_FUNCTION(api, "pixel.readText", [api](const auto& rawArgs) {
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
    HAVEL_REGISTER_FUNCTION(api, "pixel.capture", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeBool(false);
        auto svc = getPixelService();
        if (!svc) return Value::makeBool(false);
        std::string path = toString(api, args[0]);
        return Value::makeBool(svc->captureScreen(path));
    });

    // pixel.captureRegion(x, y, w, h, path) -> bool
    HAVEL_REGISTER_FUNCTION(api, "pixel.captureRegion", [api](const auto& rawArgs) {
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

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(pixel, "1.0.0", "Pixel operations module",
    havel::modules::registerPixelModule(*api);
)
#endif
