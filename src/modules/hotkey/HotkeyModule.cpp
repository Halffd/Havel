/*
 * HotkeyModule.cpp
 * 
 * Hotkey management module for Havel language.
 * Provides hotkey control and overlay functions.
 */
#include "HotkeyModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"

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
    // =========================================================================
    
    (*hotkeyObj)["list"] = HavelValue(BuiltinFunction([&hotkeyManager](const std::vector<HavelValue>&) -> HavelResult {
        auto arr = std::make_shared<std::vector<HavelValue>>();
        auto hotkeys = hotkeyManager.getAllHotkeys();
        
        for (const auto& hk : hotkeys) {
            arr->push_back(HavelValue(hk));
        }
        
        return HavelValue(arr);
    }));
    
    // Register hotkey module
    env.Define("hotkey", HavelValue(hotkeyObj));
}

} // namespace havel::modules
