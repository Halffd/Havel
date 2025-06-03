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
#include <atomic>
#include "core/DisplayManager.hpp"
#include "media/AutoRunner.h"
// Include XRandR for multi-monitor support
#ifdef __linux__
#include <X11/extensions/Xrandr.h>
#endif
namespace havel {
std::string HotkeyManager::currentMode = "default";
void HotkeyManager::Zoom(int zoom, IO& io) {
    if (zoom < 0) zoom = 0;
    else if (zoom > 2) zoom = 2;
    if (zoom == 1) {
        io.Send("^{up}");
    } else if (zoom == 0) {
        io.Send("^{down}");
    } else if (zoom == 2) {
        io.Send("^/");
    } else if (zoom == 3) {
        io.Send("^+/");
    } else {
        std::cout << "Invalid zoom level: " << zoom << std::endl;
    }
}
HotkeyManager::HotkeyManager(IO& io, WindowManager& windowManager, MPVController& mpv, ScriptEngine& scriptEngine)
    : io(io), windowManager(windowManager), mpv(mpv), scriptEngine(scriptEngine),
      brightnessManager(), verboseKeyLogging(false), verboseWindowLogging(false),
      mpvHotkeysGrabbed(false), trackWindowFocus(false), lastActiveWindowId(0) {
    loadVideoSites();
}

void HotkeyManager::loadVideoSites() {
    // Clear existing sites
    videoSites.clear();

    // Get video sites from config
    std::string sitesStr = havel::Configs::Get().Get<std::string>("VideoSites.Sites", "netflix,animelon,youtube");

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
    io.Hotkey("^!g", [this]() {
        std::string oldMode = currentMode;
        currentMode = (currentMode == "gaming") ? "default" : "gaming";
        logModeSwitch(oldMode, currentMode);
        showNotification("Mode Changed", "Active mode: " + currentMode);
    });

    // Basic application hotkeys
    io.Hotkey("^!r", [this]() {
        ReloadConfigurations();
        lo.info("Reloading configuration");
    });

    io.Hotkey("!Esc", []() {
        lo.info("Quitting application");
        exit(0);
    });

    // Media Controls
    io.Hotkey("#f1", []() {
        system("playerctl previous");
    });

    io.Hotkey("#f2", []() {
        system("playerctl play-pause");
    });

    io.Hotkey("#f3", []() {
        system("playerctl next");
    });

    io.Hotkey("NumpaAdd", []() {
        system("pactl set-sink-volume @DEFAULT_SINK@ -5%");
    });

    io.Hotkey("NumpaSub", []() {
        system("pactl set-sink-volume @DEFAULT_SINK@ +5%");
    });

    io.Hotkey("+f6", [this]() {
        mpv.SendCommand({"cycle", "pause"});
    });

    // Application Shortcuts
    io.Hotkey("@rwin", [this]() {
        std::cout << "rwin" << std::endl;
        io.Send("@!{backspace}");
    });

    io.Hotkey("@ralt", [this]() {
        std::cout << "ralt" << std::endl;
        WindowManager::MoveWindowToNextMonitor();
    });
    io.Hotkey("~Button1", [this]() {
        lo.info("Button1");
        mouse1Pressed = true;
    });
    io.Hotkey("~Button2", [this]() {
        lo.info("Button2");
        mouse2Pressed = true;
    });

    io.Hotkey("KP_7", [this]() {
        Zoom(1, io);
    });

    io.Hotkey("KP_1", [this]() {
        Zoom(0, io);
    });

    io.Hotkey("KP_5", [this]() {
        Zoom(2, io);
    });

    io.Hotkey("!f1", []() {
        system("~/scripts/f1.sh -1");
    });

    io.Hotkey("+!l", []() {
        system("~/scripts/livelink.sh");
    });

    io.Hotkey("^!l", []() {
        system("livelink screen toggle 1");
    });
    io.Hotkey("f10", [this]() {
        system("~/scripts/str");
    });
    io.Hotkey("^!k", []() {
        system("livelink screen toggle 2");
    });

    // Context-sensitive hotkeys
    AddContextualHotkey(" @nosymbol", "IsZooming",
        [this]() { // When zooming
            std::cout << "kc0 t" << std::endl;
            Zoom(2, io);
        },
        [this]() { // When not zooming
            Zoom(3, io);
        },
        0  // ID parameter
    );

    AddContextualHotkey("!x", "!Window.Active('name:Emacs')",
        []() {
            system("alacritty");
        },
        nullptr, // falseAction parameter
        0       // ID parameter
    );

    // Window Management
    io.Hotkey("#left", []() {
        lo.debug("Moving window left");
        WindowManager::MoveWindow(3);
    });

    io.Hotkey("#right", [this]() {
        lo.debug("Moving window right");
        // Move window to next monitor using MoveWindow(4) for right movement
        WindowManager::MoveWindow(4);
    });

    // Quick window switching hotkeys
    auto switchToLastWindow = []() {
        lo.debug("Switching to last window");
        WindowManager::AltTab();
    };

    io.Hotkey("^!t", switchToLastWindow);

    // Emergency Features
    const std::map<std::string, std::pair<std::string, std::function<void()>>> emergencyHotkeys = {
        {"f9", {"Suspend hotkeys", [this]() {
            lo.info("Suspending all hotkeys");
            io.Suspend(0); // Special case: 0 means suspend all hotkeys
            lo.debug("Hotkeys suspended");
        }}},
        {"#Esc", {"Restart application", [this]() {
            lo.info("Restarting application");
            // Get current executable path using the correct namespace
            std::string exePath = havel::GetExecutablePath();
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
        {"^#esc", {"Reload configuration", [this]() {
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

    io.Hotkey("+f7", [this]() {
        lo.info("Decreasing gamma");
        brightnessManager.decreaseGamma(500);
        lo.info("Current gamma: " + std::to_string(brightnessManager.getCurrentGamma()));
    });

    io.Hotkey("+f8", [this]() {
        lo.info("Increasing gamma");
        brightnessManager.increaseGamma(500);
        lo.info("Current gamma: " + std::to_string(brightnessManager.getCurrentGamma()));
    });

    // Mouse wheel + click combinations
    io.Hotkey("!Button5", [this]() {
        std::cout << "alt+Button5" << std::endl;
        Zoom(1, io);
    });
    // Add contextual hotkey for D key in Koikatu
    AddHotkey("!d", [this]() {
            // Show black overlay when D is pressed in Koikatu
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (class match)");
        });

    // Also add a title-based hotkey for Koikatu window title (as a fallback)
    AddContextualHotkey("~d", "Window.Active('name:Koikatu')",
        [this]() {
            // Show black overlay when D is pressed in Koikatu window
            lo.info("Koikatu window title detected - D key pressed - showing black overlay");
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (title match)");
        },
        nullptr, // No action when condition is false
        0 // ID parameter
    );

    // '/' key to hold 'w' down in gaming mode
    AddContextualHotkey("@y", "currentMode == 'gaming'",
        [this]() {
            lo.info("Gaming hotkey: Holding 'w' key down");
            io.Send("{w down}");

            // Register the same key to release when pressed again
            static bool wKeyPressed = false;
            wKeyPressed = !wKeyPressed;

            if (wKeyPressed) {
                lo.info("W key pressed and held down");
            } else {
                io.Send("{w up}");
                lo.info("W key released");
            }
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // "'" key to move mouse to coordinates and autoclick in gaming mode
    AddContextualHotkey("'", "currentMode == 'gaming'",
        [this]() {
            lo.info("Gaming hotkey: Moving mouse to 1600,700 and autoclicking");
            // Move mouse to coordinates
            std::string moveCmd = "xdotool mousemove 1600 700";
            system(moveCmd.c_str());

            // Start autoclicking
            startAutoclicker("Button1");
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // autoclick in gaming mode
    AddContextualHotkey("#Enter", "currentMode == 'gaming'",
        [this]() {
            lo.info("Gaming hotkey: Starting autoclicker with Enter key");
            startAutoclicker("Button1");
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // Add a variable to track and allow stopping Genshin automation
    static std::atomic<bool> genshinAutomationActive(false);
    static AutoRunner genshinAutoRunner(io);
    AddContextualHotkey("/", "currentMode == 'gaming'",
        [this]() {
            lo.info("Genshin Impact detected - Starting specialized auto actions");
            genshinAutoRunner.toggle();
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // Special hotkeys for Genshin Impact - Start automation
    AddContextualHotkey("enter", "currentMode == 'gaming'", [this]() {
    if (genshinAutomationActive) {
        lo.warning("Genshin automation is already active");
        return;
    }

    lo.info("Genshin Impact detected - Starting specialized auto actions");
    showNotification("Genshin Automation", "Starting automation sequence");
    genshinAutomationActive = true;

    startAutoclicker("Button1");

    // Launch automation thread
    std::thread([this]() {
        const int maxIterations = 300;
        int counter = 0;

        while (counter < maxIterations && genshinAutomationActive && currentMode == "gaming") {
            // Verify Genshin window is active
            bool isGenshinActive = false;
            wID activeWindow = WindowManager::GetActiveWindow();

            if (activeWindow != 0) {
                try {
                    Window window(std::to_string(activeWindow), activeWindow);
                    isGenshinActive = window.Title().find("Genshin") != std::string::npos;
                } catch (...) {
                    lo.error("Genshin automation: Could not get window title");
                    break;
                }
            }

            if (!isGenshinActive) {
                lo.info("Genshin automation: Window no longer active");
                break;
            }

            // Press E
            io.Send("e");
            lo.debug("Genshin automation: Pressed E (" + std::to_string(counter + 1) + "/" + std::to_string(maxIterations) + ")");

            // Every 5th loop, press Q
            if (counter % 5 == 0) {
                io.Send("q");
                lo.debug("Genshin automation: Pressed Q");
            }

            counter++;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        genshinAutomationActive = false;
        lo.info("Genshin automation: Automation ended");

    }).detach();

}, nullptr, 0);
    // Add hotkey to stop Genshin automation
    AddHotkey("!+g", [this]() {
        if (genshinAutomationActive) {
            genshinAutomationActive = false;
            lo.info("Manually stopping Genshin automation");
            showNotification("Genshin Automation", "Automation sequence stopped");
        } else {
            lo.info("Genshin automation is not active");
            showNotification("Genshin Automation", "No active automation to stop");
        }
    });
}
void HotkeyManager::PlayPause() {
    if (mpv.IsSocketAlive()) {
        mpv.SendCommand({"cycle", "pause"});
    } else {
        // Send playerctl
        system("playerctl play-pause");
    }
}
void HotkeyManager::RegisterMediaHotkeys() {
    int mpvBaseId = 10000;
std::vector<HotkeyDefinition> mpvHotkeys = {
    // Volume
    { "+0", "currentMode == 'gaming'", [this]() { mpv.VolumeUp(); }, nullptr, mpvBaseId++ },
    { "+9", "currentMode == 'gaming'", [this]() { mpv.VolumeDown(); }, nullptr, mpvBaseId++ },
    { "+-", "currentMode == 'gaming'", [this]() { mpv.ToggleMute(); }, nullptr, mpvBaseId++ },

    // Playback
    { "@RCtrl", "currentMode == 'gaming'", [this]() { PlayPause(); }, nullptr, mpvBaseId++ },
    { "+Esc", "currentMode == 'gaming'", [this]() { mpv.Stop(); }, nullptr, mpvBaseId++ },
    { "+PgUp", "currentMode == 'gaming'", [this]() { mpv.Next(); }, nullptr, mpvBaseId++ },
    { "+PgDn", "currentMode == 'gaming'", [this]() { mpv.Previous(); }, nullptr, mpvBaseId++ },

    // Netflix pause fallback — same key
    { "@LWin", "currentMode == 'gaming'", [this]() {
        PlayPause();
    }, []() { system("xfce4-popup-whiskermenu"); }, mpvBaseId++ },

    // Seek
    { "o", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"seek", "-3"}); }, nullptr, mpvBaseId++ },
    { "p", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"seek", "3"}); }, nullptr, mpvBaseId++ },

    // Speed
    { "+o", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"add", "speed", "-0.1"}); }, nullptr, mpvBaseId++ },
    { "+p", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"add", "speed", "0.1"}); }, nullptr, mpvBaseId++ },

    // Subtitles
    { "n", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"cycle", "sub-visibility"}); }, nullptr, mpvBaseId++ },
    { "+n", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"cycle", "secondary-sub-visibility"}); }, nullptr, mpvBaseId++ },
    { "7", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"add", "sub-scale", "-0.1"}); }, nullptr, mpvBaseId++ },
    { "8", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"add", "sub-scale", "0.1"}); }, nullptr, mpvBaseId++ },
    { "+z", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"add", "sub-delay", "-0.1"}); }, nullptr, mpvBaseId++ },
    { "+x", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"add", "sub-delay", "0.1"}); }, nullptr, mpvBaseId++ },
    { "9", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"cycle", "sub"}); }, nullptr, mpvBaseId++ },
    { "0", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"sub-seek", "0"}); }, nullptr, mpvBaseId++ },
    { "m", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"script-binding", "copy_current_subtitle"}); }, nullptr, mpvBaseId++ },
    { "minus", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"sub-seek", "-1"}); }, nullptr, mpvBaseId++ },
    { "equal", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"sub-seek", "1"}); }, nullptr, mpvBaseId++ },

    // Special Keycode (94)
    { "<", "currentMode == 'gaming'",
        [this]() {
            logHotkeyEvent("KEYPRESS", COLOR_YELLOW + "Keycode 94" + COLOR_RESET);
            PlayPause();
        }, nullptr, mpvBaseId++
    }
};
    for (const auto& hk : mpvHotkeys) {
        AddContextualHotkey(hk.key, hk.condition, hk.trueAction, hk.falseAction, hk.id);
        conditionalHotkeyIds.push_back(hk.id);
    }

    // If not in gaming mode, immediately ungrab all MPV hotkeys
    if (currentMode != "gaming") {
        lo.info("Starting in normal mode - unregistering MPV hotkeys");
        ungrabGamingHotkeys();
    }
}

void HotkeyManager::RegisterWindowHotkeys() {
    // Window movement
    io.Hotkey("^!Up", []() {
        WindowManager::MoveWindow(1);
    });

    io.Hotkey("^!Down", []() {
        WindowManager::MoveWindow(2);
    });

    io.Hotkey("^!Left", []() {
        WindowManager::MoveWindow(3);
    });

    io.Hotkey("^!Right", []() {
        WindowManager::MoveWindow(4);
    });

    // Window resizing
    io.Hotkey("+!Up", []() {
        WindowManager::ResizeWindow(1);
    });

    io.Hotkey("!+Down", []() {
        WindowManager::ResizeWindow(2);
    });

    io.Hotkey("!+Left", []() {
        WindowManager::ResizeWindow(3);
    });

    io.Hotkey("!+Right", []() {
        WindowManager::ResizeWindow(4);
    });

    // Window always on top
    io.Hotkey("!a", []() {
        WindowManager::ToggleAlwaysOnTop();
    });
    /*io.Hotkey("a", [this]() {
        io.Send("w");
    });

    io.Hotkey("left", [this]() {
        io.Send("a");
    });*/
    // Add contextual hotkey for D key in Koikatu using both class and title detection
    // First check by window class
    /*AddContextualHotkey("d", "Window.Active('class:Koikatu')",
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
    );*/
}

void HotkeyManager::RegisterSystemHotkeys() {
    // System commands
    io.Hotkey("#l", []() {
        // Lock screen
        system("xdg-screensaver lock");
    });

    io.Hotkey("+!Esc", []() {
        // Show system monitor
        system("gnome-system-monitor &");
    });

    AddHotkey("f7", [this]() {
        brightnessManager.decreaseBrightness();
    });

    AddHotkey("f8", [this]() {
        brightnessManager.increaseBrightness();
    });

    // Toggle zooming mode
    AddHotkey("!+z", [this]() {
        setZooming(!isZooming());
        logWindowEvent("ZOOM_MODE", (isZooming() ? "Enabled" : "Disabled"));
    });

    // Add new hotkey for full-screen black window
    AddHotkey("!d", [this]() {
        showBlackOverlay();
        logWindowEvent("BLACK_OVERLAY", "Showing full-screen black overlay");
    });

    // Add new hotkey to print active window info
    AddHotkey("#2", [this]() {
        printActiveWindowInfo();
    });

    // Add new hotkey to toggle automatic window focus tracking
    AddHotkey("!+i", [this]() {
        toggleWindowFocusTracking();
    });

    AddHotkey("^!d", [this]() {
        // Toggle verbose condition logging
        setVerboseConditionLogging(!verboseConditionLogging);
        Configs::Get().Set<bool>("Debug.VerboseConditionLogging", verboseConditionLogging);
        Configs::Get().Save();

        std::string status = verboseConditionLogging ? "enabled" : "disabled";
        lo.info("Verbose condition logging " + status);
        showNotification("Debug Setting", "Condition logging " + status);
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
    // Generate unique ID if none provided
    if (id == 0) {
        static int nextId = 1000;
        id = nextId++;
    }

    // Wrap action in condition check
    auto action = [this, condition, trueAction, falseAction]() {
        if (verboseKeyLogging)
            lo.debug("Evaluating condition: " + condition);

        bool conditionMet = evaluateCondition(condition);

        if (conditionMet) {
            if (trueAction) {
                if (verboseKeyLogging)
                    lo.debug("Condition met, executing true action");
                trueAction();
            }
        } else {
            if (falseAction) {
                if (verboseKeyLogging)
                    lo.debug("Condition not met, executing false action");
                falseAction();
            }
        }
    };

    // Register hotkey
    HotKey hk = io.AddHotkey(key, action, id);

    // Cache or react to initial state
    updateHotkeyStateForCondition(condition, evaluateCondition(condition));

    conditionalHotkeyIds.push_back(id);
    return id;
}

bool HotkeyManager::checkWindowCondition(const std::string& condition) {
    // This method checks if a specific window-related condition is met (like class or title)
    // Return true if the condition is met, false otherwis
    if (condition.substr(0, 14) == "Window.Active(") {
        std::string param;
        if (condition.length() > 14) {
            param = condition.substr(14, condition.length() - 16);
        }

        bool negation = condition[0] == '!';
        if (negation && param.length() > 0) {
            param = param.substr(1);
        }
        auto value = param.substr(param.find(':') != std::string::npos ? param.rfind(':') + 1 : 0);

        // Get active window information
        wID activeWindow = WindowManager::GetActiveWindow();
        bool result = false;

        if (activeWindow != 0) {
            // If the parameter starts with "class:" it means we want to match by window class
            if (param.substr(0, 6) == "class:") {
                // Get active window class directly and check for match
                std::string activeWindowClass = WindowManager::GetActiveWindowClass();

                if (verboseWindowLogging) {
                    logWindowEvent("WINDOW_CHECK",
                        "Active window class '" + activeWindowClass + "' checking for '" + value + "'");
                }

                // Check if class contains our match string
                result = (activeWindowClass.find(value) != std::string::npos);
            }
            // If the parameter starts with "name:" it explicitly specifies title matching
            else if (param.substr(0, 5) == "name:") {
                // Get active window title directly
                try {
                    Window window(std::to_string(activeWindow), activeWindow);
                    std::string activeWindowTitle = window.Title();

                    if (verboseWindowLogging) {
                        logWindowEvent("WINDOW_CHECK",
                            "Active window title '" + activeWindowTitle + "' checking for '" + value + "'");
                    }

                    // Check if title contains our match string
                    result = (activeWindowTitle.find(value) != std::string::npos);
                } catch (...) {
                    lo.error("Failed to get active window title");
                    result = false;
                }
            }
            // Legacy/default case: just match by title
            else {
                try {
                    std::string activeWindowTitle = WindowManager::activeWindow.title;

                    if (verboseWindowLogging) {
                        logWindowEvent("WINDOW_CHECK",
                            "Active window title (legacy) '" + activeWindowTitle + "' checking for '" + param + "'");
                    }

                    // Check if title contains our match string
                    result = (activeWindowTitle.find(value) != std::string::npos);
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
        condition.find("Window.Active") != std::string::npos) {

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
                    grabGamingHotkeys();
                }

                // Log the state change
        if (verboseWindowLogging) {
                    logWindowEvent("CONDITION_STATE", "Condition now TRUE: " + condition);
                }
            } else {
                // Condition is now not met, ungrab the keys
                if (mpvHotkeysGrabbed && (condition.find("currentMode == 'gaming'") != std::string::npos)) {
                    lo.info("Condition no longer met: " + condition + " - Ungrabbing MPV hotkeys");
                    ungrabGamingHotkeys();
                }

                // Log the state change
                if (verboseWindowLogging) {
                    logWindowEvent("CONDITION_STATE", "Condition now FALSE: " + condition);
                }
            }
        }
    }
}
void HotkeyManager::checkHotkeyStates() {
    // Example: check if we’re in gaming mode
    bool gamingModeActive = isGamingWindow();
    updateHotkeyStateForCondition("currentMode == 'gaming'", gamingModeActive);

    // Example: check if the active window matches some criteria
    std::string activeWindowTitle = WindowManager::activeWindow.title;
    std::string activeWindowClass = WindowManager::activeWindow.className;
    for (auto const& [key, value] : windowConditionStates) {
        bool conditionMet = evaluateCondition(key);
        updateHotkeyStateForCondition(key, conditionMet);
    }
}

bool HotkeyManager::evaluateCondition(const std::string& condition) {
    bool negated = !condition.empty() && condition[0] == '!';
    std::string actual = negated ? condition.substr(1) : condition;

    if (verboseWindowLogging && negated)
        logWindowEvent("CONDITION_CHECK", "Detected negation: " + actual);

    bool result = false;

    if (actual == "IsZooming") {
        result = isZooming();
    }
    else if (actual == "currentMode == 'gaming'") {
        result = (currentMode == "gaming");

        if (isGamingWindow()) {
            if (currentMode != "gaming") {
                std::string old = currentMode;
                currentMode = "gaming";
                logModeSwitch(old, currentMode);
                if (verboseWindowLogging)
                    logWindowEvent("AUTO_MODE_CHANGE", "Switched to gaming mode (detected)");
                lo.info(WindowManager::activeWindow.className);
                lo.info(WindowManager::activeWindow.title);
                lo.info("Auto-switched to gaming mode (detected)");
            }
            result = true;
        }

        if (!result && currentMode == "gaming") {
            std::string old = currentMode;
            currentMode = "default";
            logModeSwitch(old, currentMode);
            if (verboseWindowLogging)
                logWindowEvent("AUTO_MODE_CHANGE", "Switched to normal mode");
            lo.info("Auto-switched to normal mode");
        }

        if (verboseWindowLogging && !result)
            logWindowEvent("MODE_CHECK", "Not in gaming mode");
    }
    else if (actual.rfind("Window.Active", 0) == 0) {
        result = checkWindowCondition(condition);
        if (verboseWindowLogging)
            logWindowEvent("CONDITION_CHECK", actual + " = " + (result ? "TRUE" : "FALSE"));
    }
    else {
        lo.warning("Unrecognized condition: " + actual);
    }

    if (negated) {
        result = !result;
        if (verboseWindowLogging)
            logWindowEvent("CONDITION_RESULT", "Final result after negation: " + std::string(result ? "TRUE" : "FALSE"));
    } else if (verboseWindowLogging) {
        logWindowEvent("CONDITION_RESULT", "Final result: " + std::string(result ? "TRUE" : "FALSE"));
    }

    updateHotkeyStateForCondition(condition, result);

    // Log focus change if enabled
    if (trackWindowFocus) {
        wID active = WindowManager::GetActiveWindow();
        if (active != lastActiveWindowId && active != 0) {
            lastActiveWindowId = active;
            printActiveWindowInfo();
        }
    }

    return result;
}

void HotkeyManager::grabGamingHotkeys() {
    if (mpvHotkeysGrabbed) {
        // Already grabbed, nothing to do
        return;
    }

    int i = 0;
    for (const auto& [id, hotkey] : IO::hotkeys) {
        std::cout << "Index " << i << ": ID = " << id << " alias " << hotkey.alias << "\n";
        ++i;
    }
    // Grab all MPV hotkey IDs that we've stored
    for (int id : conditionalHotkeyIds) {
        lo.info("Grabbing hotkey: " + std::to_string(id));
        io.GrabHotkey(id);
    }

    mpvHotkeysGrabbed = true;
    lo.info("Grabbed all MPV hotkeys for gaming mode");
}

void HotkeyManager::ungrabGamingHotkeys() {
    if (!mpvHotkeysGrabbed) {
        // Already ungrabbed, nothing to do
        return;
    }

    // Ungrab all MPV hotkey IDs that we've stored
    for (int id : conditionalHotkeyIds) {
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
    std::string windowClass = WindowManager::activeWindow.className;

    // List of window classes for gaming applications
    const std::vector<std::string> gamingApps = {
        "steam_app_default",      // Steam games
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
        "games",           // Generic games category
        "minecraft",
		"nierautomata"
    };
    // Convert window class to lowercase
    std::transform(windowClass.begin(), windowClass.end(), windowClass.begin(), ::tolower);
    // Check if window class contains any gaming app identifier
    for (const auto& app : gamingApps) {
        if (windowClass.find(app) != std::string::npos) {
            currentMode = "gaming";
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
            currentMode = "gaming";
            return true;
        }
    }
    currentMode = "default";
    return false;
}

    void HotkeyManager::startAutoclicker(const std::string& button) {
    wID currentWindow = WindowManager::GetActiveWindow();

    // If already running, stop it (toggle behavior)
    if (autoclickerActive) {
        lo.info("Stopping autoclicker - toggled off");
        autoclickerActive = false;
        if (autoclickerThread.joinable()) {
            autoclickerThread.join();
        }
        return;
    }

    // Not a gaming window? Abort!
    if (!isGamingWindow()) {
        lo.debug("Autoclicker not activated - not in gaming window");
        return;
    }

    // Save the window we're starting in
    autoclickerWindowID = currentWindow;
    autoclickerActive = true;

    lo.info("Starting autoclicker (" + button + ") in window: " + std::to_string(autoclickerWindowID));

    // ONE thread that handles everything
    autoclickerThread = std::thread([this, button]() {
        while (autoclickerActive) {
            // Check if window changed
            wID activeWindow = WindowManager::GetActiveWindow();
            if (activeWindow != autoclickerWindowID) {
                lo.info("Stopping autoclicker - window changed");
                autoclickerActive = false;
                break;
            }

            // Click that mouse!
            if (button == "Button1" || button == "Left") {
                io.Click(MouseButton::Left, MouseAction::Click);
            } else if (button == "Button2" || button == "Right") {
                io.Click(MouseButton::Right, MouseAction::Click);
            } else if (button == "Button3" || button == "Middle") {
                io.Click(MouseButton::Middle, MouseAction::Click);
            } else if (button == "Side1") {
                io.Click(MouseButton::Side1, MouseAction::Click);
            } else if (button == "Side2") {
                io.Click(MouseButton::Side2, MouseAction::Click);
            } else {
                lo.error("Invalid mouse button: " + button);
            }
            // Sleep between clicks (don't destroy the CPU)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        lo.info("Autoclicker thread terminated");
    });

    // Detach so it runs independently
    autoclickerThread.detach();
}

    // Call this when you need to force-stop (e.g., on app exit)
    void HotkeyManager::stopAllAutoclickers() {
    if (autoclickerActive) {
        lo.info("Force stopping all autoclickers");
        autoclickerActive = false;
        if (autoclickerThread.joinable()) {
            autoclickerThread.join();
        }
    }
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

void HotkeyManager::printActiveWindowInfo() {
    wID activeWindow = WindowManager::GetActiveWindow();
    if (activeWindow == 0) {
        lo.info("╔══════════════════════════════════════╗");
        lo.info("║      NO ACTIVE WINDOW DETECTED       ║");
        lo.info("╚══════════════════════════════════════╝");
        return;
    }

    // Create window instance ONCE
    havel::Window window("ActiveWindow", activeWindow);

    // Get window class
    std::string windowClass = WindowManager::GetActiveWindowClass();

    // Get window title
    std::string windowTitle;
    try {
        windowTitle = window.Title();
    } catch (...) {
        windowTitle = "Unknown";
        lo.error("Failed to get active window title");
    }

    // Get window geometry
    int x = 0, y = 0, width = 0, height = 0;
    try {
        havel::Rect rect = window.Pos();
        x = rect.x;
        y = rect.y;
        width = rect.width;
        height = rect.height;
    } catch (...) {
        lo.error("Failed to get window position");
    }

    // Check if it's a gaming window
    bool isGaming = isGamingWindow();

    // Format the geometry string
    std::string geometry = std::to_string(width) + "x" + std::to_string(height) +
                           " @ (" + std::to_string(x) + "," + std::to_string(y) + ")";

    // -- Now print everything with correct padding and line limits --
    auto formatLine = [](const std::string& label, const std::string& value) -> std::string {
        std::string line = label + value;
        if (line.length() > 52) {
            line = line.substr(0, 49) + "...";
        }
        return "║ " + line + std::string(52 - line.length(), ' ') + "║";
    };

    lo.info("╔══════════════════════════════════════════════════════════╗");
    lo.info("║             ACTIVE WINDOW INFORMATION                    ║");
    lo.info("╠══════════════════════════════════════════════════════════╣");
    lo.info(formatLine("Window ID: ", std::to_string(activeWindow)));
    lo.info(formatLine("Window Title: \"", windowTitle + "\""));
    lo.info(formatLine("Window Class: \"", windowClass + "\""));
    lo.info(formatLine("Window Geometry: ", geometry));

    std::string gamingStatus = isGaming ? (COLOR_GREEN + std::string("YES ✓") + COLOR_RESET) : (COLOR_RED + std::string("NO ✗") + COLOR_RESET);
    lo.info(formatLine("Is Gaming Window: ", gamingStatus));

    lo.info(formatLine("Current Mode: ", currentMode));
    lo.info("╚══════════════════════════════════════════════════════════╝");

    // Log to window event history
    logWindowEvent("WINDOW_INFO",
        "Title: \"" + windowTitle + "\", Class: \"" + windowClass +
        "\", Gaming: " + (isGaming ? "YES" : "NO") + ", Geometry: " + geometry);
}

void HotkeyManager::toggleWindowFocusTracking() {
    trackWindowFocus = !trackWindowFocus;

    if (trackWindowFocus) {
        lo.info("Window focus tracking ENABLED - will log all window changes");
        logWindowEvent("FOCUS_TRACKING", "Enabled");

        // Initialize with current window
        lastActiveWindowId = WindowManager::GetActiveWindow();
        if (lastActiveWindowId != 0) {
            printActiveWindowInfo();
        }
    } else {
        lo.info("Window focus tracking DISABLED");
        logWindowEvent("FOCUS_TRACKING", "Disabled");
    }
}

// Implement the debug settings methods
void HotkeyManager::loadDebugSettings() {
    lo.info("Loading debug settings from config");

    // Get settings from config file with default values if not present
    bool keyLogging = Configs::Get().Get<bool>("Debug.VerboseKeyLogging", false);
    bool windowLogging = Configs::Get().Get<bool>("Debug.VerboseWindowLogging", false);
    bool conditionLogging = Configs::Get().Get<bool>("Debug.VerboseConditionLogging", false);

    // Apply the settings
    setVerboseKeyLogging(keyLogging);
    setVerboseWindowLogging(windowLogging);
    setVerboseConditionLogging(conditionLogging);

    // Log the current debug settings
    lo.info("Debug settings: KeyLogging=" + std::to_string(verboseKeyLogging) +
            ", WindowLogging=" + std::to_string(verboseWindowLogging) +
            ", ConditionLogging=" + std::to_string(verboseConditionLogging));
}

void HotkeyManager::initDebugSettings() {
    lo.info("Initializing debug settings in config file");

    // Set default values in config if they don't exist
    Configs::Get().Set<bool>("Debug.VerboseKeyLogging", verboseKeyLogging);
    Configs::Get().Set<bool>("Debug.VerboseWindowLogging", verboseWindowLogging);
    Configs::Get().Set<bool>("Debug.VerboseConditionLogging", verboseConditionLogging);

    // Save the config to persist the settings
    Configs::Get().Save();

    lo.info("Debug settings initialized and saved to config");
}

void HotkeyManager::applyDebugSettings() {
    // Apply current debug settings
    if (verboseKeyLogging) {
        lo.info("Verbose key logging is enabled");
    }

    if (verboseWindowLogging) {
        lo.info("Verbose window logging is enabled");
    }

    if (verboseConditionLogging) {
        lo.info("Verbose condition logging is enabled");
    }

    // Set up config watchers to react to changes in real-time
    Configs::Get().Watch<bool>("Debug.VerboseKeyLogging", [this](bool oldValue, bool newValue) {
        lo.info("Key logging setting changed from " + std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseKeyLogging(newValue);
    });

    Configs::Get().Watch<bool>("Debug.VerboseWindowLogging", [this](bool oldValue, bool newValue) {
        lo.info("Window logging setting changed from " + std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseWindowLogging(newValue);
    });

    Configs::Get().Watch<bool>("Debug.VerboseConditionLogging", [this](bool oldValue, bool newValue) {
        lo.info("Condition logging setting changed from " + std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseConditionLogging(newValue);
    });
}

} // namespace havel
