/*
 * ModeModule.cpp
 *
 * Mode system module for Havel language.
 * Exposes mode.* API for scripts.
 */
#include "ModeModule.hpp"
#include "../../core/ModeManager.hpp"
#include <memory>

namespace havel::modules {

void registerModeModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  auto modeObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // mode.current - Get current mode name
  (*modeObj)["current"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (modeManager) {
          std::string currentMode = modeManager->getCurrentMode();
          return HavelValue(currentMode.empty() ? "default" : currentMode);
        }
        return HavelValue("default");
      }));

  // mode.get() - Alias for mode.current
  (*modeObj)["get"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (modeManager) {
          std::string currentMode = modeManager->getCurrentMode();
          return HavelValue(currentMode.empty() ? "default" : currentMode);
        }
        return HavelValue("default");
      }));

  // mode.previous - Get previous mode name
  (*modeObj)["previous"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (modeManager) {
          return HavelValue(modeManager->getPreviousMode());
        }
        return HavelValue("default");
      }));

  // mode.time(name) - Get time spent in mode
  (*modeObj)["time"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (!modeManager) {
          return HavelRuntimeError("ModeManager not available");
        }
        if (args.empty()) {
          // Return time in current mode
          auto ms = modeManager->getModeTime(modeManager->getCurrentMode());
          return HavelValue(static_cast<double>(ms.count()) /
                            1000.0); // Return seconds
        }
        std::string modeName = args[0].asString();
        auto ms = modeManager->getModeTime(modeName);
        return HavelValue(static_cast<double>(ms.count()) /
                          1000.0); // Return seconds
      }));

  // mode.transitions(name) - Get number of transitions
  (*modeObj)["transitions"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (!modeManager) {
          return HavelRuntimeError("ModeManager not available");
        }
        if (args.empty()) {
          return HavelValue(
              modeManager->getModeTransitions(modeManager->getCurrentMode()));
        }
        std::string modeName = args[0].asString();
        return HavelValue(
            static_cast<double>(modeManager->getModeTransitions(modeName)));
      }));

  // mode.set(name) - Set mode explicitly
  (*modeObj)["set"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (!modeManager) {
          return HavelRuntimeError("ModeManager not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("mode.set() requires mode name");
        }
        std::string modeName = args[0].asString();
        modeManager->setMode(modeName);
        return HavelValue(true);
      }));

  // mode.list() - List all defined modes
  (*modeObj)["list"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (!modeManager) {
          return HavelRuntimeError("ModeManager not available");
        }
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto &mode : modeManager->getModes()) {
          resultArray->push_back(HavelValue(mode.name));
        }
        return HavelValue(resultArray);
      }));

  // mode.isSignal(name) - Check if signal is active
  (*modeObj)["isSignal"] = HavelValue(BuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        auto modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;
        if (!modeManager) {
          return HavelRuntimeError("ModeManager not available");
        }
        if (args.empty()) {
          return HavelRuntimeError("mode.isSignal() requires signal name");
        }
        std::string signalName = args[0].asString();
        return HavelValue(modeManager->isSignalActive(signalName));
      }));

  // MIGRATED TO BYTECODE VM: env.Define("mode", HavelValue(modeObj));
}

} // namespace havel::modules
