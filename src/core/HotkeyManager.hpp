#pragma once
#include "IO.hpp"
#include "../window/WindowManager.hpp"
#include "MPVController.hpp"
#include "ScriptEngine.hpp"
#include "BrightnessManager.hpp"
#include "../utils/Utils.hpp"
#include <functional>
#include <filesystem>
#include <string>
#include <map>
#include <regex>
#include <vector>
#include <ctime>

namespace H {

class MPVController; // Forward declaration

class HotkeyManager {
public:
    HotkeyManager(IO& io, WindowManager& windowManager, MPVController& mpv, ScriptEngine& scriptEngine);
    ~HotkeyManager() = default;
    
    // Debug flags
    bool verboseKeyLogging = false;
    bool verboseWindowLogging = false;
    bool verboseConditionLogging = false;
    
    void setVerboseKeyLogging(bool value) { verboseKeyLogging = value; }
    void setVerboseWindowLogging(bool value) { verboseWindowLogging = value; }
    void setVerboseConditionLogging(bool value) { verboseConditionLogging = value; }
    
    // Load debug settings from config
    void loadDebugSettings();
    
    // Initialize debug settings
    void initDebugSettings();
    
    // Apply current debug settings
    void applyDebugSettings();

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

    // Mode management
    void setMode(const std::string& mode);
    std::string getMode() const { return currentMode; }
    bool isZooming() const { return m_isZooming; }
    void setZooming(bool zooming) { m_isZooming = zooming; }
    
    // MPV hotkey management
    void grabMPVHotkeys();
    void ungrabMPVHotkeys();
    
    // Black overlay functionality
    void showBlackOverlay();

    // Contextual hotkey support
    bool AddContextualHotkey(const std::string& key, const std::string& condition,
                           std::function<void()> trueAction,
                           std::function<void()> falseAction = nullptr);
    
    // Overload that accepts an ID parameter
    int AddContextualHotkey(const std::string& key, const std::string& condition,
                          std::function<void()> trueAction,
                          std::function<void()> falseAction = nullptr,
                          int id = 0);
                          
    // Make this public so main.cpp can call it for window checks
    bool evaluateCondition(const std::string& condition);

    // Window management
    void minimizeActiveWindow();
    void maximizeActiveWindow();
    void tileWindows();
    void centerActiveWindow();
    void restoreWindow();

    // Active window info
    void printActiveWindowInfo();
    void toggleWindowFocusTracking();

private:
    IO& io;
    WindowManager& windowManager;
    MPVController& mpv;
    ScriptEngine& scriptEngine;
    BrightnessManager brightnessManager;

    // Mode management
    std::string currentMode{"default"};
    bool m_isZooming{false};
    bool videoPlaying{false};
    time_t lastVideoCheck{0};
    const int VIDEO_TIMEOUT_SECONDS{1800}; // 30 minutes
    
    // Hotkey state management
    bool mpvHotkeysGrabbed{false};
    std::map<std::string, bool> windowConditionStates;  // Tracks if particular window conditions were met

    // Window groups
    std::vector<std::string> videoSites;  // Will be loaded from config

    // Key name conversion maps
    const std::map<std::string, std::string> keyNameAliases = {
        // Mouse buttons
        {"button1", "Button1"},
        {"lmb", "Button1"},
        {"rmb", "Button2"},
        {"mmb", "Button3"},
        {"mouse1", "Button1"},
        {"mouse2", "Button2"},
        {"mouse3", "Button3"},
        {"wheelup", "Button4"},
        {"wheeldown", "Button5"},
        
        // Numpad keys
        {"numpad0", "KP_0"},
        {"numpad1", "KP_1"},
        {"numpad2", "KP_2"},
        {"numpad3", "KP_3"},
        {"numpad4", "KP_4"},
        {"numpad5", "KP_5"},
        {"numpad6", "KP_6"},
        {"numpad7", "KP_7"},
        {"numpad8", "KP_8"},
        {"numpad9", "KP_9"},
        {"numpaddot", "KP_Decimal"},
        {"numpadenter", "KP_Enter"},
        {"numpadplus", "KP_Add"},
        {"numpadminus", "KP_Subtract"},
        {"numpadmult", "KP_Multiply"},
        {"numpaddiv", "KP_Divide"},
        
        // Special keys
        {"win", "Super_L"},
        {"rwin", "Super_R"},
        {"menu", "Menu"},
        {"apps", "Menu"},
        {"less", "comma"},
        {"greater", "period"},
        {"equals", "equal"},
        {"minus", "minus"},
        {"plus", "plus"},
        {"return", "Return"},
        {"enter", "Return"},
        {"esc", "Escape"},
        {"backspace", "BackSpace"},
        {"del", "Delete"},
        {"ins", "Insert"},
        {"pgup", "Page_Up"},
        {"pgdn", "Page_Down"},
        {"prtsc", "Print"},
        
        // Modifier keys
        {"ctrl", "Control_L"},
        {"rctrl", "Control_R"},
        {"alt", "Alt_L"},
        {"ralt", "Alt_R"},
        {"shift", "Shift_L"},
        {"rshift", "Shift_R"},
        {"capslock", "Caps_Lock"},
        {"numlock", "Num_Lock"},
        {"scrolllock", "Scroll_Lock"}
    };

    // Helper functions
    void showNotification(const std::string& title, const std::string& message);
    std::string convertKeyName(const std::string& keyName);
    std::string parseHotkeyString(const std::string& hotkeyStr);
    
    // Autoclicker helpers
    bool isGamingWindow();
    void startAutoclicker(const std::string& button);

    // Key name conversion helpers
    std::string handleKeycode(const std::string& input);
    std::string handleScancode(const std::string& input);
    std::string normalizeKeyName(const std::string& keyName);

    // ANSI color codes for logging
    const std::string COLOR_RESET = "\033[0m";
    const std::string COLOR_RED = "\033[31m";
    const std::string COLOR_GREEN = "\033[32m";
    const std::string COLOR_YELLOW = "\033[33m";
    const std::string COLOR_BLUE = "\033[34m";
    const std::string COLOR_MAGENTA = "\033[35m";
    const std::string COLOR_CYAN = "\033[36m";
    const std::string COLOR_BOLD = "\033[1m";
    const std::string COLOR_DIM = "\033[2m";

    // Enhanced logging methods
    void logKeyEvent(const std::string& key, const std::string& eventType, const std::string& details = "");
    void logWindowEvent(const std::string& eventType, const std::string& details = "");
    std::string getWindowInfo(wID windowId = 0);

    // Logging helpers
    void logHotkeyEvent(const std::string& eventType, const std::string& details);
    void logKeyConversion(const std::string& from, const std::string& to);
    void logModeSwitch(const std::string& from, const std::string& to);

    // New methods for video window group and playback status
    bool isVideoSiteActive();
    void updateVideoPlaybackStatus();
    void handleMediaCommand(const std::vector<std::string>& mpvCommand);

    // New methods for video timeout
    void loadVideoSites();
    bool hasVideoTimedOut() const;
    void updateLastVideoCheck();

    // Store IDs of MPV hotkeys for grab/ungrab
    std::vector<int> mpvHotkeyIds;
    
    // Window condition helper methods
    bool checkWindowCondition(const std::string& condition);
    void updateHotkeyStateForCondition(const std::string& condition, bool conditionMet);

    // Window focus tracking
    bool trackWindowFocus;
    wID lastActiveWindowId;
};
} 