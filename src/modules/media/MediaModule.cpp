/*
 * MediaModule.cpp
 * 
 * Media control module for Havel language.
 * Host binding - connects language to MPVController via HavelApp.
 */
#include "../../host/HostContext.hpp"
#include "../runtime/Environment.hpp"
#include "gui/HavelApp.hpp"

namespace havel::modules {

void registerMediaModule(Environment& env, HostContext&) {
    auto app = HavelApp::instance;
    if (!app) {
        return;  // Skip if HavelApp not available
    }
    
    // Create media module object
    auto mediaObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to check if MPV is available
    auto checkMpv = [app]() -> bool {
        return app && app->mpv;
    };
    
    // =========================================================================
    // Media control functions
    // =========================================================================
    
    (*mediaObj)["play"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->PlayPause();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mediaObj)["pause"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->PlayPause();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mediaObj)["stop"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->Stop();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mediaObj)["next"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->Next();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mediaObj)["previous"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->Previous();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    // =========================================================================
    // MPV Controller functions
    // =========================================================================
    
    auto mpvObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*mpvObj)["volumeUp"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->VolumeUp();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mpvObj)["volumeDown"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            app->mpv->VolumeDown();
            return HavelValue(true);
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mpvObj)["setVolume"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>& args) -> HavelResult {
        if (!checkMpv()) {
            return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
            return HavelRuntimeError("setVolume() requires volume value");
        }
        double volume = args[0].asNumber();
        app->mpv->SetVolume(volume);
        return HavelValue(true);
    }));
    
    (*mpvObj)["getVolume"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            return HavelValue(app->mpv->GetVolume());
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mpvObj)["seek"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>& args) -> HavelResult {
        if (!checkMpv()) {
            return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
            return HavelRuntimeError("seek() requires position");
        }
        double position = args[0].asNumber();
        app->mpv->Seek(position);
        return HavelValue(true);
    }));
    
    (*mpvObj)["getPosition"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            return HavelValue(app->mpv->GetPosition());
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mpvObj)["getDuration"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>&) -> HavelResult {
        if (checkMpv()) {
            return HavelValue(app->mpv->GetDuration());
        }
        return HavelRuntimeError("MPVController not available");
    }));
    
    (*mpvObj)["loadFile"] = HavelValue(BuiltinFunction([checkMpv, app](const std::vector<HavelValue>& args) -> HavelResult {
        if (!checkMpv()) {
            return HavelRuntimeError("MPVController not available");
        }
        if (args.empty()) {
            return HavelRuntimeError("loadFile() requires file path");
        }
        std::string path = args[0].asString();
        app->mpv->LoadFile(path);
        return HavelValue(true);
    }));
    
    // Register modules
    env.Define("media", HavelValue(mediaObj));
    env.Define("mpvcontroller", HavelValue(mpvObj));
}

} // namespace havel::modules
