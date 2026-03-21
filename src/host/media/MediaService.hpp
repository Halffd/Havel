/*
 * MediaService.hpp - Media playback service (stub, header-only)
 * Full implementation requires MPV
 */
#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::host {

using havel::HavelValue;
using havel::HavelRuntimeError;
// BuiltinFunction is already available from Environment.hpp

class MediaService {
public:
    bool play(const std::string& path) { (void)path; return false; }
    void pause() {}
    void resume() {}
    void togglePause() {}
    void stop() {}
    bool isPlaying() const { return false; }
    bool isPaused() const { return false; }
    int getVolume() const { return 50; }
    void setVolume(int) {}
    void setMute(bool) {}
    bool isMuted() const { return false; }
    double getPosition() const { return 0.0; }
    void setPosition(double) {}
    double getDuration() const { return 0.0; }
    double getProgress() const { return 0.0; }
    void next() {}
    void previous() {}
    std::string getTitle() const { return ""; }
};

// Module registration (co-located)
class Environment;
class IHostAPI;
struct HavelValue;
struct HavelRuntimeError;
template<typename T> struct BuiltinFunction;

inline void registerMediaModule(havel::Environment& env, std::shared_ptr<havel::IHostAPI>) {
    auto mediaService = std::make_shared<MediaService>();
    auto mediaObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*mediaObj)["play"] = HavelValue(::havel::BuiltinFunction(
        [mediaService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) return HavelRuntimeError("play requires path");
            return HavelValue(mediaService->play(args[0].asString()));
        }));
    
    (*mediaObj)["pause"] = HavelValue(::havel::BuiltinFunction([mediaService](const std::vector<HavelValue>&) -> HavelResult {
        mediaService->pause(); return HavelValue(nullptr);
    }));
    
    (*mediaObj)["stop"] = HavelValue(::havel::BuiltinFunction([mediaService](const std::vector<HavelValue>&) -> HavelResult {
        mediaService->stop(); return HavelValue(nullptr);
    }));
    
    (*mediaObj)["getVolume"] = HavelValue(::havel::BuiltinFunction([mediaService](const std::vector<HavelValue>&) -> HavelResult {
        return HavelValue(static_cast<double>(mediaService->getVolume()));
    }));
    
    (*mediaObj)["setVolume"] = HavelValue(::havel::BuiltinFunction(
        [mediaService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) return HavelRuntimeError("setVolume requires volume");
            mediaService->setVolume(static_cast<int>(args[0].asNumber()));
            return HavelValue(nullptr);
        }));
    
    env.Define("media", HavelValue(mediaObj));
}

} // namespace havel::host
