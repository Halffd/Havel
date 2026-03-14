/*
 * HotkeyModule.cpp
 *
 * Hotkey management module for Havel language.
 * Provides hotkey control and overlay functions.
 */
#include "HotkeyModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/Interpreter.hpp"
#include "core/HotkeyManager.hpp"

namespace havel::modules {

// Global weak reference to interpreter for hotkey callbacks
static std::weak_ptr<Interpreter> g_hotkeyInterpreter;

void SetHotkeyInterpreter(std::weak_ptr<Interpreter> interp) {
    g_hotkeyInterpreter = interp;
    if (auto ptr = interp.lock()) {
        havel::info("Hotkey interpreter set to {}", (void*)ptr.get());
    } else {
        havel::warn("Hotkey interpreter cleared");
    }
}

void registerHotkeyModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    // Don't skip - register even if hotkeyManager is null initially
    // It will be set later via HostAPI::SetHotkeyManager()
    
    // Create hotkey object
    auto hotkeyObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // =========================================================================
    // hotkey.toggleOverlay() - Toggle fake desktop overlay
    // =========================================================================
    
    (*hotkeyObj)["toggleOverlay"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        hostAPI->GetHotkeyManager()->toggleFakeDesktopOverlay();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.showBlackOverlay() - Show black overlay
    // =========================================================================
    
    (*hotkeyObj)["showBlackOverlay"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        hostAPI->GetHotkeyManager()->showBlackOverlay();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.printActiveWindowInfo() - Print active window info
    // =========================================================================
    
    (*hotkeyObj)["printActiveWindowInfo"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        hostAPI->GetHotkeyManager()->printActiveWindowInfo();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.toggleWindowFocusTracking() - Toggle window focus tracking
    // =========================================================================
    
    (*hotkeyObj)["toggleWindowFocusTracking"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        hostAPI->GetHotkeyManager()->toggleWindowFocusTracking();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.updateConditional() - Update conditional hotkeys
    // =========================================================================
    
    (*hotkeyObj)["updateConditional"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        hostAPI->GetHotkeyManager()->updateAllConditionalHotkeys();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.clearAll() - Clear all registered hotkeys
    // =========================================================================
    
    (*hotkeyObj)["clearAll"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
        hostAPI->GetHotkeyManager()->clearAllHotkeys();
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // hotkey.list() - List all registered hotkeys
    // Note: getAllHotkeys() not available in HotkeyManager
    // =========================================================================

    (*hotkeyObj)["list"] = HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>&) -> HavelResult {
          // Suppress unused warning
        auto arr = std::make_shared<std::vector<HavelValue>>();
        // TODO: Implement when HotkeyManager supports listing hotkeys
        return HavelValue(arr);
    }));

    // =========================================================================
    // Global Hotkey() function - AHK-style hotkey registration
    // Hotkey(key, callback, condition?, conditionFalse?)
    // Returns: Hotkey object with id, key, condition, etc.
    // =========================================================================

    static std::atomic<int> nextHotkeyId{1000};

    env.Define("Hotkey", HavelValue(BuiltinFunction([hostAPI](const std::vector<HavelValue>& args) -> HavelResult {
        // Check if HotkeyManager is available
        auto* hm = hostAPI->GetHotkeyManager();
        if (!hm) {
            return HavelRuntimeError("Hotkey() requires HotkeyManager (not initialized yet)");
        }
        
        if (args.size() < 2) {
            return HavelRuntimeError("Hotkey() requires at least (key, callback)");
        }

        std::string key = args[0].asString();
        std::function<void()> callback;
        std::function<void()> conditionFalse;
        std::string condition;

        if (args[1].isFunction()) {
            // Store the function value for later execution through interpreter
            HavelValue funcValue = args[1];
            
            callback = [funcValue]() {
                try {
                    // Lock the interpreter weak pointer
                    if (auto interp = g_hotkeyInterpreter.lock()) {
                        havel::debug("Hotkey callback executing on interpreter {}", (void*)interp.get());
                        // Execute through interpreter - CallFunction handles its own locking
                        auto result = interp->CallFunction(funcValue, {});
                        if (std::holds_alternative<HavelRuntimeError>(result)) {
                            havel::error("Hotkey callback error: {}", 
                                std::get<HavelRuntimeError>(result).what());
                        }
                    } else {
                        havel::error("Hotkey callback: interpreter no longer exists");
                    }
                } catch (const std::exception& e) {
                    havel::error("Hotkey callback error: {}", e.what());
                }
            };
        } else {
            return HavelRuntimeError("Hotkey(): second argument must be a function");
        }

        // Parse optional condition (string or function)
        if (args.size() >= 3) {
            if (args[2].isString()) {
                condition = args[2].asString();
            } else if (args[2].isFunction()) {
                // Condition function - will be evaluated by ConditionalHotkeyManager
                condition = args[2].asString();  // Store as string representation
            }
        }

        // Parse optional conditionFalse callback
        if (args.size() >= 4 && args[3].isFunction()) {
            HavelValue conditionFunc = args[3];
            conditionFalse = [conditionFunc]() {
                try {
                    if (auto interp = g_hotkeyInterpreter.lock()) {
                        auto result = interp->CallFunction(conditionFunc, {});
                        if (std::holds_alternative<HavelRuntimeError>(result)) {
                            havel::error("Hotkey conditionFalse error: {}", 
                                std::get<HavelRuntimeError>(result).what());
                        }
                    } else if (conditionFunc.isFunction()) {
                        if (auto* builtinFunc = conditionFunc.get_if<BuiltinFunction>()) {
                            (*builtinFunc)({});
                        }
                    }
                } catch (const std::exception& e) {
                    havel::error("Hotkey conditionFalse error: {}", e.what());
                }
            };
        }

        // Register the hotkey
        int hotkeyId = nextHotkeyId++;
        
        if (!condition.empty()) {
            if (conditionFalse) {
                // With conditionFalse callback
                hostAPI->GetHotkeyManager()->AddContextualHotkey(key, condition, callback, conditionFalse, hotkeyId);
            } else {
                // With condition, no conditionFalse
                hostAPI->GetHotkeyManager()->AddContextualHotkey(key, condition, callback, nullptr, hotkeyId);
            }
        } else {
            // No condition
            hostAPI->GetHotkeyManager()->AddHotkey(key, callback, hotkeyId);
        }

        // Return hotkey object with all info
        auto hotkeyInfo = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*hotkeyInfo)["id"] = HavelValue(static_cast<double>(hotkeyId));
        (*hotkeyInfo)["key"] = HavelValue(key);
        (*hotkeyInfo)["condition"] = HavelValue(condition);
        (*hotkeyInfo)["hasConditionFalse"] = HavelValue(conditionFalse ? true : false);
        (*hotkeyInfo)["active"] = HavelValue(true);
        
        // Methods
        (*hotkeyInfo)["ungrab"] = HavelValue(BuiltinFunction([=](const std::vector<HavelValue>&) -> HavelResult {
            hostAPI->GetHotkeyManager()->UngrabHotkey(hotkeyId);
            return HavelValue(nullptr);
        }));
        
        (*hotkeyInfo)["grab"] = HavelValue(BuiltinFunction([=](const std::vector<HavelValue>&) -> HavelResult {
            hostAPI->GetHotkeyManager()->GrabHotkey(hotkeyId);
            return HavelValue(nullptr);
        }));
        
        (*hotkeyInfo)["remove"] = HavelValue(BuiltinFunction([=](const std::vector<HavelValue>&) -> HavelResult {
            hostAPI->GetHotkeyManager()->RemoveHotkey(hotkeyId);
            return HavelValue(nullptr);
        }));
        
        (*hotkeyInfo)["info"] = HavelValue(BuiltinFunction([hotkeyId, key, condition](const std::vector<HavelValue>&) -> HavelResult {
            auto info = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*info)["id"] = HavelValue(static_cast<double>(hotkeyId));
            (*info)["key"] = HavelValue(key);
            (*info)["condition"] = HavelValue(condition);
            return HavelValue(info);
        }));

        return HavelValue(hotkeyInfo);
    })));

    // Register hotkey module
    env.Define("hotkey", HavelValue(hotkeyObj));
}

} // namespace havel::modules
