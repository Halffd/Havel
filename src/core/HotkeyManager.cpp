#include "HotkeyManager.hpp"
#include "utils/Logger.hpp"
#include "window/Window.hpp"
#include "core/ConfigManager.hpp"
#include <iostream>
#include <map>
#include <vector>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <sstream>
#include <algorithm>
#include <ctime>

namespace H {

HotkeyManager::HotkeyManager(IO& io, WindowManager& windowManager, MPVController& mpv, ScriptEngine& scriptEngine)
    : io(io), windowManager(windowManager), mpv(mpv), scriptEngine(scriptEngine), brightnessManager() {
    loadVideoSites();
}

void HotkeyManager::loadVideoSites() {
    // Clear existing sites
    videoSites.clear();
    
    // Get video sites from config
    std::string sitesStr = H::Configs::Get().Get<std::string>("VideoSites.Sites", "netflix,animelon,youtube");
    
    // Split the comma-separated string into vector
    std::stringstream ss(sitesStr);
    std::string site;
    while (std::getline(ss, site, ',')) {
        // Trim whitespace
        site.erase(0, site.find_first_not_of(" \t"));
        site.erase(site.find_last_not_of(" \t") + 1);
        if (!site.empty()) {
            videoSites.push_back(site);
        }
    }
    
    if (verboseWindowLogging) {
        std::string siteList;
        for (const auto& site : videoSites) {
            if (!siteList.empty()) siteList += ", ";
            siteList += site;
        }
        logWindowEvent("CONFIG", "Loaded video sites: " + siteList);
    }
}

bool HotkeyManager::hasVideoTimedOut() const {
    if (lastVideoCheck == 0) return true;
    return (time(nullptr) - lastVideoCheck) > VIDEO_TIMEOUT_SECONDS;
}

void HotkeyManager::updateLastVideoCheck() {
    lastVideoCheck = time(nullptr);
    if (verboseWindowLogging) {
        logWindowEvent("VIDEO_CHECK", "Updated last video check timestamp");
    }
}

void HotkeyManager::updateVideoPlaybackStatus() {
    if (!isVideoSiteActive()) {
        videoPlaying = false;
        return;
    }

    // If we haven't detected video activity in 30 minutes, reset the status
    if (hasVideoTimedOut()) {
        if (verboseWindowLogging && videoPlaying) {
            logWindowEvent("VIDEO_TIMEOUT", "Video playback status reset due to timeout");
        }
        videoPlaying = false;
        return;
    }

    // Update the last check time since we found a video site
    updateLastVideoCheck();
    videoPlaying = true;
    
    if (verboseWindowLogging) {
        logWindowEvent("VIDEO_STATUS", videoPlaying ? "Video is playing" : "No video playing");
    }
}

void HotkeyManager::RegisterDefaultHotkeys() {
    // Auto-start applications if enabled
    // Temporarily commenting out auto-start until ScriptEngine config is fixed
    /*if (scriptEngine.GetConfig().Get<bool>("System.AutoStart", false)) {
        system("discord &");
        system("spotify &");
    }*/

    // Mode toggles
    io.Hotkey("ctrl+shift+g", [this]() {
        std::string oldMode = currentMode;
        currentMode = (currentMode == "gaming") ? "default" : "gaming";
        logModeSwitch(oldMode, currentMode);
        if (verboseWindowLogging) {
            logWindowEvent("MODE_CHANGE", "Mode switched to: " + currentMode);
        }
        showNotification("Mode Changed", "Active mode: " + currentMode);
    });

    // MPV Controls - Gaming Mode Only
    const std::map<std::string, std::vector<std::string>> mpvHotkeys = {
        // Playback controls
        {"z", {"cycle", "pause"}},
        {"o", {"seek", "-3"}},
        {"p", {"seek", "3"}},
        
        // Subtitle visibility
        {"l", {"cycle", "sub-visibility"}},
        {"semicolon", {"cycle", "secondary-sub-visibility"}},
        
        // Subtitle scale
        {"7", {"add", "sub-scale", "-0.1"}},
        {"8", {"add", "sub-scale", "0.1"}},
        
        // Subtitle delay
        {"5", {"add", "sub-delay", "-0.1"}},
        {"6", {"add", "sub-delay", "0.1"}},
        
        // Subtitle navigation
        {"9", {"cycle", "sub"}},
        {"0", {"sub-seek", "0"}},
        {"minus", {"sub-seek", "-1"}},
        {"equal", {"sub-seek", "1"}},
        
        // Special commands
        {"m", {"script-binding", "copy_current_subtitle"}}
    };

    // Register all MPV hotkeys with gaming mode check
    for (const auto& [key, command] : mpvHotkeys) {
        io.Hotkey(key, [this, command]() {
            // Check if we're in a gaming window
            bool isGamingWindow = false;
            const std::vector<std::string> gamingApps = {
                "steam_app", "retroarch", "ryujinx", "pcsx2"
            };
            
            // Get active window class
            Window activeWindow;
            std::string windowClass = WindowManager::GetActiveWindowClass();
            
            // Check if window class matches any gaming app
            for (const auto& app : gamingApps) {
                if (windowClass.find(app) != std::string::npos) {
                    isGamingWindow = true;
                    break;
                }
            }
            
            if (isGamingWindow) {
                handleMediaCommand(command);
            }
        });
    }

    // Special keycode-based hotkey for gaming mode
    io.Hotkey("kc94", [this]() {
        logHotkeyEvent("KEYPRESS", COLOR_YELLOW + "Keycode 94" + COLOR_RESET);
        
        if (isGamingWindow()) {
            logHotkeyEvent("ACTION", COLOR_CYAN + "Gaming mode active" + COLOR_RESET + " - Sending pause to MPV");
            mpv.SendCommand({"cycle", "pause"});
        } else {
            logHotkeyEvent("ACTION", COLOR_CYAN + "Normal mode" + COLOR_RESET + " - Remapping to backslash");
            io.PressKey("backslash", true);
            io.PressKey("backslash", false);
        }
    });

    // Basic application hotkeys
    io.Hotkey("ctrl+alt+r", [this]() {
        ReloadConfigurations();
        lo.info("Reloading configuration");
    });
    
    io.Hotkey("ctrl+alt+q", []() {
        lo.info("Quitting application");
        exit(0);
    });

    // Media Controls
    io.Hotkey("win+f1", []() {
        system("playerctl previous");
    });
    
    io.Hotkey("win+f2", []() {
        system("playerctl play-pause");
    });
    
    io.Hotkey("win+f3", []() {
        system("playerctl next");
    });
    
    io.Hotkey("win+f4", []() {
        system("pactl set-sink-volume @DEFAULT_SINK@ -5%");
    });
    
    io.Hotkey("win+f5", []() {
        system("pactl set-sink-volume @DEFAULT_SINK@ +5%");
    });

    io.Hotkey("alt+f6", [this]() {
        mpv.SendCommand({"cycle", "pause"});
    });

    // Application Shortcuts
    io.Hotkey("rwin", [this]() {
        io.PressKey("alt", true);
        io.PressKey("backspace", true);
        io.PressKey("backspace", false);
        io.PressKey("alt", false);
    });

    io.Hotkey("KP_7", [this]() {
        io.PressKey("ctrl", true);
        io.PressKey("up", true);
        io.PressKey("up", false);
        io.PressKey("ctrl", false);
    });

    io.Hotkey("KP_1", [this]() {
        io.PressKey("ctrl", true);
        io.PressKey("down", true);
        io.PressKey("down", false);
        io.PressKey("ctrl", false);
    });

    io.Hotkey("KP_5", [this]() {
        io.PressKey("ctrl", true);
        io.PressKey("/", true);
        io.PressKey("/", false);
        io.PressKey("ctrl", false);
    });

    io.Hotkey("alt+f1", []() {
        system("~/scripts/f1.sh -1");
    });

    io.Hotkey("alt+l", []() {
        system("livelink 1");
    });

    io.Hotkey("alt+k", []() {
        system("livelink 0 1");
    });

    // Context-sensitive hotkeys
    AddContextualHotkey("NoSymbol", "IsZooming",
        [this]() { // When zooming
            io.PressKey("ctrl", true);
            io.PressKey("/", true);
            io.PressKey("/", false);
            io.PressKey("ctrl", false);
        },
        [this]() { // When not zooming
            io.PressKey("ctrl", true);
            io.PressKey("up", true);
            io.PressKey("up", false);
            io.PressKey("ctrl", false);
        }
    );

    AddContextualHotkey("alt+x", "!Window.Active('Emacs')",
        []() { system("kitty"); }
    );

    // Window Management
    io.Hotkey("alt+left", []() {
        lo.debug("Moving window left");
        WindowManager::MoveWindow(3);
    });

    io.Hotkey("alt+right", [this]() {
        lo.debug("Moving window right");
        // Move window to next monitor using MoveWindow(4) for right movement
        WindowManager::MoveWindow(4);
    });

    io.Hotkey("Menu", [this]() {
        // Log current window before switching
        std::string currentClass = WindowManager::GetActiveWindowClass();
        lo.debug("Current window class before switch: " + currentClass);
        
        // Switch to last window using window manager's static method
        WindowManager::MoveWindow(0);
        
        // Log new window after switching
        std::string newClass = WindowManager::GetActiveWindowClass();
        lo.debug("New window class after switch: " + newClass);
    });

    // Emergency Features
    const std::map<std::string, std::pair<std::string, std::function<void()>>> emergencyHotkeys = {
        {"alt+esc", {"Emergency application exit", []() {
            lo.info("Emergency exit triggered");
            exit(0);
        }}},
        {"f9", {"Suspend hotkeys", [this]() {
            lo.info("Suspending all hotkeys");
            io.Suspend(0); // Special case: 0 means suspend all hotkeys
            lo.debug("Hotkeys suspended");
        }}},
        {"win+esc", {"Restart application", [this]() {
            lo.info("Restarting application");
            // Get current executable path using the correct namespace
            std::string exePath = H::GetExecutablePath();
            if (!exePath.empty()) {
                lo.debug("Executable path: " + exePath);
                // Fork and exec to restart
                if(fork() == 0) {
                    lo.debug("Child process started, executing: " + exePath);
                    execl(exePath.c_str(), exePath.c_str(), nullptr);
                    lo.error("Failed to restart application");
                    exit(1); // Only reached if execl fails
                }
                lo.info("Parent process exiting for restart");
                exit(0); // Parent process exits
            } else {
                lo.error("Failed to get executable path");
            }
        }}},
        {"win+alt+esc", {"Reload configuration", [this]() {
            lo.info("Reloading configuration");
            ReloadConfigurations();
            lo.debug("Configuration reload complete");
        }}}
    };

    // Register all emergency hotkeys
    for (const auto& [key, info] : emergencyHotkeys) {
        const auto& [description, action] = info;
        io.Hotkey(key, [description, action]() {
            lo.info("Executing emergency hotkey: " + description);
            action();
        });
    }

    // Brightness and gamma control
    io.Hotkey("f3", [this]() {
        lo.info("Setting default brightness");
        brightnessManager.setDefaultBrightness();
        lo.info("Brightness set to: " + std::to_string(brightnessManager.getCurrentBrightnessValue()));
    });

    io.Hotkey("f7", [this]() {
        lo.info("Decreasing brightness");
        brightnessManager.decreaseBrightness(0.05);
        lo.info("Current brightness: " + std::to_string(brightnessManager.getCurrentBrightnessValue()));
    });

    io.Hotkey("f8", [this]() {
        lo.info("Increasing brightness");
        brightnessManager.increaseBrightness(0.05);
        lo.info("Current brightness: " + std::to_string(brightnessManager.getCurrentBrightnessValue()));
    });

    io.Hotkey("shift+f7", [this]() {
        lo.info("Decreasing gamma");
        brightnessManager.decreaseGamma(500);
        lo.info("Current gamma: " + std::to_string(brightnessManager.getCurrentGamma()));
    });

    io.Hotkey("shift+f8", [this]() {
        lo.info("Increasing gamma");
        brightnessManager.increaseGamma(500);
        lo.info("Current gamma: " + std::to_string(brightnessManager.getCurrentGamma()));
    });

    // Mouse wheel + click combinations
    io.Hotkey("Button1", [this]() {
        io.PressKey("ctrl", true);
        io.PressKey("up", true);
        io.PressKey("up", false);
        io.PressKey("ctrl", false);
    });

    io.Hotkey("Button5", [this]() {
        io.PressKey("ctrl", true);
        io.PressKey("down", true);
        io.PressKey("down", false);
        io.PressKey("ctrl", false);
    });

    // Autoclicker for gaming mode
    io.Hotkey("comma", [this]() {
        startAutoclicker("Button1");
    });

    io.Hotkey("Shift_R", [this]() {
        startAutoclicker("Button2");
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
    io.Hotkey("alt+Up", []() {
        WindowManager::MoveWindow(1);
    });
    
    io.Hotkey("alt+Down", []() {
        WindowManager::MoveWindow(2);
    });
    
    io.Hotkey("alt+Left", []() {
        WindowManager::MoveWindow(3);
    });
    
    io.Hotkey("alt+Right", []() {
        WindowManager::MoveWindow(4);
    });
    
    // Window resizing
    io.Hotkey("shift+alt+Up", []() {
        WindowManager::ResizeWindow(1);
    });
    
    io.Hotkey("shift+alt+Down", []() {
        WindowManager::ResizeWindow(2);
    });
    
    io.Hotkey("shift+alt+Left", []() {
        WindowManager::ResizeWindow(3);
    });
    
    io.Hotkey("shift+alt+Right", []() {
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
    
    io.Hotkey("ctrl+alt+Delete", []() {
        // Show system monitor
        system("gnome-system-monitor &");
    });
}

bool HotkeyManager::AddHotkey(const std::string& hotkeyStr, std::function<void()> callback) {
    std::string convertedHotkey = parseHotkeyString(hotkeyStr);
    logKeyEvent(hotkeyStr, "REGISTER", "Converted to: " + convertedHotkey);
    
    return io.Hotkey(convertedHotkey, [this, hotkeyStr, callback]() {
        logKeyEvent(hotkeyStr, "PRESS");
        if (verboseWindowLogging) {
            logWindowEvent("ACTIVE", "Key pressed: " + hotkeyStr);
        }
        callback();
    });
}

bool HotkeyManager::AddHotkey(const std::string& hotkeyStr, const std::string& action) {
    std::string convertedHotkey = parseHotkeyString(hotkeyStr);
    lo.debug("Converting hotkey '" + hotkeyStr + "' to '" + convertedHotkey + "'");
    return io.Hotkey(convertedHotkey, [action]() {
        system(action.c_str());
    });
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
    lo.info("Reloading configurations");
    LoadHotkeyConfigurations();
    
    // Reload video sites
    loadVideoSites();
}

bool HotkeyManager::AddContextualHotkey(const std::string& key, const std::string& condition,
                                      std::function<void()> trueAction,
                                      std::function<void()> falseAction) {
    return io.Hotkey(key, [this, condition, trueAction, falseAction]() {
        if (evaluateCondition(condition)) {
            if (trueAction) trueAction();
        } else {
            if (falseAction) falseAction();
        }
    });
}

bool HotkeyManager::evaluateCondition(const std::string& condition) {
    if (condition == "IsZooming") {
        return isZooming();
    }
    else if (condition == "currentMode == 'gaming'") {
        // Check if current window is a gaming window
        const std::vector<std::string> gamingApps = {
            "steam_app", "retroarch", "ryujinx", "pcsx2"
        };
        
        std::string windowClass = WindowManager::GetActiveWindowClass();
        for (const auto& app : gamingApps) {
            if (windowClass.find(app) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    else if (condition.find("Window.Active") == 0) {
        // Extract window title from condition
        std::string title = condition.substr(14, condition.length() - 16);
        bool negation = condition[0] == '!';
        if (negation) {
            title = title.substr(1);
        }
        // Use WindowManager's static method to find window
        wID windowId = WindowManager::FindByTitle(title.c_str());
        return negation ? (windowId == 0) : (windowId != 0);
    }
    return false;
}

void HotkeyManager::showNotification(const std::string& title, const std::string& message) {
    std::string cmd = "notify-send \"" + title + "\" \"" + message + "\"";
    system(cmd.c_str());
}

bool HotkeyManager::isGamingWindow() {
    std::string windowClass = WindowManager::GetActiveWindowClass();
    if (verboseWindowLogging) {
        logWindowEvent("CHECK_GAMING", "Checking if window is gaming application");
    }
    
    const std::vector<std::string> gamingApps = {
        "steam_app", "retroarch", "ryujinx", "pcsx2"
    };
    
    for (const auto& app : gamingApps) {
        if (windowClass.find(app) != std::string::npos) {
            if (verboseWindowLogging) {
                logWindowEvent("GAMING_DETECTED", "Gaming application: " + app);
            }
            return true;
        }
    }
    
    if (verboseWindowLogging) {
        logWindowEvent("NOT_GAMING", "Window is not a gaming application");
    }
    return false;
}

void HotkeyManager::startAutoclicker(const std::string& button) {
    if (!isGamingWindow()) {
        lo.debug("Autoclicker not activated - not in gaming window");
        return;
    }
    
    lo.info("Starting autoclicker (" + button + " button) in gaming window");
    io.SetTimer(50, [this, button]() {
        io.PressKey(button, true);
        io.PressKey(button, false);
    });
}

std::string HotkeyManager::handleKeycode(const std::string& input) {
    // Extract the keycode number from format "kcXXX"
    std::string numStr = input.substr(2);
    try {
        int keycode = std::stoi(numStr);
        // Convert keycode to keysym using X11
        Display* display = XOpenDisplay(nullptr);
        if (!display) {
            lo.error("Failed to open X display for keycode conversion");
            return input;
        }
        
        KeySym keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
        char* keyName = XKeysymToString(keysym);
        XCloseDisplay(display);
        
        if (keyName) {
            return keyName;
        }
    } catch (const std::exception& e) {
        lo.error("Failed to convert keycode: " + input + " - " + e.what());
    }
    return input;
}

std::string HotkeyManager::handleScancode(const std::string& input) {
    // Extract the scancode number from format "scXXX"
    std::string numStr = input.substr(2);
    try {
        int scancode = std::stoi(numStr);
        // Convert scancode to keycode (platform specific)
        // This is a simplified example - you might need to adjust for your specific needs
        int keycode = scancode + 8; // Common offset on Linux
        return handleKeycode("kc" + std::to_string(keycode));
    } catch (const std::exception& e) {
        lo.error("Failed to convert scancode: " + input + " - " + e.what());
    }
    return input;
}

std::string HotkeyManager::normalizeKeyName(const std::string& keyName) {
    // Convert to lowercase for case-insensitive comparison
    std::string normalized = keyName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    // Check aliases map
    auto it = keyNameAliases.find(normalized);
    if (it != keyNameAliases.end()) {
        return it->second;
    }
    
    // Handle single letters (ensure they're lowercase)
    if (normalized.length() == 1 && std::isalpha(normalized[0])) {
        return normalized;
    }
    
    // Handle function keys (F1-F24)
    std::regex fkeyRegex("^f([1-9]|1[0-9]|2[0-4])$");
    if (std::regex_match(normalized, fkeyRegex)) {
        return "F" + normalized.substr(1);
    }
    
    // If no conversion found, return original
    return keyName;
}

std::string HotkeyManager::convertKeyName(const std::string& keyName) {
    std::string result;
    
    // Handle keycodes (kcXXX)
    if (keyName.substr(0, 2) == "kc") {
        result = handleKeycode(keyName);
        logKeyConversion(keyName, result);
        return result;
    }
    
    // Handle scancodes (scXXX)
    if (keyName.substr(0, 2) == "sc") {
        result = handleScancode(keyName);
        logKeyConversion(keyName, result);
        return result;
    }
    
    // Handle normal key names
    result = normalizeKeyName(keyName);
    if (result != keyName) {
        logKeyConversion(keyName, result);
    }
    return result;
}

std::string HotkeyManager::parseHotkeyString(const std::string& hotkeyStr) {
    std::vector<std::string> parts;
    std::stringstream ss(hotkeyStr);
    std::string part;
    
    // Split by '+'
    while (std::getline(ss, part, '+')) {
        // Trim whitespace
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        
        // Convert each part
        parts.push_back(convertKeyName(part));
    }
    
    // Reconstruct the hotkey string
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += "+";
        result += parts[i];
    }
    
    return result;
}

void HotkeyManager::logHotkeyEvent(const std::string& eventType, const std::string& details) {
    std::string timestamp = "[" + COLOR_DIM + std::to_string(time(nullptr)) + COLOR_RESET + "]";
    std::string type = COLOR_BOLD + COLOR_CYAN + "[" + eventType + "]" + COLOR_RESET;
    lo.info(timestamp + " " + type + " " + details);
}

void HotkeyManager::logKeyConversion(const std::string& from, const std::string& to) {
    std::string arrow = COLOR_BOLD + COLOR_BLUE + " → " + COLOR_RESET;
    std::string fromStr = COLOR_YELLOW + from + COLOR_RESET;
    std::string toStr = COLOR_GREEN + to + COLOR_RESET;
    logHotkeyEvent("KEY_CONVERT", fromStr + arrow + toStr);
}

void HotkeyManager::logModeSwitch(const std::string& from, const std::string& to) {
    std::string arrow = COLOR_BOLD + COLOR_MAGENTA + " → " + COLOR_RESET;
    std::string fromStr = COLOR_YELLOW + from + COLOR_RESET;
    std::string toStr = COLOR_GREEN + to + COLOR_RESET;
    logHotkeyEvent("MODE_SWITCH", fromStr + arrow + toStr);
}

void HotkeyManager::logKeyEvent(const std::string& key, const std::string& eventType, const std::string& details) {
    if (!verboseKeyLogging) return;
    
    std::string timestamp = "[" + COLOR_DIM + std::to_string(time(nullptr)) + COLOR_RESET + "]";
    std::string type = COLOR_BOLD + COLOR_CYAN + "[KEY_" + eventType + "]" + COLOR_RESET;
    std::string keyInfo = COLOR_YELLOW + key + COLOR_RESET;
    std::string detailInfo = details.empty() ? "" : " (" + COLOR_GREEN + details + COLOR_RESET + ")";
    
    lo.info(timestamp + " " + type + " " + keyInfo + detailInfo);
}

void HotkeyManager::logWindowEvent(const std::string& eventType, const std::string& details) {
    if (!verboseWindowLogging) return;
    
    std::string timestamp = "[" + COLOR_DIM + std::to_string(time(nullptr)) + COLOR_RESET + "]";
    std::string type = COLOR_BOLD + COLOR_MAGENTA + "[WINDOW_" + eventType + "]" + COLOR_RESET;
    std::string windowInfo = getWindowInfo();
    std::string detailInfo = details.empty() ? "" : " (" + COLOR_GREEN + details + COLOR_RESET + ")";
    
    lo.info(timestamp + " " + type + " " + windowInfo + detailInfo);
}

std::string HotkeyManager::getWindowInfo(wID windowId) {
    if (windowId == 0) {
        windowId = WindowManager::GetActiveWindow();
    }
    
    std::string windowClass = WindowManager::GetActiveWindowClass();
    Window window(std::to_string(windowId), 0);
    std::string title = window.Title();
    
    return COLOR_CYAN + "Class: " + COLOR_RESET + windowClass + 
           COLOR_CYAN + " Title: " + COLOR_RESET + title +
           COLOR_CYAN + " ID: " + COLOR_RESET + std::to_string(windowId);
}

bool HotkeyManager::isVideoSiteActive() {
    Window window(std::to_string(WindowManager::GetActiveWindow()), 0);
    std::string windowTitle = window.Title();
    std::transform(windowTitle.begin(), windowTitle.end(), windowTitle.begin(), ::tolower);
    
    for (const auto& site : videoSites) {
        if (windowTitle.find(site) != std::string::npos) {
            if (verboseWindowLogging) {
                logWindowEvent("VIDEO_SITE", "Detected video site: " + site);
            }
            return true;
        }
    }
    return false;
}

void HotkeyManager::handleMediaCommand(const std::vector<std::string>& mpvCommand) {
    updateVideoPlaybackStatus();
    
    if (isVideoSiteActive() && videoPlaying) {
        if (verboseWindowLogging) {
            logWindowEvent("MEDIA_CONTROL", "Using media keys for web video");
        }
        
        // Map MPV commands to media key actions
        if (mpvCommand[0] == "cycle" && mpvCommand[1] == "pause") {
            system("playerctl play-pause");
        }
        else if (mpvCommand[0] == "seek") {
            if (mpvCommand[1] == "-3") {
                system("playerctl position 3-");
            } else if (mpvCommand[1] == "3") {
                system("playerctl position 3+");
            }
        }
    } else {
        if (verboseWindowLogging) {
            logWindowEvent("MEDIA_CONTROL", "Using MPV command: " + mpvCommand[0]);
        }
        mpv.SendCommand(mpvCommand);
    }
}

} // namespace H 