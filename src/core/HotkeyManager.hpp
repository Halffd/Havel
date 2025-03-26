#pragma once
#include "IO.hpp"
#include "../window/WindowManager.hpp"
#include "MPVController.hpp"
#include "ScriptEngine.hpp"
#include "BrightnessManager.hpp"
#include <functional>
#include <filesystem>

namespace H {

class MPVController; // Forward declaration

class HotkeyManager {
public:
    HotkeyManager(IO& io, WindowManager& windowManager, MPVController& mpv, ScriptEngine& scriptEngine);
    ~HotkeyManager() = default;
    
    void RegisterDefaultHotkeys();
    void RegisterMediaHotkeys();
    void RegisterWindowHotkeys();
    void RegisterSystemHotkeys();
    
    bool AddHotkey(const std::string& hotkeyStr, std::function<void()> callback);
    bool AddHotkey(const std::string& hotkeyStr, const std::string& action);
    bool RemoveHotkey(const std::string& hotkeyStr);
    
    void LoadHotkeyConfigurations();
    void ReloadConfigurations();

    // Add public getter for brightnessManager
    BrightnessManager& getBrightnessManager() { return brightnessManager; }

private:
    IO& io;
    WindowManager& windowManager;
    MPVController& mpv;
    ScriptEngine& scriptEngine;
    BrightnessManager brightnessManager;
};
} 