/*
 * AudioModule.cpp
 *
 * Audio management module for Havel language.
 * Thin binding layer - delegates to AudioService.
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../host/audio/AudioService.hpp"

namespace havel::modules {

void registerAudioModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  (void)hostAPI;  // Services don't use hostAPI directly
  
  // Get AudioService from registry - FAIL if not registered
  auto& registry = host::ServiceRegistry::instance();
  auto audioService = registry.get<host::AudioService>();
  
  if (!audioService) {
    throw std::runtime_error("AudioService not registered. Call initializeServiceRegistry() first.");
  }

  // Create audio module object
  auto audioObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // Volume control - thin wrappers over AudioService
  // =========================================================================

  (*audioObj)["getVolume"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(audioService->getVolume());
      }));

  (*audioObj)["setVolume"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("setVolume() requires volume value (0.0-1.0)");
        }
        audioService->setVolume(args[0].asNumber());
        return HavelValue(nullptr);
      }));

  (*audioObj)["increaseVolume"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &args) -> HavelResult {
        double amount = args.empty() ? 0.05 : args[0].asNumber();
        audioService->increaseVolume(amount);
        return HavelValue(nullptr);
      }));

  (*audioObj)["decreaseVolume"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &args) -> HavelResult {
        double amount = args.empty() ? 0.05 : args[0].asNumber();
        audioService->decreaseVolume(amount);
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Mute control - thin wrappers over AudioService
  // =========================================================================

  (*audioObj)["toggleMute"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &) -> HavelResult {
        audioService->toggleMute();
        return HavelValue(nullptr);
      }));

  (*audioObj)["setMute"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("setMute() requires boolean value");
        }
        audioService->setMute(args[0].asBool());
        return HavelValue(nullptr);
      }));

  (*audioObj)["isMuted"] = HavelValue(BuiltinFunction(
      [audioService](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(audioService->isMuted());
      }));

  // Register audio module
  // MIGRATED TO BYTECODE VM: env.Define("audio", HavelValue(audioObj));
}

} // namespace havel::modules
