/*
 * AudioModule.cpp
 * 
 * Audio management module for Havel language.
 * Host binding - connects language to AudioManager.
 */
#include "../../host/HostContext.hpp"
#include "../runtime/Environment.hpp"
#include "media/AudioManager.hpp"

namespace havel::modules {

void registerAudioModule(Environment& env, HostContext& ctx) {
    if (!ctx.isValid() || !ctx.audioManager) {
        return;  // Skip if no audio manager available
    }
    
    auto& am = *ctx.audioManager;
    
    // Create audio module object
    auto audioObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // =========================================================================
    // Volume control functions
    // =========================================================================
    
    (*audioObj)["getVolume"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(am.getVolume());
    }));
    
    (*audioObj)["setVolume"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setVolume() requires volume value (0.0-1.0)");
        }
        double volume = args[0].asNumber();
        am.setVolume(volume);
        return HavelValue(nullptr);
    }));
    
    (*audioObj)["increaseVolume"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>& args) -> HavelResult {
        double amount = args.empty() ? 0.05 : args[0].asNumber();
        am.increaseVolume(amount);
        return HavelValue(nullptr);
    }));
    
    (*audioObj)["decreaseVolume"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>& args) -> HavelResult {
        double amount = args.empty() ? 0.05 : args[0].asNumber();
        am.decreaseVolume(amount);
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // Mute control functions
    // =========================================================================
    
    (*audioObj)["toggleMute"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>&) -> HavelResult {
        am.toggleMute();
        return HavelValue(nullptr);
    }));
    
    (*audioObj)["setMute"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("setMute() requires boolean value");
        }
        bool muted = args[0].asBool();
        am.setMute(muted);
        return HavelValue(nullptr);
    }));
    
    (*audioObj)["isMuted"] = HavelValue(BuiltinFunction([&am](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(am.isMuted());
    }));
    
    // Register audio module
    env.Define("audio", HavelValue(audioObj));
}

} // namespace havel::modules
