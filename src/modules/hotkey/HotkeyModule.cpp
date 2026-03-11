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
    // Hotkey(key, callback, condition?)
    // =========================================================================

    env.Define("Hotkey", HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 2) {
            return HavelRuntimeError("Hotkey() requires at least (key, callback)");
        }

        std::string key = args[0].asString();
        std::function<void()> callback;
        std::string condition;

        if (args[1].isFunction()) {
            callback = [func = args[1].get<std::function<HavelResult(const std::vector<HavelValue>&)>>()]() {
                func({});
            };
        } else {
            return HavelRuntimeError("Hotkey(): second argument must be a function");
        }

        if (args.size() >= 3 && args[2].isString()) {
            condition = args[2].asString();
        }

        if (!condition.empty()) {
            hotkeyManager.AddContextualHotkey(key, condition, callback);
        } else {
            hotkeyManager.AddHotkey(key, callback);
        }

        return HavelValue(nullptr);
    })));

    // Register hotkey module
    env.Define("hotkey", HavelValue(hotkeyObj));
}

} // namespace havel::modules
