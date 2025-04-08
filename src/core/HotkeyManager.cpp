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
#include <regex>
#include <thread>
#include <chrono>
#include "core/DisplayManager.hpp"

// Include XRandR for multi-monitor support
#ifdef __linux__
#include <X11/extensions/Xrandr.h>
#endif

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
    std::istringstream ss(sitesStr);
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
    AddContextualHotkey("kc0", "IsZooming",
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
        },
        0  // ID parameter
    );

    AddContextualHotkey("alt+x", "!Window.Active('Emacs')",
        []() { system("kitty"); },
        nullptr, // falseAction parameter
        0       // ID parameter
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

    // Quick window switching hotkeys
    auto switchToLastWindow = []() {
        lo.debug("Switching to last window");
        WindowManager::AltTab();
    };

    io.Hotkey("kc135", switchToLastWindow);
    io.Hotkey("alt+t", switchToLastWindow);

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

    // Add contextual hotkey for D key in Koikatu
    AddContextualHotkey("d", "Window.Active('class:Koikatu')",
        [this]() {
            // Show black overlay when D is pressed in Koikatu
            lo.info("Koikatu window detected - D key pressed - showing black overlay");
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (class match)");
        },
        nullptr, // No action when condition is false
        0 // ID parameter
    );
    
    // Also add a title-based hotkey for Koikatu window title (as a fallback)
    AddContextualHotkey("d", "Window.Active('name:Koikatu')",
        [this]() {
            // Show black overlay when D is pressed in Koikatu window
            lo.info("Koikatu window title detected - D key pressed - showing black overlay");
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (title match)");
        },
        nullptr, // No action when condition is false
        0 // ID parameter
    );
}

void HotkeyManager::RegisterMediaHotkeys() {
    // Clear any existing MPV hotkey IDs
    mpvHotkeyIds.clear();
    
    // Define MPV media hotkeys with unique IDs starting at 10000
    int mpvBaseId = 10000;
    
    // Volume control - Only active in gaming mode
    int volumeUpId = mpvBaseId++;
    AddContextualHotkey("XF86AudioRaiseVolume", "currentMode == 'gaming'", 
        [this]() {
            mpv.VolumeUp();
            logWindowEvent("MPV_HOTKEY", "Volume Up (Gaming Mode)");
        },
        nullptr, volumeUpId
    );
    mpvHotkeyIds.push_back(volumeUpId);
    
    int volumeDownId = mpvBaseId++;
    AddContextualHotkey("XF86AudioLowerVolume", "currentMode == 'gaming'", 
        [this]() {
            mpv.VolumeDown();
            logWindowEvent("MPV_HOTKEY", "Volume Down (Gaming Mode)");
        },
        nullptr, volumeDownId
    );
    mpvHotkeyIds.push_back(volumeDownId);
    
    int muteId = mpvBaseId++;
    AddContextualHotkey("XF86AudioMute", "currentMode == 'gaming'", 
        [this]() {
            mpv.ToggleMute();
            logWindowEvent("MPV_HOTKEY", "Toggle Mute (Gaming Mode)");
        },
        nullptr, muteId
    );
    mpvHotkeyIds.push_back(muteId);
    
    // Media control - Only active in gaming mode
    int playId = mpvBaseId++;
    AddContextualHotkey("XF86AudioPlay", "currentMode == 'gaming'", 
        [this]() {
            mpv.PlayPause();
            logWindowEvent("MPV_HOTKEY", "Play/Pause (Gaming Mode)");
        },
        nullptr, playId
    );
    mpvHotkeyIds.push_back(playId);
    
    int stopId = mpvBaseId++;
    AddContextualHotkey("XF86AudioStop", "currentMode == 'gaming'", 
        [this]() {
            mpv.Stop();
            logWindowEvent("MPV_HOTKEY", "Stop (Gaming Mode)");
        },
        nullptr, stopId
    );
    mpvHotkeyIds.push_back(stopId);
    
    int nextId = mpvBaseId++;
    AddContextualHotkey("XF86AudioNext", "currentMode == 'gaming'", 
        [this]() {
            mpv.Next();
            logWindowEvent("MPV_HOTKEY", "Next (Gaming Mode)");
        },
        nullptr, nextId
    );
    mpvHotkeyIds.push_back(nextId);
    
    int prevId = mpvBaseId++;
    AddContextualHotkey("XF86AudioPrev", "currentMode == 'gaming'", 
        [this]() {
            mpv.Previous();
            logWindowEvent("MPV_HOTKEY", "Previous (Gaming Mode)");
        },
        nullptr, prevId
    );
    mpvHotkeyIds.push_back(prevId);
    
    // Special keycode-based hotkey for gaming mode
    int kc94Id = mpvBaseId++;
    AddContextualHotkey("kc94", "currentMode == 'gaming'",
        [this]() {
            logHotkeyEvent("KEYPRESS", COLOR_YELLOW + "Keycode 94" + COLOR_RESET);
            mpv.SendCommand({"cycle", "pause"});
        },
        [this]() {
            logHotkeyEvent("KEYPRESS", COLOR_YELLOW + "Keycode 94" + COLOR_RESET);
            io.PressKey("backslash", true);
            io.PressKey("backslash", false);
        },
        kc94Id
    );
    mpvHotkeyIds.push_back(kc94Id);
    
    // If not in gaming mode, immediately ungrab all MPV hotkeys
    if (currentMode != "gaming") {
        lo.info("Starting in normal mode - unregistering MPV hotkeys");
        ungrabMPVHotkeys();
    }
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
    
    // Add contextual hotkey for D key in Koikatu using both class and title detection
    // First check by window class
    AddContextualHotkey("d", "Window.Active('class:Koikatu')",
        [this]() {
            // Show black overlay when D is pressed in Koikatu
            lo.info("Koikatu window detected - D key pressed - showing black overlay");
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (class match)");
        },
        nullptr, // No action when condition is false
        0 // ID parameter
    );
    
    // Also add a title-based hotkey for Koikatu window title (as a fallback)
    AddContextualHotkey("d", "Window.Active('name:Koikatu')",
        [this]() {
            // Show black overlay when D is pressed in Koikatu window
            lo.info("Koikatu window title detected - D key pressed - showing black overlay");
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (title match)");
        },
        nullptr, // No action when condition is false
        0 // ID parameter
    );
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

    // Volume control
    AddHotkey("alt+ctrl+-", [this]() {
        brightnessManager.decreaseBrightness();
    });
    
    AddHotkey("alt+ctrl+=", [this]() {
        brightnessManager.increaseBrightness();
    });
    
    // Brightness control
    AddHotkey("alt+shift+-", [this]() {
        brightnessManager.decreaseGamma();
    });
    
    AddHotkey("alt+shift+=", [this]() {
        brightnessManager.increaseGamma();
    });
    
    // Toggle zooming mode
    AddHotkey("alt+shift+z", [this]() {
        setZooming(!isZooming());
        logWindowEvent("ZOOM_MODE", (isZooming() ? "Enabled" : "Disabled"));
    });
    
    // Add new hotkey for full-screen black window
    AddHotkey("alt+d", [this]() {
        showBlackOverlay();
        logWindowEvent("BLACK_OVERLAY", "Showing full-screen black overlay");
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
    lo.info("Loading hotkey configurations...");
    
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

int HotkeyManager::AddContextualHotkey(const std::string& key, const std::string& condition,
                                std::function<void()> trueAction,
                                std::function<void()> falseAction,
                                int id) {
    // If no ID provided, generate one
    if (id == 0) {
        static int nextId = 1000;
        id = nextId++;
    }
    
    // Convert special key names
    std::string normalizedKey = parseHotkeyString(key);
    
    // Create an action that will evaluate the condition
    auto action = [this, condition, trueAction, falseAction]() {
        // Log the contextual hotkey being evaluated with the condition
        if (verboseKeyLogging) {
            std::string conditionLog = "Evaluating condition: " + condition;
            lo.debug(conditionLog);
        }
        
        // Evaluate the condition
        // Examples of supported conditions:
        // - "IsZooming" - Check if zooming mode is active
        // - "currentMode == 'gaming'" - Check if in gaming mode
        // - "Window.Active('Firefox')" - Check window title contains "Firefox"
        // - "Window.Active('class:Firefox')" - Check window class contains "Firefox"
        // - "Window.Active('name:Gmail')" - Explicitly check window title
        // - "!Window.Active('class:Emacs')" - Negation: Check window class does NOT contain "Emacs"
        bool conditionMet = evaluateCondition(condition);
        
        // Execute the appropriate action based on the condition result
        if (conditionMet && trueAction) {
            if (verboseKeyLogging) {
                lo.debug("Condition met, executing true action");
            }
            trueAction();
        } else if (!conditionMet && falseAction) {
            if (verboseKeyLogging) {
                lo.debug("Condition not met, executing false action");
            }
            falseAction();
        }
    };
    
    // Register the hotkey with the action
    io.Hotkey(normalizedKey, action, id);
    
    return id;
}

bool HotkeyManager::checkWindowCondition(const std::string& condition) {
    // This method checks if a specific window-related condition is met (like class or title)
    // Return true if the condition is met, false otherwise
    
    if (condition.substr(0, 14) == "Window.Active(") {
        std::string param;
        if (condition.length() > 14) {
            param = condition.substr(14, condition.length() - 16);
        }
        
        bool negation = condition[0] == '!';
        if (negation && param.length() > 0) {
            param = param.substr(1);
        }
        
        // Get active window information
        wID activeWindow = WindowManager::GetActiveWindow();
        bool result = false;
        
        if (activeWindow != 0) {
            // If the parameter starts with "class:" it means we want to match by window class
            if (param.substr(0, 6) == "class:") {
                std::string matchValue = param.substr(6);
                
                // Get active window class directly and check for match
                std::string activeWindowClass = WindowManager::GetActiveWindowClass();
                
                if (verboseWindowLogging) {
                    logWindowEvent("WINDOW_CHECK", 
                        "Active window class '" + activeWindowClass + "' checking for '" + matchValue + "'");
                }
                
                // Check if class contains our match string
                result = (activeWindowClass.find(matchValue) != std::string::npos);
            } 
            // If the parameter starts with "name:" it explicitly specifies title matching
            else if (param.substr(0, 5) == "name:") {
                std::string matchValue = param.substr(5);
                
                // Get active window title directly
                try {
                    Window window(std::to_string(activeWindow), activeWindow);
                    std::string activeWindowTitle = window.Title();
                    
                    if (verboseWindowLogging) {
                        logWindowEvent("WINDOW_CHECK", 
                            "Active window title '" + activeWindowTitle + "' checking for '" + matchValue + "'");
                    }
                    
                    // Check if title contains our match string
                    result = (activeWindowTitle.find(matchValue) != std::string::npos);
                } catch (...) {
                    lo.error("Failed to get active window title");
                    result = false;
                }
            }
            // Legacy/default case: just match by title
            else {
                try {
                    Window window(std::to_string(activeWindow), activeWindow);
                    std::string activeWindowTitle = window.Title();
                    
                    if (verboseWindowLogging) {
                        logWindowEvent("WINDOW_CHECK", 
                            "Active window title (legacy) '" + activeWindowTitle + "' checking for '" + param + "'");
                    }
                    
                    // Check if title contains our match string
                    result = (activeWindowTitle.find(param) != std::string::npos);
                } catch (...) {
                    lo.error("Failed to get active window title");
                    result = false;
                }
            }
        }
        
        // Handle negation if present
        if (negation) result = !result;
        
        return result;
    }
    
    return false;
}

void HotkeyManager::updateHotkeyStateForCondition(const std::string& condition, bool conditionMet) {
    // This handles grabbing or ungrabbing hotkeys based on condition state changes
    // We only care about certain conditions like window class/title matches
    
    // First, check if we need to handle this condition
    if (condition.find("currentMode == 'gaming'") != std::string::npos || 
        condition.find("Window.Active('class:Koikatu')") != std::string::npos ||
        condition.find("Window.Active('name:Koikatu')") != std::string::npos) {
        
        // Check if state has changed since last time
        auto it = windowConditionStates.find(condition);
        bool stateChanged = (it == windowConditionStates.end() || it->second != conditionMet);
        
        // Update the stored state
        windowConditionStates[condition] = conditionMet;
        
        // Only take action if the state changed
        if (stateChanged) {
            if (conditionMet) {
                // Condition is now met, grab the keys
                if (!mpvHotkeysGrabbed && (condition.find("currentMode == 'gaming'") != std::string::npos)) {
                    lo.info("Condition met: " + condition + " - Grabbing MPV hotkeys");
                    grabMPVHotkeys();
                }
                
                // Log the state change
        if (verboseWindowLogging) {
                    logWindowEvent("CONDITION_STATE", "Condition now TRUE: " + condition);
                }
            } else {
                // Condition is now not met, ungrab the keys
                if (mpvHotkeysGrabbed && (condition.find("currentMode == 'gaming'") != std::string::npos)) {
                    lo.info("Condition no longer met: " + condition + " - Ungrabbing MPV hotkeys");
                    ungrabMPVHotkeys();
                }
                
                // Log the state change
                if (verboseWindowLogging) {
                    logWindowEvent("CONDITION_STATE", "Condition now FALSE: " + condition);
                }
            }
        }
    }
}

bool HotkeyManager::evaluateCondition(const std::string& condition) {
    bool result = false;
    
    if (condition == "IsZooming") {
        result = isZooming();
    }
    else if (condition == "currentMode == 'gaming'") {
        // Check if current mode is set to gaming
        if (currentMode == "gaming") {
            lo.debug("Evaluating condition: currentMode == 'gaming' is TRUE (already in gaming mode)");
            result = true;
        }
        
        // Or check if the current window is a gaming window
        if (isGamingWindow()) {
            // If in a gaming window, automatically set mode to gaming
            if (currentMode != "gaming") {
                std::string oldMode = currentMode;
                currentMode = "gaming";
                logModeSwitch(oldMode, currentMode);
                
                // Grab hotkeys when switching to gaming mode - this is now handled by updateHotkeyStateForCondition
                
                if (verboseWindowLogging) {
                    logWindowEvent("AUTO_MODE_CHANGE", "Switched to gaming mode based on window class");
                }
                
                lo.info("Auto-switched to gaming mode");
            }
            result = true;
        }
        
        // Not in gaming mode and not a gaming window
        // If we were previously in gaming mode, switch back to default mode
        if (!result && currentMode == "gaming") {
            std::string oldMode = currentMode;
            currentMode = "default";
            logModeSwitch(oldMode, currentMode);
            
            // Ungrab hotkeys when switching out of gaming mode - this is now handled by updateHotkeyStateForCondition
            
            if (verboseWindowLogging) {
                logWindowEvent("AUTO_MODE_CHANGE", "Switched to normal mode");
            }
            
            lo.info("Auto-switched to normal mode");
        }
        
        if (verboseWindowLogging && !result) {
            logWindowEvent("MODE_CHECK", "Not in gaming mode");
        }
    }
    else if (condition.find("Window.Active") == 0) {
        // Use the new window condition check helper
        result = checkWindowCondition(condition);
        
        // Log the result
        if (verboseWindowLogging) {
            logWindowEvent("CONDITION_CHECK", 
                condition + " = " + (result ? "TRUE" : "FALSE"));
        }
    }
    else {
    // Log unrecognized conditions
    lo.warning("Unrecognized condition: " + condition);
    }
    
    // Update hotkey states based on the condition result
    updateHotkeyStateForCondition(condition, result);
    
    return result;
}

void HotkeyManager::grabMPVHotkeys() {
    if (mpvHotkeysGrabbed) {
        // Already grabbed, nothing to do
        return;
    }
    
    // Media keys that should only be grabbed in gaming mode
    const std::vector<std::string> mpvKeys = {
        "XF86AudioRaiseVolume",
        "XF86AudioLowerVolume",
        "XF86AudioMute",
        "XF86AudioPlay",
        "XF86AudioStop",
        "XF86AudioNext",
        "XF86AudioPrev",
        // Also grab our special MPV hotkeys
        "z", "o", "p", "l", "semicolon", "7", "8", "5", "6", "9", "0",
        "minus", "equal", "m"
    };
    
    for (const auto& key : mpvKeys) {
        if (verboseKeyLogging) {
            logKeyEvent(key, "GRAB", "Grabbing for gaming mode");
        }
    }
    
    // Grab all MPV hotkey IDs that we've stored
    for (int id : mpvHotkeyIds) {
        io.GrabHotkey(id);
    }
    
    mpvHotkeysGrabbed = true;
    lo.info("Grabbed all MPV hotkeys for gaming mode");
}

void HotkeyManager::ungrabMPVHotkeys() {
    if (!mpvHotkeysGrabbed) {
        // Already ungrabbed, nothing to do
        return;
    }
    
    // Media keys that should be ungrabbed when not in gaming mode
    const std::vector<std::string> mpvKeys = {
        "XF86AudioRaiseVolume",
        "XF86AudioLowerVolume",
        "XF86AudioMute",
        "XF86AudioPlay",
        "XF86AudioStop",
        "XF86AudioNext",
        "XF86AudioPrev",
        // Also grab our special MPV hotkeys
        "z", "o", "p", "l", "semicolon", "7", "8", "5", "6", "9", "0",
        "minus", "equal", "m"
    };
    
    for (const auto& key : mpvKeys) {
        if (verboseKeyLogging) {
            logKeyEvent(key, "UNGRAB", "Releasing for normal mode");
        }
    }
    
    // Ungrab all MPV hotkey IDs that we've stored
    for (int id : mpvHotkeyIds) {
        io.UngrabHotkey(id);
    }
    
    mpvHotkeysGrabbed = false;
    lo.info("Released all MPV hotkeys for normal mode");
}

void HotkeyManager::showNotification(const std::string& title, const std::string& message) {
    std::string cmd = "notify-send \"" + title + "\" \"" + message + "\"";
    system(cmd.c_str());
}

bool HotkeyManager::isGamingWindow() {
    std::string windowClass = WindowManager::GetActiveWindowClass();
    
    if (verboseWindowLogging) {
        logWindowEvent("CHECK_GAMING", "Checking if window class '" + windowClass + "' is a gaming application");
    }
    
    // List of window classes for gaming applications
    const std::vector<std::string> gamingApps = {
        "steam_app",      // Steam games
        "retroarch",      // RetroArch emulator
        "ryujinx",        // Nintendo Switch emulator
        "pcsx2",          // PlayStation 2 emulator
        "dolphin-emu",    // GameCube/Wii emulator
        "rpcs3",          // PlayStation 3 emulator
        "cemu",           // Wii U emulator
        "yuzu",           // Another Switch emulator
        "duckstation",    // PlayStation 1 emulator
        "ppsspp",         // PSP emulator
        "xemu",           // Original Xbox emulator
        "wine",           // Wine (Windows games on Linux)
        "lutris",         // Lutris game launcher
        "heroic",         // Epic Games launcher for Linux
        "gamescope",      // Valve's gaming compositor
        "games"           // Generic games category
    };
    
    // Check if window class contains any gaming app identifier
    for (const auto& app : gamingApps) {
        if (windowClass.find(app) != std::string::npos) {
            if (verboseWindowLogging) {
                logWindowEvent("GAMING_DETECTED", "Gaming application detected: '" + app + "' in '" + windowClass + "'");
            }
            return true;
        }
    }
    
    // Check for specific full class names
    const std::vector<std::string> exactGamingClasses = {
        "Minecraft",
        "minecraft-launcher",
        "factorio",
        "stardew_valley",
        "terraria",
        "dota2",
        "csgo",
        "goggalaxy",      // GOG Galaxy
        "MangoHud"        // Often used with games
    };
    
    for (const auto& exactClass : exactGamingClasses) {
        if (windowClass == exactClass) {
            if (verboseWindowLogging) {
                logWindowEvent("GAMING_DETECTED", "Gaming application detected by exact match: '" + exactClass + "'");
            }
            return true;
        }
    }
    
    if (verboseWindowLogging) {
        logWindowEvent("NOT_GAMING", "Window class '" + windowClass + "' is not a gaming application");
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
    const std::regex fkeyRegex("^f([1-9]|1[0-9]|2[0-4])$");
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
        // For direct keycodes, pass through as-is
        result = keyName;
        logKeyConversion(keyName, result);
        return result;
    }
    
    // Handle scancodes (scXXX)
    if (keyName.substr(0, 2) == "sc") {
        result = handleScancode(keyName);
        logKeyConversion(keyName, result);
        return result;
    }
    
    // Special case for Menu key
    if (keyName == "Menu") {
        result = "kc135"; // Direct keycode for Menu key
        logKeyConversion(keyName, result);
        return result;
    }
    
    // Special case for NoSymbol
    if (keyName == "NoSymbol") {
        result = "kc0";
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
    std::istringstream ss(hotkeyStr);
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
    
    // Get window info with current active window
    wID activeWindow = WindowManager::GetActiveWindow();
    std::string windowClass = WindowManager::GetActiveWindowClass();
    
    // Get window title
    std::string windowTitle;
    try {
        Window window(std::to_string(activeWindow), activeWindow);
        windowTitle = window.Title();
    } catch (...) {
        windowTitle = "<error getting title>";
    }
    
    // Format window info
    std::string windowInfo = COLOR_BOLD + COLOR_CYAN + "Class: " + COLOR_RESET + windowClass + 
                             COLOR_BOLD + COLOR_CYAN + " | Title: " + COLOR_RESET + windowTitle + 
                             COLOR_BOLD + COLOR_CYAN + " | ID: " + COLOR_RESET + std::to_string(activeWindow);
    
    std::string detailInfo = details.empty() ? "" : " (" + COLOR_GREEN + details + COLOR_RESET + ")";
    
    lo.info(timestamp + " " + type + " " + windowInfo + detailInfo);
}

std::string HotkeyManager::getWindowInfo(wID windowId) {
    if (windowId == 0) {
        windowId = WindowManager::GetActiveWindow();
    }
    
    std::string windowClass;
    std::string title;
    
    // Get window class
    if (windowId) {
        try {
            if (windowId == WindowManager::GetActiveWindow()) {
                windowClass = WindowManager::GetActiveWindowClass();
            } else {
                // For non-active windows we might need a different approach
                windowClass = "<not implemented for non-active>";
            }
        } catch (...) {
            windowClass = "<error>";
        }
    } else {
        windowClass = "<no window>";
    }
    
    // Get window title
    try {
        Window window(std::to_string(windowId), windowId);
        title = window.Title();
    } catch (...) {
        title = "<error getting title>";
    }
    
    return COLOR_BOLD + COLOR_CYAN + "Class: " + COLOR_RESET + windowClass + 
           COLOR_BOLD + COLOR_CYAN + " | Title: " + COLOR_RESET + title + 
           COLOR_BOLD + COLOR_CYAN + " | ID: " + COLOR_RESET + std::to_string(windowId);
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

// Implement setMode to handle mode changes and key grabbing
void HotkeyManager::setMode(const std::string& mode) {
    // Don't do anything if the mode isn't changing
    if (mode == currentMode) return;
    
    std::string oldMode = currentMode;
    currentMode = mode;
    
    // Log the mode change
    logModeSwitch(oldMode, currentMode);
    
    // Update window condition state for our gaming mode condition
    // This will trigger the appropriate hotkey grab/ungrab
    updateHotkeyStateForCondition("currentMode == 'gaming'", currentMode == "gaming");
    
    if (verboseWindowLogging) {
        logWindowEvent("MODE_CHANGE", 
            oldMode + " → " + currentMode + 
            (currentMode == "gaming" ? " (MPV hotkeys active)" : " (MPV hotkeys inactive)"));
    }
}

void HotkeyManager::showBlackOverlay() {
    // Log that we're showing the black overlay
    lo.info("Showing black overlay window on all monitors");
    
    Display* display = DisplayManager::GetDisplay();
    if (!display) {
        lo.error("Failed to get display for black overlay");
        return;
    }
    
    // Get the root window - use X11's Window type here, not our Window class
    ::Window rootWindow = DefaultRootWindow(display);
    
    // Get screen dimensions (this will get the primary monitor dimensions)
    Screen* screen = DefaultScreenOfDisplay(display);
    int screenWidth = WidthOfScreen(screen);
    int screenHeight = HeightOfScreen(screen);
    
    // Check if we can get all monitor information
    int numMonitors = 0;
    bool multiMonitorSupport = false;
    
    #ifdef HAVE_XRANDR
    // Use XRandR to get multi-monitor information
    XRRScreenResources* resources = XRRGetScreenResources(display, rootWindow);
    if (resources) {
        numMonitors = resources->noutput;
        multiMonitorSupport = true;
        
        // Get total dimensions encompassing all monitors
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
        
        for (int i = 0; i < resources->noutput; i++) {
            XRROutputInfo* outputInfo = XRRGetOutputInfo(display, resources, resources->outputs[i]);
            if (outputInfo && outputInfo->connection == RR_Connected) {
                // Find the CRTC for this output
                XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(display, resources, outputInfo->crtc);
                if (crtcInfo) {
                    // Update bounds
                    minX = std::min(minX, crtcInfo->x);
                    minY = std::min(minY, crtcInfo->y);
                    maxX = std::max(maxX, crtcInfo->x + (int)crtcInfo->width);
                    maxY = std::max(maxY, crtcInfo->y + (int)crtcInfo->height);
                    
                    XRRFreeCrtcInfo(crtcInfo);
                }
            }
            if (outputInfo) {
                XRRFreeOutputInfo(outputInfo);
            }
        }
        
        // If we got valid dimensions
        if (maxX > minX && maxY > minY) {
            screenWidth = maxX - minX;
            screenHeight = maxY - minY;
        }
        
        XRRFreeScreenResources(resources);
    }
    #endif
    
    // Create black window attributes
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;  // Bypass window manager
    attrs.background_pixel = BlackPixel(display, DefaultScreen(display));
    attrs.border_pixel = BlackPixel(display, DefaultScreen(display));
    attrs.event_mask = ButtonPressMask | KeyPressMask;  // Capture events to close it
    
    // Create the black window - save as X11's Window type, not our Window class
    ::Window blackWindow = XCreateWindow(display, 
                                      rootWindow,
                                      0, 0,                            // x, y
                                      screenWidth, screenHeight,       // width, height
                                      0,                               // border width
                                      CopyFromParent,                  // depth
                                      InputOutput,                     // class
                                      CopyFromParent,                  // visual
                                      CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
                                      &attrs);
    
    // Make it topmost
    XSetTransientForHint(display, blackWindow, rootWindow);
    
    // Map the window
    XMapRaised(display, blackWindow);
    XFlush(display);
    
    // Create a thread to handle events (to close the window)
    std::thread eventThread([display, blackWindow]() {
        XEvent event;
        bool running = true;
        
        // Set up timeout for auto-closing (5 minutes)
        auto startTime = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::minutes(5);
        
        while (running) {
            // Check for timeout
            auto now = std::chrono::steady_clock::now();
            if (now - startTime > timeout) {
                lo.info("Black overlay auto-closed after timeout");
                running = false;
                break;
            }
            
            // Check for events
            while (XPending(display) > 0) {
                XNextEvent(display, &event);
                
                // Close on any key press or mouse click
                if (event.type == KeyPress || event.type == ButtonPress) {
                    running = false;
                    lo.info("Black overlay closed by user input");
                    break;
                }
            }
            
            // Sleep to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Destroy the window
        XDestroyWindow(display, blackWindow);
        XFlush(display);
    });
    
    // Detach the thread to let it run independently
    eventThread.detach();
}

} // namespace H 