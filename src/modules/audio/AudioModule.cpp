/*
 * AudioModule.cpp
 *
 * Audio management module for Havel language.
 * Host binding - connects language to AudioManager.
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "media/AudioManager.hpp"

namespace havel::modules {

void registerAudioModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    if (!hostAPI->GetIO() || !hostAPI->GetAudioManager()) {
        return;  // Skip if no audio manager available
    }

    // Create audio module object
    auto audioObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Helper to get audio manager with null check
    auto getAudioManager = [hostAPI]() -> AudioManager* {
        return hostAPI->GetAudioManager();
    };

    // =========================================================================
    // Volume control functions
    // =========================================================================

    (*audioObj)["getVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>&) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        return HavelValue(am->getVolume());
    }));

    (*audioObj)["setVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.empty()) {
            return HavelRuntimeError("setVolume() requires volume value (0.0-1.0)");
        }
        double volume = args[0].asNumber();
        am->setVolume(volume);
        return HavelValue(nullptr);
    }));

    (*audioObj)["increaseVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        double amount = args.empty() ? 0.05 : args[0].asNumber();
        am->increaseVolume(amount);
        return HavelValue(nullptr);
    }));

    (*audioObj)["decreaseVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        double amount = args.empty() ? 0.05 : args[0].asNumber();
        am->decreaseVolume(amount);
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // Mute control functions
    // =========================================================================

    (*audioObj)["toggleMute"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>&) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        am->toggleMute();
        return HavelValue(nullptr);
    }));

    (*audioObj)["setMute"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.empty()) {
            return HavelRuntimeError("setMute() requires boolean value");
        }
        bool muted = args[0].asBool();
        am->setMute(muted);
        return HavelValue(nullptr);
    }));

    (*audioObj)["isMuted"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>&) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        return HavelValue(am->isMuted());
    }));

    // =========================================================================
    // Device-specific functions
    // =========================================================================

    (*audioObj)["getDevices"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>&) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        auto arr = std::make_shared<std::vector<HavelValue>>();
        const auto& devices = am->getDevices();

        for (const auto& device : devices) {
            auto obj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["name"] = HavelValue(device.name);
            (*obj)["description"] = HavelValue(device.description);
            (*obj)["isDefault"] = HavelValue(device.isDefault);
            arr->push_back(HavelValue(obj));
        }

        return HavelValue(arr);
    }));

    (*audioObj)["setDeviceVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.size() < 2) {
            return HavelRuntimeError("audio.setDeviceVolume() requires (device, volume)");
        }

        std::string device = args[0].isString() ? args[0].asString() :
            std::to_string(static_cast<int>(args[0].asNumber()));
        double volume = args[1].asNumber();

        return HavelValue(am->setVolume(device, volume));
    }));

    (*audioObj)["getDeviceVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.empty()) {
            return HavelRuntimeError("audio.getDeviceVolume() requires device");
        }

        std::string device = args[0].isString() ? args[0].asString() :
            std::to_string(static_cast<int>(args[0].asNumber()));

        return HavelValue(am->getVolume(device));
    }));

    // =========================================================================
    // Per-application volume control (PipeWire/PulseAudio)
    // =========================================================================

    (*audioObj)["getApplications"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>&) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        auto arr = std::make_shared<std::vector<HavelValue>>();
        const auto& apps = am->getApplications();

        for (const auto& app : apps) {
            auto obj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*obj)["name"] = HavelValue(app.name);
            (*obj)["index"] = HavelValue(static_cast<double>(app.index));
            (*obj)["volume"] = HavelValue(am->getApplicationVolume(app.index));
            (*obj)["isMuted"] = HavelValue(app.isMuted);
            arr->push_back(HavelValue(obj));
        }

        return HavelValue(arr);
    }));

    (*audioObj)["setAppVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.size() < 2) {
            return HavelRuntimeError("audio.setAppVolume() requires (appName, volume)");
        }

        std::string appName = args[0].asString();
        double volume = args[1].asNumber();
        return HavelValue(am->setApplicationVolume(appName, volume));
    }));

    (*audioObj)["getAppVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.empty()) {
            return HavelRuntimeError("audio.getAppVolume() requires appName");
        }

        std::string appName = args[0].asString();
        return HavelValue(am->getApplicationVolume(appName));
    }));

    (*audioObj)["increaseAppVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.size() < 1) {
            return HavelRuntimeError("audio.increaseAppVolume() requires appName");
        }

        std::string appName = args[0].asString();
        double amount = args.size() >= 2 ? args[1].asNumber() : 0.05;
        return HavelValue(am->increaseApplicationVolume(appName, amount));
    }));

    (*audioObj)["decreaseAppVolume"] = HavelValue(BuiltinFunction([getAudioManager](const std::vector<HavelValue>& args) -> HavelResult {
        auto* am = getAudioManager();
        if (!am) return HavelRuntimeError("AudioManager not available");
        if (args.size() < 1) {
            return HavelRuntimeError("audio.decreaseAppVolume() requires appName");
        }

        std::string appName = args[0].asString();
        double amount = args.size() >= 2 ? args[1].asNumber() : 0.05;
        return HavelValue(am->decreaseApplicationVolume(appName, amount));
    }));

    // Register audio module
    env.Define("audio", HavelValue(audioObj));
}

} // namespace havel::modules
