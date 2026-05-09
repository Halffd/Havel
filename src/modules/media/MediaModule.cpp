/*
 * MediaModule.cpp - Media playback control module for Havel bytecode VM
 *
 * Exposes MPRIS/D-Bus media player control via MediaService.
 *
 * Functions:
 *   media.playPause() -> null
 *   media.play() -> null
 *   media.pause() -> null
 *   media.stop() -> null
 *   media.next() -> null
 *   media.previous() -> null
 *   media.getVolume() -> double
 *   media.setVolume(vol) -> null
 *   media.getActivePlayer() -> string
 *   media.setActivePlayer(name) -> null
 *   media.getAvailablePlayers() -> [string...]
 *   media.hasPlayer() -> bool
 */
#include "MediaModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/media/MediaService.hpp"
#include "utils/Logger.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;
using host::MediaService;

static const char* MEDIA_MODULE_MARKER = "__media_module";

static bool isMediaModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, MEDIA_MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isMediaModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

static std::shared_ptr<MediaService> getMediaService() {
    auto& registry = host::ServiceRegistry::instance();
    auto svc = registry.get<MediaService>();
    if (!svc) {
        debug("MediaModule: MediaService not available");
    }
    return svc;
}

static std::string toString(const VMApi& api, const Value& v) {
    if (v.isStringId() || v.isStringValId()) return api.toString(v);
    if (v.isNull()) return "";
    if (v.isInt()) return std::to_string(v.asInt());
    if (v.isDouble()) return std::to_string(v.asDouble());
    if (v.isBool()) return v.asBool() ? "true" : "false";
    return "";
}

static double toDouble(const Value& v, double def = 0.0) {
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
    return def;
}

void registerMediaModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("Media");

    HAVEL_REGISTER_FUNCTION(api, "media.playPause", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        try { svc->playPause(); } catch (const std::exception& e) {
            debug("media.playPause error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.play", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        try { svc->play(); } catch (const std::exception& e) {
            debug("media.play error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.pause", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        try { svc->pause(); } catch (const std::exception& e) {
            debug("media.pause error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.stop", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        try { svc->stop(); } catch (const std::exception& e) {
            debug("media.stop error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.next", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        try { svc->next(); } catch (const std::exception& e) {
            debug("media.next error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.previous", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        try { svc->previous(); } catch (const std::exception& e) {
            debug("media.previous error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.getVolume", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeDouble(0.0);
        try {
            return Value::makeDouble(svc->getVolume());
        } catch (const std::exception& e) {
            debug("media.getVolume error: {}", e.what());
            return Value::makeDouble(0.0);
        }
    });

    HAVEL_REGISTER_FUNCTION(api, "media.setVolume", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeNull();
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        double vol = toDouble(args[0], 0.5);
        try { svc->setVolume(vol); } catch (const std::exception& e) {
            debug("media.setVolume error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.getActivePlayer", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return api.makeString("");
        try {
            return api.makeString(svc->getActivePlayer());
        } catch (const std::exception& e) {
            debug("media.getActivePlayer error: {}", e.what());
            return api.makeString("");
        }
    });

    HAVEL_REGISTER_FUNCTION(api, "media.setActivePlayer", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        if (args.size() < 1) return Value::makeNull();
        auto svc = getMediaService();
        if (!svc) return Value::makeNull();
        std::string name = toString(api, args[0]);
        try { svc->setActivePlayer(name); } catch (const std::exception& e) {
            debug("media.setActivePlayer error: {}", e.what());
        }
        return Value::makeNull();
    });

    HAVEL_REGISTER_FUNCTION(api, "media.getAvailablePlayers", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return api.makeArray();
        try {
            auto players = svc->getAvailablePlayers();
            auto arr = api.makeArray();
            for (const auto& p : players) {
                api.push(arr, api.makeString(p));
            }
            return arr;
        } catch (const std::exception& e) {
            debug("media.getAvailablePlayers error: {}", e.what());
            return api.makeArray();
        }
    });

    HAVEL_REGISTER_FUNCTION(api, "media.hasPlayer", [api](const auto& rawArgs) {
        auto args = stripReceiver(api, rawArgs);
        auto svc = getMediaService();
        if (!svc) return Value::makeBool(false);
        try {
            return Value::makeBool(svc->hasPlayer());
        } catch (const std::exception& e) {
            return Value::makeBool(false);
        }
    });

    auto mediaObj = api.makeObject();
    api.setGlobal("media", mediaObj);
    api.setField(mediaObj, MEDIA_MODULE_MARKER, Value::makeBool(true));
    api.setField(mediaObj, "playPause", api.makeFunctionRef("media.playPause"));
    api.setField(mediaObj, "play", api.makeFunctionRef("media.play"));
    api.setField(mediaObj, "pause", api.makeFunctionRef("media.pause"));
    api.setField(mediaObj, "stop", api.makeFunctionRef("media.stop"));
    api.setField(mediaObj, "next", api.makeFunctionRef("media.next"));
    api.setField(mediaObj, "previous", api.makeFunctionRef("media.previous"));
    api.setField(mediaObj, "getVolume", api.makeFunctionRef("media.getVolume"));
    api.setField(mediaObj, "setVolume", api.makeFunctionRef("media.setVolume"));
    api.setField(mediaObj, "getActivePlayer", api.makeFunctionRef("media.getActivePlayer"));
    api.setField(mediaObj, "setActivePlayer", api.makeFunctionRef("media.setActivePlayer"));
    api.setField(mediaObj, "getAvailablePlayers", api.makeFunctionRef("media.getAvailablePlayers"));
    api.setField(mediaObj, "hasPlayer", api.makeFunctionRef("media.hasPlayer"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules
