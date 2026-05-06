/*
 * ImageModule.cpp - Image processing module for Havel bytecode VM
 *
 * Exposes OpenCV image operations via ImageService.
 * Images are opaque handles (integers) — lightweight, no per-pixel VM objects.
 *
 * NOTE: Uses api.registerFunction() directly instead of HAVEL_REGISTER_FUNCTION
 * macro because lambdas with comma-separated declarations break the preprocessor.
 */
#include "ImageModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/image/ImageService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::ImageService;

static const char* IMAGE_MODULE_MARKER = "__image_module";

static bool isImageModuleObject(VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, IMAGE_MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isImageModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static std::shared_ptr<ImageService> getImageService() {
    auto& registry = host::ServiceRegistry::instance();
    auto svc = registry.get<ImageService>();
    if (!svc) {
        debug("ImageModule: ImageService not available");
    }
    return svc;
}

static int toInt(const Value& v, int def = 0) {
    if (v.isInt()) return static_cast<int>(v.asInt());
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return def;
}

static double toDouble(const Value& v, double def = 0.0) {
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
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

#define REG(name, lambda) do { \
    api.registerFunction(name, lambda); \
    if (g_currentModule) g_currentModule->functionCount++; \
} while(0)

void registerImageModule(VMApi& api) {
    HAVEL_BEGIN_MODULE("Image");

    REG("image.load", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        std::string path = toString(api, args[0]);
        return Value::makeInt(svc->load(path));
    });

    REG("image.save", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeBool(false);
        auto svc = getImageService();
        if (!svc) return Value::makeBool(false);
        int64_t handle = toInt(args[0]);
        std::string path = toString(api, args[1]);
        return Value::makeBool(svc->save(handle, path));
    });

    REG("image.release", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeNull();
        auto svc = getImageService();
        if (!svc) return Value::makeNull();
        svc->release(toInt(args[0]));
        return Value::makeNull();
    });

    REG("image.info", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return api.makeNull();
        auto svc = getImageService();
        if (!svc) return api.makeNull();
        auto inf = svc->info(toInt(args[0]));
        auto obj = api.makeObject();
        api.setField(obj, "width", Value::makeInt(inf.width));
        api.setField(obj, "height", Value::makeInt(inf.height));
        api.setField(obj, "channels", Value::makeInt(inf.channels));
        return obj;
    });

    REG("image.width", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        return Value::makeInt(svc->width(toInt(args[0])));
    });

    REG("image.height", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        return Value::makeInt(svc->height(toInt(args[0])));
    });

    REG("image.channels", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        return Value::makeInt(svc->channels(toInt(args[0])));
    });

    REG("image.resize", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 3) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int64_t handle = toInt(args[0]);
        int w = toInt(args[1]);
        int h = toInt(args[2]);
        return Value::makeInt(svc->resize(handle, w, h));
    });

    REG("image.crop", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 5) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int64_t handle = toInt(args[0]);
        int x = toInt(args[1]);
        int y = toInt(args[2]);
        int w = toInt(args[3]);
        int h = toInt(args[4]);
        return Value::makeInt(svc->crop(handle, x, y, w, h));
    });

    REG("image.rotate", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        return Value::makeInt(svc->rotate(toInt(args[0]), toDouble(args[1])));
    });

    REG("image.blur", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        return Value::makeInt(svc->blur(toInt(args[0]), toInt(args[1])));
    });

    REG("image.grayscale", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        return Value::makeInt(svc->grayscale(toInt(args[0])));
    });

    REG("image.edges", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int64_t handle = toInt(args[0]);
        double t1 = args.size() > 1 ? toDouble(args[1], 100.0) : 100.0;
        double t2 = args.size() > 2 ? toDouble(args[2], 200.0) : 200.0;
        return Value::makeInt(svc->edges(handle, t1, t2));
    });

    REG("image.threshold", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int64_t handle = toInt(args[0]);
        double thresh = toDouble(args[1]);
        double maxval = args.size() > 2 ? toDouble(args[2], 255.0) : 255.0;
        int type = args.size() > 3 ? toInt(args[3], 0) : 0;
        return Value::makeInt(svc->threshold(handle, thresh, maxval, type));
    });

    REG("image.flip", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int code = args.size() > 1 ? toInt(args[1], 0) : 0;
        return Value::makeInt(svc->flip(toInt(args[0]), code));
    });

    REG("image.blend", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 3) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int64_t h1 = toInt(args[0]);
        int64_t h2 = toInt(args[1]);
        double alpha = toDouble(args[2], 0.5);
        return Value::makeInt(svc->blend(h1, h2, alpha));
    });

    REG("image.getPixel", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 3) return api.makeNull();
        auto svc = getImageService();
        if (!svc) return api.makeNull();
        int64_t handle = toInt(args[0]);
        int x = toInt(args[1]);
        int y = toInt(args[2]);
        int r; int g; int b; int a;
        if (!svc->getPixel(handle, x, y, r, g, b, a)) return api.makeNull();
        auto obj = api.makeObject();
        api.setField(obj, "r", Value::makeInt(r));
        api.setField(obj, "g", Value::makeInt(g));
        api.setField(obj, "b", Value::makeInt(b));
        api.setField(obj, "a", Value::makeInt(a));
        return obj;
    });

    REG("image.setPixel", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 6) return Value::makeBool(false);
        auto svc = getImageService();
        if (!svc) return Value::makeBool(false);
        int64_t handle = toInt(args[0]);
        int x = toInt(args[1]);
        int y = toInt(args[2]);
        int r = toInt(args[3]);
        int g = toInt(args[4]);
        int b = toInt(args[5]);
        int a = args.size() > 6 ? toInt(args[6], 255) : 255;
        return Value::makeBool(svc->setPixel(handle, x, y, r, g, b, a));
    });

    REG("image.create", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 2) return Value::makeInt(0);
        auto svc = getImageService();
        if (!svc) return Value::makeInt(0);
        int w = toInt(args[0]);
        int h = toInt(args[1]);
        int r = args.size() > 2 ? toInt(args[2], 0) : 0;
        int g = args.size() > 3 ? toInt(args[3], 0) : 0;
        int b = args.size() > 4 ? toInt(args[4], 0) : 0;
        int a = args.size() > 5 ? toInt(args[5], 255) : 255;
        return Value::makeInt(svc->create(w, h, r, g, b, a));
    });

    REG("image.toRGBA", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return api.makeArray();
        auto svc = getImageService();
        if (!svc) return api.makeArray();
        auto rgba = svc->toRGBA(toInt(args[0]));
        auto arr = api.makeArray();
        for (uint8_t v : rgba) {
            api.push(arr, Value::makeInt(v));
        }
        return arr;
    });

    REG("image.matchTemplate", [&api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto notFound = [&api]() {
            auto obj = api.makeObject();
            api.setField(obj, "found", Value::makeBool(false));
            return obj;
        };
        if (args.size() < 2) return notFound();
        auto svc = getImageService();
        if (!svc) return notFound();
        int64_t screen = toInt(args[0]);
        int64_t tmpl = toInt(args[1]);
        float threshold = args.size() > 2 ? toFloat(args[2], 0.9f) : 0.9f;
        int outX; int outY; float outConf;
        auto result = svc->matchTemplate(screen, tmpl, threshold, outX, outY, outConf);
        auto obj = api.makeObject();
        api.setField(obj, "found", Value::makeBool(result > 0));
        if (result > 0) {
            api.setField(obj, "x", Value::makeInt(outX));
            api.setField(obj, "y", Value::makeInt(outY));
            api.setField(obj, "confidence", Value::makeDouble(outConf));
        }
        return obj;
    });

#undef REG

    auto imageObj = api.makeObject();
    api.setGlobal("image", imageObj);
    api.setField(imageObj, IMAGE_MODULE_MARKER, Value::makeBool(true));
    api.setField(imageObj, "load", api.makeFunctionRef("image.load"));
    api.setField(imageObj, "save", api.makeFunctionRef("image.save"));
    api.setField(imageObj, "release", api.makeFunctionRef("image.release"));
    api.setField(imageObj, "info", api.makeFunctionRef("image.info"));
    api.setField(imageObj, "width", api.makeFunctionRef("image.width"));
    api.setField(imageObj, "height", api.makeFunctionRef("image.height"));
    api.setField(imageObj, "channels", api.makeFunctionRef("image.channels"));
    api.setField(imageObj, "resize", api.makeFunctionRef("image.resize"));
    api.setField(imageObj, "crop", api.makeFunctionRef("image.crop"));
    api.setField(imageObj, "rotate", api.makeFunctionRef("image.rotate"));
    api.setField(imageObj, "blur", api.makeFunctionRef("image.blur"));
    api.setField(imageObj, "grayscale", api.makeFunctionRef("image.grayscale"));
    api.setField(imageObj, "edges", api.makeFunctionRef("image.edges"));
    api.setField(imageObj, "threshold", api.makeFunctionRef("image.threshold"));
    api.setField(imageObj, "flip", api.makeFunctionRef("image.flip"));
    api.setField(imageObj, "blend", api.makeFunctionRef("image.blend"));
    api.setField(imageObj, "getPixel", api.makeFunctionRef("image.getPixel"));
    api.setField(imageObj, "setPixel", api.makeFunctionRef("image.setPixel"));
    api.setField(imageObj, "create", api.makeFunctionRef("image.create"));
    api.setField(imageObj, "toRGBA", api.makeFunctionRef("image.toRGBA"));
    api.setField(imageObj, "matchTemplate", api.makeFunctionRef("image.matchTemplate"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules
