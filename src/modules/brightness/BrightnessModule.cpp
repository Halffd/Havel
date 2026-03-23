/*
 * BrightnessModule.cpp
 *
 * Brightness management module for Havel language.
 * Thin binding layer - delegates to BrightnessService.
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../host/brightness/BrightnessService.hpp"

namespace havel::modules {

void registerBrightnessModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  (void)hostAPI;  // Services don't use hostAPI directly
  
  // Get BrightnessService from registry - FAIL if not registered
  auto& registry = host::ServiceRegistry::instance();
  auto brightnessService = registry.get<host::BrightnessService>();
  
  if (!brightnessService) {
    throw std::runtime_error("BrightnessService not registered. Call initializeServiceRegistry() first.");
  }

  // Create brightness module object
  auto brightnessObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // Brightness get/set - thin wrappers over BrightnessService
  // =========================================================================

  (*brightnessObj)["getBrightness"] = HavelValue(BuiltinFunction(
      [brightnessService](const std::vector<HavelValue> &args) -> HavelResult {
        int monitorIndex = args.empty() ? -1 : static_cast<int>(args[0].asNumber());
        return HavelValue(brightnessService->getBrightness(monitorIndex));
      }));

  (*brightnessObj)["setBrightness"] = HavelValue(BuiltinFunction(
      [brightnessService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("setBrightness() requires brightness value");
        }
        if (args.size() >= 2) {
          int monitorIndex = static_cast<int>(args[0].asNumber());
          double brightness = args[1].asNumber();
          brightnessService->setBrightness(brightness, monitorIndex);
        } else {
          brightnessService->setBrightness(args[0].asNumber());
        }
        return HavelValue(nullptr);
      }));

  (*brightnessObj)["increaseBrightness"] = HavelValue(BuiltinFunction(
      [brightnessService](const std::vector<HavelValue> &args) -> HavelResult {
        double step = args.empty() ? 0.1 : args[0].asNumber();
        if (args.size() >= 2) {
          int monitorIndex = static_cast<int>(args[0].asNumber());
          brightnessService->increaseBrightness(step, monitorIndex);
        } else {
          brightnessService->increaseBrightness(step);
        }
        return HavelValue(nullptr);
      }));

  (*brightnessObj)["decreaseBrightness"] = HavelValue(BuiltinFunction(
      [brightnessService](const std::vector<HavelValue> &args) -> HavelResult {
        double step = args.empty() ? 0.1 : args[0].asNumber();
        if (args.size() >= 2) {
          int monitorIndex = static_cast<int>(args[0].asNumber());
          brightnessService->decreaseBrightness(step, monitorIndex);
        } else {
          brightnessService->decreaseBrightness(step);
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Temperature control - thin wrappers over BrightnessService
  // =========================================================================

  (*brightnessObj)["getTemperature"] = HavelValue(BuiltinFunction(
      [brightnessService](const std::vector<HavelValue> &args) -> HavelResult {
        int monitorIndex = args.empty() ? -1 : static_cast<int>(args[0].asNumber());
        return HavelValue(static_cast<double>(brightnessService->getTemperature(monitorIndex)));
      }));

  (*brightnessObj)["setTemperature"] = HavelValue(BuiltinFunction(
      [brightnessService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("setTemperature() requires temperature value");
        }
        int temperature = static_cast<int>(args[0].asNumber());
        if (args.size() >= 2) {
          int monitorIndex = static_cast<int>(args[0].asNumber());
          brightnessService->setTemperature(temperature, monitorIndex);
        } else {
          brightnessService->setTemperature(temperature);
        }
        return HavelValue(nullptr);
      }));

  // Register brightness module
  // MIGRATED TO BYTECODE VM: env.Define("brightness", HavelValue(brightnessObj));
}

} // namespace havel::modules
