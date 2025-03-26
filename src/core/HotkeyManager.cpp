#include "HotkeyManager.hpp"
#include "utils/Logger.hpp"
#include <iostream>

namespace H {

HotkeyManager::HotkeyManager(IO& io, WindowManager& windowManager, MPVController& mpv, ScriptEngine& scriptEngine)
    : io(io), windowManager(windowManager), mpv(mpv), scriptEngine(scriptEngine) {
    // Initialize
}

void HotkeyManager::RegisterDefaultHotkeys() {
    // Basic application hotkeys
    io.Hotkey("ctrl+alt+r", [this]() {
        // Reload configuration
        // config.Reload();
        std::cout << "Reloading configuration" << std::endl;
    });
    
    io.Hotkey("ctrl+alt+q", []() {
        // Quit application
        std::cout << "Quitting application" << std::endl;
        exit(0);
    });
}

void HotkeyManager::RegisterMediaHotkeys() {
    // Volume control
    io.Hotkey("XF86AudioRaiseVolume", [this]() {
        mpv.VolumeUp();
    });
    
    io.Hotkey("XF86AudioLowerVolume", [this]() {
        mpv.VolumeDown();
    });
    
    io.Hotkey("XF86AudioMute", [this]() {
        mpv.ToggleMute();
    });
    
    // Media control
    io.Hotkey("XF86AudioPlay", [this]() {
        mpv.PlayPause();
    });
    
    io.Hotkey("XF86AudioStop", [this]() {
        mpv.Stop();
    });
    
    io.Hotkey("XF86AudioNext", [this]() {
        mpv.Next();
    });
    
    io.Hotkey("XF86AudioPrev", [this]() {
        mpv.Previous();
    });
}

void HotkeyManager::RegisterWindowHotkeys() {
    // Window movement
    io.Hotkey("alt+up", []() {
        WindowManager::MoveWindow(1);
    });
    
    io.Hotkey("alt+down", []() {
        WindowManager::MoveWindow(2);
    });
    
    io.Hotkey("alt+left", []() {
        WindowManager::MoveWindow(3);
    });
    
    io.Hotkey("alt+right", []() {
        WindowManager::MoveWindow(4);
    });
    
    // Window resizing
    io.Hotkey("shift+alt+up", []() {
        WindowManager::ResizeWindow(1);
    });
    
    io.Hotkey("shift+alt+down", []() {
        WindowManager::ResizeWindow(2);
    });
    
    io.Hotkey("shift+alt+left", []() {
        WindowManager::ResizeWindow(3);
    });
    
    io.Hotkey("shift+alt+right", []() {
        WindowManager::ResizeWindow(4);
    });
    
    // Window always on top
    io.Hotkey("ctrl+space", []() {
        WindowManager::ToggleAlwaysOnTop();
    });
}

void HotkeyManager::RegisterSystemHotkeys() {
    // System commands
    io.Hotkey("ctrl+alt+l", []() {
        // Lock screen
        system("xdg-screensaver lock");
    });
    
    io.Hotkey("ctrl+alt+delete", []() {
        // Show system monitor
        system("gnome-system-monitor &");
    });
}

bool HotkeyManager::AddHotkey(const std::string& hotkeyStr, std::function<void()> callback) {
    // Add a hotkey with a callback
    lo.info("Adding hotkey: " + hotkeyStr);
    return true;
}

bool HotkeyManager::AddHotkey(const std::string& hotkeyStr, const std::string& action) {
    // Add a hotkey with an action string
    lo.info("Adding hotkey: " + hotkeyStr + " with action: " + action);
    return true;
}

bool HotkeyManager::RemoveHotkey(const std::string& hotkeyStr) {
    // Remove a hotkey
    lo.info("Removing hotkey: " + hotkeyStr);
    return true;
}

void HotkeyManager::LoadHotkeyConfigurations() {
    // Load hotkeys from configuration file
    // This would typically read from a JSON or similar config file
    
    // Example of loading a hotkey from config (placeholder implementation)
    std::cout << "Loading hotkey configurations..." << std::endl;
    
    // In a real implementation, we would iterate through config entries
    // and register each hotkey with its action
}

void HotkeyManager::ReloadConfigurations() {
    // Reload hotkey configurations
    lo.info("Reloading hotkey configurations");
    LoadHotkeyConfigurations();
}

} // namespace H 