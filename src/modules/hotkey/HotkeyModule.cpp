/*
 * HotkeyModule.cpp
 *
 * Hotkey management module for Havel language.
 * Provides hotkey control and overlay functions.
 */
#include "HotkeyModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/HotkeyManager.hpp"

namespace havel::modules {

void registerHotkeyModule(Environment& env, HostContext& ctx) {
    if (!ctx.hotkeyManager) {
        return;  // Skip if hotkey manager not available
    }
    
    auto& hotkeyManager = *ctx.hotkeyManager;
    
    // Create hotkey object
    auto hotkeyObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // =========================================================================
    // hotkey.toggleOverlay() - Toggle fake desktop overlay
    // =========================================================================
    
    (*hotkeyObj)["toggleOverlay"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        hotkeyManager.toggleFakeDesktopOverlay();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.showBlackOverlay() - Show black overlay
    // =========================================================================
    
    (*hotkeyObj)["showBlackOverlay"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        hotkeyManager.showBlackOverlay();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.printActiveWindowInfo() - Print active window info
    // =========================================================================
    
    (*hotkeyObj)["printActiveWindowInfo"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        hotkeyManager.printActiveWindowInfo();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.toggleWindowFocusTracking() - Toggle window focus tracking
    // =========================================================================
    
    (*hotkeyObj)["toggleWindowFocusTracking"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        hotkeyManager.toggleWindowFocusTracking();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.updateConditional() - Update conditional hotkeys
    // =========================================================================
    
    (*hotkeyObj)["updateConditional"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        hotkeyManager.updateAllConditionalHotkeys();
        return HavelValue(nullptr);
    }));
    
    // =========================================================================
    // hotkey.clearAll() - Clear all registered hotkeys
    // =========================================================================
    
    (*hotkeyObj)["clearAll"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        hotkeyManager.clearAllHotkeys();
        return HavelValue(nullptr);
    }));

    // =========================================================================
    // hotkey.list() - List all registered hotkeys
    // Note: getAllHotkeys() not available in HotkeyManager
    // =========================================================================

    (*hotkeyObj)["list"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        (void)hotkeyManager;  // Suppress unused warning
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

    env.Define("Hotkey", HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("Hotkey() requires at least (key, callback)");
        }

        std::string key = args[0].asString();
        std::function<void()> callback;
        std::function<void()> conditionFalse;
        std::string condition;

        if (args[1].isFunction()) {
            callback = [func = args[1].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() {
                func({});
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
            conditionFalse = [func = args[3].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() {
                func({});
            };
        }

        // Register the hotkey
        int hotkeyId = nextHotkeyId++;
        
        if (!condition.empty()) {
            if (conditionFalse) {
                // With conditionFalse callback
                hotkeyManager.AddContextualHotkey(key, condition, callback, conditionFalse, hotkeyId);
            } else {
                // With condition, no conditionFalse
                hotkeyManager.AddContextualHotkey(key, condition, callback, nullptr, hotkeyId);
            }
        } else {
            // No condition
            hotkeyManager.AddHotkey(key, callback, hotkeyId);
        }

        // Return hotkey object with all info
        auto hotkeyInfo = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*hotkeyInfo)["id"] = HavelValue(static_cast<double>(hotkeyId));
        (*hotkeyInfo)["key"] = HavelValue(key);
        (*hotkeyInfo)["condition"] = HavelValue(condition);
        (*hotkeyInfo)["hasConditionFalse"] = HavelValue(conditionFalse ? true : false);
        (*hotkeyInfo)["active"] = HavelValue(true);
        
        // Methods
        (*hotkeyInfo)["ungrab"] = HavelValue(BuiltinFunction([hotkeyId, &hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
            hotkeyManager.UngrabHotkey(hotkeyId);
            return HavelValue(nullptr);
        }));
        
        (*hotkeyInfo)["grab"] = HavelValue(BuiltinFunction([hotkeyId, &hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
            hotkeyManager.GrabHotkey(hotkeyId);
            return HavelValue(nullptr);
        }));
        
        (*hotkeyInfo)["remove"] = HavelValue(BuiltinFunction([hotkeyId, &hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
            hotkeyManager.RemoveHotkey(hotkeyId);
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
