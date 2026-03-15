/*
 * ModeModule.cpp
 *
 * Mode system module for Havel language.
 * Provides mode switching for conditional hotkeys.
 */
#include "ModeModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/HotkeyManager.hpp"

namespace havel::modules {

void registerModeModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    // Create mode object
    auto modeObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // =========================================================================
    // mode.get() - Get current mode
    // =========================================================================

    (*modeObj)["get"] = HavelValue(BuiltinFunction([&env](const std::vector<HavelValue>&) -> HavelResult {
        auto currentModeOpt = env.Get("__current_mode__");

        if (currentModeOpt && currentModeOpt->is<std::string>()) {
            return HavelValue(currentModeOpt->get<std::string>());
        }

        return HavelValue(std::string("default"));
    }));

    // =========================================================================
    // mode.set(newMode) - Set current mode and update conditional hotkeys
    // =========================================================================

    (*modeObj)["set"] = HavelValue(BuiltinFunction([hostAPI, &env](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("mode.set() requires mode name");
        }

        std::string newMode = args[0].is<std::string>() ?
            args[0].get<std::string>() : "default";

        // Get previous mode
        auto currentModeOpt = env.Get("__current_mode__");
        std::string currentMode = "default";

        if (currentModeOpt && currentModeOpt->is<std::string>()) {
            currentMode = currentModeOpt->get<std::string>();
        }

        // Store previous mode
        env.Define("__previous_mode__", HavelValue(currentMode));

        // Set new mode in environment
        env.Define("__current_mode__", HavelValue(newMode));

        // Set new mode in ConditionalHotkeyManager
        if (auto* hm = hostAPI->GetHotkeyManager()) {
            hm->setMode(newMode);
        }

        // Update conditional hotkeys to reflect mode change
        if (auto* hm = hostAPI->GetHotkeyManager()) {
            hm->updateAllConditionalHotkeys();
        }

        return HavelValue(nullptr);
    }));

    // =========================================================================
    // mode.previous() - Switch to previous mode and update conditional hotkeys
    // =========================================================================

    (*modeObj)["previous"] = HavelValue(BuiltinFunction([hostAPI, &env](const std::vector<HavelValue>&) -> HavelResult {
        auto currentModeOpt = env.Get("__current_mode__");
        auto previousModeOpt = env.Get("__previous_mode__");

        std::string currentMode = "default";
        std::string previousMode = "default";

        if (currentModeOpt && currentModeOpt->is<std::string>()) {
            currentMode = currentModeOpt->get<std::string>();
        }
        if (previousModeOpt && previousModeOpt->is<std::string>()) {
            previousMode = previousModeOpt->get<std::string>();
        }

        // Swap modes
        env.Define("__previous_mode__", HavelValue(currentMode));
        env.Define("__current_mode__", HavelValue(previousMode));

        // Set mode in ConditionalHotkeyManager
        if (auto* hm = hostAPI->GetHotkeyManager()) {
            hm->setMode(previousMode);
        }

        // Update conditional hotkeys to reflect mode change
        if (auto* hm = hostAPI->GetHotkeyManager()) {
            hm->updateAllConditionalHotkeys();
        }

        return HavelValue(nullptr);
    }));

    // =========================================================================
    // mode.is(modeName) - Check if in specific mode
    // =========================================================================

    (*modeObj)["is"] = HavelValue(BuiltinFunction([&env](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("mode.is() requires mode name");
        }

        std::string checkMode = args[0].is<std::string>() ?
            args[0].get<std::string>() : "";

        auto currentModeOpt = env.Get("__current_mode__");

        if (currentModeOpt && currentModeOpt->is<std::string>()) {
            std::string currentMode = currentModeOpt->get<std::string>();
            return HavelValue(currentMode == checkMode);
        }

        return HavelValue(checkMode == "default");
    }));

    // Register mode module
    env.Define("mode", HavelValue(modeObj));
}

} // namespace havel::modules
