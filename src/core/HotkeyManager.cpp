#include "HotkeyManager.hpp"
#include "../utils/Logger.hpp"
#include "IO.hpp"
#include "core/BrightnessManager.hpp"
#include "window/Window.hpp"
#include "core/ConfigManager.hpp"
#include "../process/Launcher.hpp"
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
void HotkeyManager::printHotkeys() const {
    static int counter = 0;
    counter++;
    
    info("=== Hotkey Status Report #" + std::to_string(counter) + " ===");
    
    if (IO::hotkeys.empty()) {
        info("No hotkeys registered");
        return;
    }
    
    for (const auto& [id, hotkey] : IO::hotkeys) {
        std::string status = "Hotkey[" + std::to_string(id) + "] " +
                           "alias='" + hotkey.alias + "' " +
                           "key=" + std::to_string(hotkey.key) + " " +
                           "mod=" + std::to_string(hotkey.modifiers) + " " +
                           "action='" + hotkey.action + "' " +
                           "enabled=" + (hotkey.enabled ? "Y" : "N") + " " +
                           "block=" + (hotkey.blockInput ? "Y" : "N") + " " +
                           "excl=" + (hotkey.exclusive ? "Y" : "N") + " " +
                           "succ=" + (hotkey.success ? "Y" : "N") + " " +
                           "susp=" + (hotkey.suspend ? "Y" : "N");
        info(status);
    }
    
    info("=== End Hotkey Report ===");
}
HotkeyManager::HotkeyManager(IO& io, WindowManager& windowManager, MPVController& mpv, ScriptEngine& scriptEngine)
    : io(io),
      windowManager(windowManager),
      mpv(mpv),
      scriptEngine(scriptEngine){
    config = Configs::Get();
    loadVideoSites();
    loadDebugSettings();
    applyDebugSettings();
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
        Launcher::runShell("discord &");
        Launcher::runShell("spotify &");
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
        info("Reloading configuration");
    });

    io.Hotkey("!Esc", []() {
        if (App::instance()) {
            info("Quitting application");
            App::quit();
        }
    });

    // Media Controls
    io.Hotkey("#f1", []() {
        Launcher::runShell("playerctl previous");
    });

    io.Hotkey("#f2", []() {
        Launcher::runShell("playerctl play-pause");
    });

    io.Hotkey("#f3", []() {
        Launcher::runShell("playerctl next");
    });

    io.Hotkey("NumpadAdd", []() {
        Launcher::runShell("pactl set-sink-volume @DEFAULT_SINK@ -5%");
    });

    io.Hotkey("NumpadSub", []() {
        Launcher::runShell("pactl set-sink-volume @DEFAULT_SINK@ +5%");
    });

    io.Hotkey("f6", [this]() {
        PlayPause();
    });

    // Application Shortcuts
    io.Hotkey("@rwin", [this]() {
        std::cout << "rwin" << std::endl;
        io.Send("@!{backspace}");
    });

    io.Hotkey("@ralt", []() {
        std::cout << "ralt" << std::endl;
        WindowManager::MoveWindowToNextMonitor();
    });
    AddContextualHotkey("@lwin", "currentMode == 'gaming'", [this]() {
        PlayPause();
    }, nullptr, 0);

    // If not gaming mode: open whisker menu on keyup
    AddContextualHotkey("@lwin:up", "currentMode == 'gaming'", 
        nullptr,
        []() {
        Launcher::runAsync("/bin/xfce4-popup-whiskermenu");
    }, 0);
    AddContextualHotkey("u", "currentMode == 'gaming'", 
        [this]() {
            if (!holdClick) {
                io.Click(MouseButton::Left, MouseAction::Hold);
                holdClick = true;
            } else {
                io.Click(MouseButton::Left, MouseAction::Release);
                holdClick = false;
            }
        },
        nullptr,
        0);
    io.Hotkey("~Button1", [this]() {
        info("Button1");
        mouse1Pressed = true;
    });
    io.Hotkey("~Button2", [this]() {
        info("Button2");
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

    io.Hotkey("^f1", []() {
        Launcher::runShell("~/scripts/f1.sh -1");
    });

    io.Hotkey("+!l", []() {
        Launcher::runShell("~/scripts/livelink.sh");
    });

    io.Hotkey("^!l", []() {
        Launcher::runShell("livelink screen toggle 1");
    });
    io.Hotkey("f10", []() {
        Launcher::runShell("~/scripts/str");
    });
    io.Hotkey("^!k", []() {
        Launcher::runShell("livelink screen toggle 2");
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
            Launcher::runAsync("/bin/alacritty");
        },
        nullptr, // falseAction parameter
        0       // ID parameter
    );

    // Window Management
    io.Hotkey("#left", []() {
        debug("Moving window left");
        WindowManager::MoveToCorners(3);
    });

    io.Hotkey("#right", []() {
        debug("Moving window right");
        // Move window to next monitor using MoveWindow(4) for right movement
        WindowManager::MoveToCorners(4);
    });
    io.Hotkey("$f9", [this]() {
        info("Suspending all hotkeys");
        io.Suspend(); // Special case: 0 means suspend all hotkeys
        debug("Hotkeys suspended");
    });
    // Quick window switching hotkeys
    auto switchToLastWindow = []() {
        debug("Switching to last window");
        WindowManager::AltTab();
    };

    io.Hotkey("^!t", switchToLastWindow);

    // Emergency Features
    const std::map<std::string, std::pair<std::string, std::function<void()>>> emergencyHotkeys = {
        {"#Esc", {"Restart application", []() {
            info("Restarting application");
            // Get current executable path using the correct namespace
            std::string exePath = havel::GetExecutablePath();
            if (!exePath.empty()) {
                debug("Executable path: " + exePath);
                // Fork and exec to restart
                if(fork() == 0) {
                    debug("Child process started, executing: " + exePath);
                    execl(exePath.c_str(), exePath.c_str(), nullptr);
                    error("Failed to restart application");
                    exit(1); // Only reached if execl fails
                }
                info("Parent process exiting for restart");
                if (App::instance()) {
                    App::quit();
                }
            } else {
                error("Failed to get executable path");
            }
        }}},
        {"^#esc", {"Reload configuration", [this]() {
            info("Reloading configuration");
            ReloadConfigurations();
            debug("Configuration reload complete");
        }}}
    };

    // Register all emergency hotkeys
    for (const auto& [key, hotkeyInfo] : emergencyHotkeys) {
        const auto& [description, action] = hotkeyInfo;
        io.Hotkey(key, [description, action]() {
            info("Executing emergency hotkey: " + description);
            action();
        });
    }

    // Brightness and gamma control
    io.Hotkey("f3", [this]() {
        info("Setting default brightness");
        brightnessManager.setBrightness(Configs::Get().Get<double>("Brightness.Default", 1.0));
        info("Brightness set to: " + std::to_string(Configs::Get().Get<double>("Brightness.Default", 1.0)));
    });

    io.Hotkey("f7", [this]() {
        info("Decreasing brightness");
        brightnessManager.decreaseBrightness(0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness()));
    });

    io.Hotkey("^f7", [this]() {
        info("Decreasing brightness");
        brightnessManager.decreaseBrightness(brightnessManager.getMonitor(1),0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness(brightnessManager.getMonitor(1))));
    });

    io.Hotkey("f8", [this]() {
        info("Increasing brightness");
        brightnessManager.increaseBrightness(0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness()));
    });
    io.Hotkey("^f8", [this]() {
        info("Increasing brightness");
        brightnessManager.increaseBrightness(brightnessManager.getMonitor(1),0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness(brightnessManager.getMonitor(1))));
    });

    io.Hotkey("+f7", [this]() {
        info("Decreasing gamma");
        brightnessManager.decreaseGamma(500);
        info("Current gamma: " + std::to_string(brightnessManager.getTemperature()));
    });
    io.Hotkey("^+f7", [this]() {
        info("Decreasing gamma");
         brightnessManager.decreaseGamma(brightnessManager.getMonitor(1),500);
        info("Current gamma: " + std::to_string(brightnessManager.getTemperature(brightnessManager.getMonitor(1))));
    });

    io.Hotkey("+f8", [this]() {
        info("Increasing gamma");
        brightnessManager.increaseGamma(500);
        info("Current gamma: " + std::to_string(brightnessManager.getTemperature()));
    });
    io.Hotkey("^+f8", [this]() {
        info("Increasing gamma");
        brightnessManager.increaseGamma(brightnessManager.getMonitor(1),500);
        info("Current gamma: " + std::to_string(brightnessManager.getTemperature(brightnessManager.getMonitor(1))));
    });

    // Mouse wheel + click combinations
    io.Hotkey("!Button5", [this]() {
        std::cout << "alt+Button5" << std::endl;
        Zoom(1, io);
    });
    AddHotkey("!d", [this]() {
        showBlackOverlay();
        logWindowEvent("BLACK_OVERLAY", "Showing black overlay");
    });

    //Emergency exit
    AddHotkey("@^!+#Esc", [this]() {
        info("Emergency exit");
        io.EmergencyReleaseAllKeys();
        exit(0);
    });
    AddHotkey("@+#Esc", [this]() {
        info("Emergency release all keys");
        io.EmergencyReleaseAllKeys();
    });
    
    // Window moving
    // Fixed lambda syntax - should be a proper function or lambda variable
    auto WinMove = [](int x, int y, int w, int h) {
        havel::Window win(WindowManager::GetActiveWindow());
        Rect pos = win.Pos();
        WindowManager::MoveResize(win.ID(), pos.x + x, pos.y + y, pos.w + w, pos.h + h);
    };
    
    // Window movement hotkeys
    // Meta+Numpad: Move window (5=down, 6=up, 7=left, 8=right)
    AddHotkey("@!numpad5", [this, WinMove]() {
        WinMove(0, winOffset, 0, 0); // Move down
    });
    
    AddHotkey("@!numpad6", [this, WinMove]() {
        WinMove(0, -winOffset, 0, 0); // Move up
    });
    
    AddHotkey("@!numpad7", [this, WinMove]() {
        WinMove(-winOffset, 0, 0, 0); // Move left
    });
    
    AddHotkey("@!numpad8", [this, WinMove]() {
        WinMove(winOffset, 0, 0, 0); // Move right
    });
    
    // Meta+Shift+Numpad: Resize window (width/height adjustment)
    AddHotkey("@!+numpad5", [this, WinMove]() {
        WinMove(0, 0, 0, winOffset); // Increase height
    });
    
    AddHotkey("@!+numpad6", [this, WinMove]() {
        WinMove(0, 0, 0, -winOffset); // Decrease height
    });
    
    AddHotkey("@!+numpad7", [this, WinMove]() {
        WinMove(0, 0, -winOffset, 0); // Decrease width
    });
    
    AddHotkey("@!+numpad8", [this, WinMove]() {
        WinMove(0, 0, winOffset, 0); // Increase width
    });
    
    // Mouse emulation
    // Numpad mouse movement with acceleration
    AddHotkey("numpad1", [this]() { // Bottom-left diagonal
        static int currentSpeed = speed;
        io.MouseMove(-currentSpeed, currentSpeed, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad2", [this]() { // Down
        static int currentSpeed = speed;
        io.MouseMove(0, currentSpeed, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad3", [this]() { // Bottom-right diagonal
        static int currentSpeed = speed;
        io.MouseMove(currentSpeed, currentSpeed, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad4", [this]() { // Left
        static int currentSpeed = speed;
        io.MouseMove(-currentSpeed, 0, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad6", [this]() { // Right
        static int currentSpeed = speed;
        io.MouseMove(currentSpeed, 0, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad7", [this]() { // Top-left diagonal
        static int currentSpeed = speed;
        io.MouseMove(-currentSpeed, -currentSpeed, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad8", [this]() { // Up
        static int currentSpeed = speed;
        io.MouseMove(0, -currentSpeed, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    AddHotkey("numpad9", [this]() { // Top-right diagonal
        static int currentSpeed = speed;
        io.MouseMove(currentSpeed, -currentSpeed, 1, acc);
        currentSpeed += static_cast<int>(acc);
    });
    
    // Mouse clicking
    AddHotkey("numpad5", [this]() { // Left click hold
        io.Click(MouseButton::Left, MouseAction::Hold);
    });
    
    AddHotkey("numpad5:up", [this]() { // Left click release
        io.Click(MouseButton::Left, MouseAction::Release);
    });
    
    AddHotkey("numpadmult", [this]() { // Right click (*)
        io.Click(MouseButton::Right, MouseAction::Hold);
    });
    
    AddHotkey("numpadmult:up", [this]() { // Right click release
        io.Click(MouseButton::Right, MouseAction::Release);
    });
    
    AddHotkey("numpaddiv", [this]() { // Middle click (/)
        io.Click(MouseButton::Middle, MouseAction::Hold);
    });

    AddHotkey("numpaddiv:up", [this]() { // Middle click release
        io.Click(MouseButton::Middle, MouseAction::Release);
    });
    
    // Mouse scrolling
    AddHotkey("numpad0", [this]() { // Scroll down
        io.Scroll(-1, 0);
    });
    
    AddHotkey("numpaddec", [this]() { // Scroll up (numpad decimal/dot)
        io.Scroll(1, 0);
    });
    
    // Reset speed when any key is released
    AddHotkey("numpad1:up", []() { /* Reset speed for numpad1 */ });
    AddHotkey("numpad2:up", []() { /* Reset speed for numpad2 */ });
    AddHotkey("numpad3:up", []() { /* Reset speed for numpad3 */ });
    AddHotkey("numpad4:up", []() { /* Reset speed for numpad4 */ });
    AddHotkey("numpad6:up", []() { /* Reset speed for numpad6 */ });
    AddHotkey("numpad7:up", []() { /* Reset speed for numpad7 */ });
    AddHotkey("numpad8:up", []() { /* Reset speed for numpad8 */ });
    AddHotkey("numpad9:up", []() { /* Reset speed for numpad9 */ });
    // Also add a title-based hotkey for Koikatu window title (as a fallback)
    AddContextualHotkey("~d", "Window.Active('name:Koikatu')",
        [this]() {
            // Show black overlay when D is pressed in Koikatu window
            info("Koikatu window title detected - D key pressed - showing black overlay");
            showBlackOverlay();
            logWindowEvent("KOIKATU_BLACK_OVERLAY", "Showing black overlay from Koikatu window (title match)");
        },
        nullptr, // No action when condition is false
        0 // ID parameter
    );

    // '/' key to hold 'w' down in gaming mode
    AddContextualHotkey("@y", "currentMode == 'gaming'",
        [this]() {
            info("Gaming hotkey: Holding 'w' key down");
            io.Send("{w down}");

            // Register the same key to release when pressed again
            static bool keyDown = false;
            keyDown = !keyDown;

            if (keyDown) {
                info("W key pressed and held down");
            } else {
                io.Send("{w up}");
                info("W key released");
            }
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // "'" key to move mouse to coordinates and autoclick in gaming mode
    AddContextualHotkey("'", "currentMode == 'gaming'",
        [this]() {
            info("Gaming hotkey: Moving mouse to 1600,700 and autoclicking");
            io.MouseMove(1600, 700, 10, 1.0f); // Using default speed (10) and acceleration (1.0f)
            // Start autoclicking
            startAutoclicker("Button1");
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // autoclick in gaming mode
    AddHotkey("#Enter", 
        [this]() {
            info("Starting autoclicker with Enter key");
            startAutoclicker("Button1");
        }
    );

    // Add a variable to track and allow stopping Genshin automation
    static std::atomic<bool> genshinAutomationActive(false);
    static AutoRunner genshinAutoRunner(io);
    AddContextualHotkey("/", "currentMode == 'gaming'",
        []() {
            info("Genshin Impact detected - Starting specialized auto actions");
            genshinAutoRunner.toggle();
        },
        nullptr, // No action when not in gaming mode
        0 // ID parameter
    );

    // Special hotkeys for Genshin Impact - Start automation
    AddContextualHotkey("enter", "currentMode == 'gaming'", [this]() {
    if (genshinAutomationActive) {
        warning("Genshin automation is already active");
        return;
    }

    info("Genshin Impact detected - Starting specialized auto actions");
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
                    error("Genshin automation: Could not get window title");
                    break;
                }
            }

            if (!isGenshinActive) {
                info("Genshin automation: Window no longer active");
                break;
            }

            // Press E
            io.Send("e");
            debug("Genshin automation: Pressed E (" + std::to_string(counter + 1) + "/" + std::to_string(maxIterations) + ")");

            // Every 5th loop, press Q
            if (counter % 5 == 0) {
                io.Send("q");
                debug("Genshin automation: Pressed Q");
            }

            counter++;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        genshinAutomationActive = false;
        info("Genshin automation: Automation ended");

    }).detach();

}, nullptr, 0);
    //Cutscene skipper
    AddContextualHotkey("+s", "currentMode == 'gaming'", [this]() {
        info("Genshin Impact detected - Skipping cutscene");
        io.SetTimer(100, [this]() {
            Rect pos = {1600, 700};
            float speed = 100.0f;
            float accel = 5.0f;
            io.MouseClick(MouseButton::Left, pos.x, pos.y, speed, accel);
            io.Send("{enter}");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            io.Send("f");
        });
    }, nullptr, 0);
    // Add hotkey to stop Genshin automation
    AddHotkey("!+g", [this]() {
        if (genshinAutomationActive) {
            genshinAutomationActive = false;
            info("Manually stopping Genshin automation");
            showNotification("Genshin Automation", "Automation sequence stopped");
        } else {
            info("Genshin automation is not active");
            showNotification("Genshin Automation", "No active automation to stop");
        }
    });
}
void HotkeyManager::PlayPause() {
    if (mpv.IsSocketAlive()) {
        mpv.SendCommand({"cycle", "pause"});
    } else {
        // Send playerctl
        Launcher::runShell("playerctl play-pause");
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
    { "+c", "currentMode == 'gaming'", [this]() { mpv.SendCommand({"script-binding", "copy_current_subtitle"}); }, nullptr, mpvBaseId++ },
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
        info("Starting in normal mode - unregistering MPV hotkeys");
        ungrabGamingHotkeys();
    }
}

void HotkeyManager::RegisterWindowHotkeys() {
    // Window movement
    io.Hotkey("^!Up", []() {
        WindowManager::MoveToCorners(1);
    });

    io.Hotkey("^!Down", []() {
        WindowManager::MoveToCorners(2);
    });

    io.Hotkey("^!Left", []() {
        WindowManager::MoveToCorners(3);
    });

    io.Hotkey("^!Right", []() {
        WindowManager::MoveToCorners(4);
    });

    // Window resizing
    io.Hotkey("+!Up", []() {
        WindowManager::ResizeToCorner(1);
    });

    io.Hotkey("!+Down", []() {
        WindowManager::ResizeToCorner(2);
    });

    io.Hotkey("!+Left", []() {
        WindowManager::ResizeToCorner(3);
    });

    io.Hotkey("!+Right", []() {
        WindowManager::ResizeToCorner(4);
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
            info("Koikatu window detected - D key pressed - showing black overlay");
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
            info("Koikatu window title detected - D key pressed - showing black overlay");
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
        Launcher::runShell("xdg-screensaver lock");
    });

    io.Hotkey("+!Esc", []() {
        // Show system monitor
        Launcher::runShell("gnome-system-monitor &");
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
        info("Verbose condition logging " + status);
        showNotification("Debug Setting", "Condition logging " + status);
    });
}

bool HotkeyManager::AddHotkey(const std::string& hotkeyStr, std::function<void()> callback) {
    return io.Hotkey(hotkeyStr, [this, hotkeyStr, callback]() {
        logWindowEvent("ACTIVE", "Key pressed: " + hotkeyStr);
        callback();
    });
}

bool HotkeyManager::AddHotkey(const std::string& hotkeyStr, const std::string& action) {
    return io.Hotkey(hotkeyStr, [action]() {
        Launcher::runShell(action.c_str());
    });
}

bool HotkeyManager::RemoveHotkey(const std::string& hotkeyStr) {
    // Remove a hotkey
    info("Removing hotkey: " + hotkeyStr);
    return true;
}

void HotkeyManager::LoadHotkeyConfigurations() {
    // Load hotkeys from configuration file
    // This would typically read from a JSON or similar config file

    // Example of loading a hotkey from config (placeholder implementation)
    info("Loading hotkey configurations...");

    // In a real implementation, we would iterate through config entries
    // and register each hotkey with its action
}

void HotkeyManager::ReloadConfigurations() {
    // Reload hotkey configurations
    info("Reloading configurations");
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
            debug("Evaluating condition: " + condition);

        bool conditionMet = evaluateCondition(condition);

        if (conditionMet) {
            if (trueAction) {
                if (verboseKeyLogging)
                    debug("Condition met, executing true action");
                trueAction();
            }
        } else {
            if (falseAction) {
                if (verboseKeyLogging)
                    debug("Condition not met, executing false action");
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
                    error("Failed to get active window title");
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
                    error("Failed to get active window title");
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
                    info("Condition met: " + condition + " - Grabbing MPV hotkeys");
                    grabGamingHotkeys();
                }

                // Log the state change
        if (verboseWindowLogging) {
                    logWindowEvent("CONDITION_STATE", "Condition now TRUE: " + condition);
                }
            } else {
                // Condition is now not met, ungrab the keys
                if (mpvHotkeysGrabbed && (condition.find("currentMode == 'gaming'") != std::string::npos)) {
                    info("Condition no longer met: " + condition + " - Ungrabbing MPV hotkeys");
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
                info(WindowManager::activeWindow.className);
                info(WindowManager::activeWindow.title);
                info("Auto-switched to gaming mode (detected)");
            }
            result = true;
        }

        if (!result && currentMode == "gaming") {
            std::string old = currentMode;
            currentMode = "default";
            logModeSwitch(old, currentMode);
            if (verboseWindowLogging)
                logWindowEvent("AUTO_MODE_CHANGE", "Switched to normal mode");
            info("Auto-switched to normal mode");
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
        warning("Unrecognized condition: " + actual);
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
        info("Grabbing hotkey: " + std::to_string(id));
        io.GrabHotkey(id);
    }

    mpvHotkeysGrabbed = true;
    info("Grabbed all MPV hotkeys for gaming mode");
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
    info("Released all MPV hotkeys for normal mode");
}

void HotkeyManager::showNotification(const std::string& title, const std::string& message) {
    std::string cmd = "notify-send \"" + title + "\" \"" + message + "\"";
    Launcher::runShell(cmd.c_str());
}

bool HotkeyManager::isGamingWindow() {
    std::string windowClass = WindowManager::activeWindow.className;

    // List of window classes for gaming applications
    const std::vector<std::string> gamingApps = Configs::Get().GetGamingApps();
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
    // If already running, stop it (toggle behavior)
    if (autoclickerActive) {
        info("Stopping autoclicker - toggled off");
        autoclickerActive = false;
        if (autoclickerThread.joinable()) {
            autoclickerThread.join();
        }
        return;
    }

    // If a thread is somehow still joinable here, clean it up before starting a new one.
    if (autoclickerThread.joinable()) {
        autoclickerThread.join();
    }

    // Not a gaming window? Abort!
    if (!isGamingWindow()) {
        debug("Autoclicker not activated - not in gaming window");
        return;
    }

    // Get the active window after we've confirmed we're in a gaming window
    wID currentWindow = WindowManager::GetActiveWindow();
    if (!currentWindow) {
        error("Failed to get active window for autoclicker");
        return;
    }

    // Save the window we're starting in
    autoclickerWindowID = currentWindow;
    autoclickerActive = true;

    try {
        info("Starting autoclicker (" + button + ") in window: " + std::to_string(autoclickerWindowID));
    } catch (const std::exception& e) {
        error("Failed to log autoclicker start: " + std::string(e.what()));
        return;
    }

    // Configuration for autoclicker timing (in milliseconds)
    struct AutoClickerConfig {
        int clickInterval = 50;  // Time between click sequences
        int clickHold = 20;      // Time to hold button down
    };
    
    static AutoClickerConfig config;  // Static to persist between calls
    
    // Helper function to handle mouse button clicks
    auto clickButton = [this, &button](MouseAction action) {
        if (button == "Button1" || button == "Left") {
            io.Click(MouseButton::Left, action);
        } else if (button == "Button2" || button == "Right") {
            io.Click(MouseButton::Right, action);
        } else if (button == "Button3" || button == "Middle") {
            io.Click(MouseButton::Middle, action);
        } else if (button == "Side1") {
            io.Click(MouseButton::Side1, action);
        } else if (button == "Side2") {
            io.Click(MouseButton::Side2, action);
        } else {
            error("Invalid mouse button: " + button);
        }
    };



    // ONE thread that handles everything
    autoclickerThread = std::thread([this, button, clickButton]() {
        while (autoclickerActive) {
            // Check if window changed
            wID activeWindow = WindowManager::GetActiveWindow();
            if (activeWindow != autoclickerWindowID) {
                info("Stopping autoclicker - window changed");
                autoclickerActive = false;
                break;
            }

            // Click sequence
            clickButton(MouseAction::Hold);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.clickHold));
            clickButton(MouseAction::Release);
            
            // Sleep for the rest of the interval
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config.clickInterval - config.clickHold));
        }

        info("Autoclicker thread terminated");
    });
}

    // Call this when you need to force-stop (e.g., on app exit)
    void HotkeyManager::stopAllAutoclickers() {
    if (autoclickerActive) {
        info("Force stopping all autoclickers");
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
            error("Failed to open X display for keycode conversion");
            return input;
        }

        KeySym keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
        char* keyName = XKeysymToString(keysym);
        XCloseDisplay(display);

        if (keyName) {
            return keyName;
        }
    } catch (const std::exception& e) {
        error("Failed to convert keycode: " + input + " - " + e.what());
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
        error("Failed to convert scancode: " + input + " - " + e.what());
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
    info(timestamp + " " + type + " " + details);
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

    info(timestamp + " " + type + " " + keyInfo + detailInfo);
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

    info(timestamp + " " + type + " " + windowInfo + detailInfo);
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
            Launcher::runShell("playerctl play-pause");
        }
        else if (mpvCommand[0] == "seek") {
            if (mpvCommand[1] == "-3") {
                Launcher::runShell("playerctl position 3-");
            } else if (mpvCommand[1] == "3") {
                Launcher::runShell("playerctl position 3+");
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
    info("Showing black overlay window on all monitors");

    Display* display = DisplayManager::GetDisplay();
    if (!display) {
        error("Failed to get display for black overlay");
        return;
    }

    // Get the root window - use X11's Window type here, not our Window class
    ::Window rootWindow = DefaultRootWindow(display);

    // Get screen dimensions (this will get the primary monitor dimensions)
    Screen* screen = DefaultScreenOfDisplay(display);
    int screenWidth = WidthOfScreen(screen);
    int screenHeight = HeightOfScreen(screen);

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
    attrs.override_redirect = x11::XTrue;  // Bypass window manager
    attrs.background_pixel = BlackPixel(display, DefaultScreen(display));
    attrs.border_pixel = BlackPixel(display, DefaultScreen(display));
    attrs.event_mask = ButtonPressMask | x11::XKeyPressMask;  // Capture events to close it

    // Create the black window - save as X11's Window type, not our Window class
    ::Window blackWindow = XCreateWindow(display,
                                      rootWindow,
                                      0, 0,                            // x, y
                                      screenWidth, screenHeight,       // width, height
                                      0,                               // border width
                                      CopyFromParent,                  // depth
                                      x11::XInputOutput,                     // class
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
                info("Black overlay auto-closed after timeout");
                running = false;
                break;
            }

            // Check for events
            while (XPending(display) > 0) {
                XNextEvent(display, &event);

                // Close on any key press or mouse click
                if (event.type == x11::XKeyPress || event.type == x11::XButtonPress) {
                    running = false;
                    info("Black overlay closed by user input");
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
        info("╔══════════════════════════════════════╗");
        info("║      NO ACTIVE WINDOW DETECTED       ║");
        info("╚══════════════════════════════════════╝");
        return;
    }

    // Create window instance
    havel::Window window("ActiveWindow", activeWindow);

    // Get window class
    std::string windowClass = WindowManager::GetActiveWindowClass();

    // Get window title
    std::string windowTitle;
    try {
        windowTitle = window.Title();
    } catch (...) {
        windowTitle = "Unknown";
        error("Failed to get active window title");
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
        error("Failed to get window position");
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

    info("╔══════════════════════════════════════════════════════════╗");
    info("║             ACTIVE WINDOW INFORMATION                    ║");
    info("╠══════════════════════════════════════════════════════════╣");
    info(formatLine("Window ID: ", std::to_string(activeWindow)));
    info(formatLine("Window Title: \"", windowTitle + "\""));
    info(formatLine("Window Class: \"", windowClass + "\""));
    info(formatLine("Window Geometry: ", geometry));

    std::string gamingStatus = isGaming ? (COLOR_GREEN + std::string("YES ✓") + COLOR_RESET) : (COLOR_RED + std::string("NO ✗") + COLOR_RESET);
    info(formatLine("Is Gaming Window: ", gamingStatus));

    info(formatLine("Current Mode: ", currentMode));
    info("╚══════════════════════════════════════════════════════════╝");

    // Log to window event history
    logWindowEvent("WINDOW_INFO",
        "Title: \"" + windowTitle + "\", Class: \"" + windowClass +
        "\", Gaming: " + (isGaming ? "YES" : "NO") + ", Geometry: " + geometry);
}

void HotkeyManager::cleanup() {
    // Stop all autoclickers if any are running
    stopAllAutoclickers();
    
    // Release any grabbed gaming hotkeys
    ungrabGamingHotkeys();
    
    // Reset mode to default to release any special key states
    setMode("default");
    
    // Ensure all keys are released
    io.Send("{LAlt up}{RAlt up}{LShift up}{RShift up}{LCtrl up}{RCtrl up}{LWin up}{RWin up}");
    
    if (verboseWindowLogging) {
        logWindowEvent("CLEANUP", "HotkeyManager resources cleaned up");
    }
}

void HotkeyManager::toggleWindowFocusTracking() {
    trackWindowFocus = !trackWindowFocus;

    if (trackWindowFocus) {
        info("Window focus tracking ENABLED - will log all window changes");
        logWindowEvent("FOCUS_TRACKING", "Enabled");

        // Initialize with current window
        lastActiveWindowId = WindowManager::GetActiveWindow();
        if (lastActiveWindowId != 0) {
            printActiveWindowInfo();
        }
    } else {
        info("Window focus tracking DISABLED");
        logWindowEvent("FOCUS_TRACKING", "Disabled");
    }
}

// Implement the debug settings methods
void HotkeyManager::loadDebugSettings() {
    info("Loading debug settings from config");

    // Get settings from config file with default values if not present
    bool keyLogging = Configs::Get().Get<bool>("Debug.VerboseKeyLogging", false);
    bool windowLogging = Configs::Get().Get<bool>("Debug.VerboseWindowLogging", false);
    bool conditionLogging = Configs::Get().Get<bool>("Debug.VerboseConditionLogging", false);

    // Apply the settings
    setVerboseKeyLogging(keyLogging);
    setVerboseWindowLogging(windowLogging);
    setVerboseConditionLogging(conditionLogging);

    // Log the current debug settings
    info("Debug settings: KeyLogging=" + std::to_string(verboseKeyLogging) +
            ", WindowLogging=" + std::to_string(verboseWindowLogging) +
            ", ConditionLogging=" + std::to_string(verboseConditionLogging));
}

void HotkeyManager::applyDebugSettings() {
    // Apply current debug settings
    if (verboseKeyLogging) {
        info("Verbose key logging is enabled");
    }

    if (verboseWindowLogging) {
        info("Verbose window logging is enabled");
    }

    if (verboseConditionLogging) {
        info("Verbose condition logging is enabled");
    }

    // Set up config watchers to react to changes in real-time
    Configs::Get().Watch<bool>("Debug.VerboseKeyLogging", [this](bool oldValue, bool newValue) {
        info("Key logging setting changed from " + std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseKeyLogging(newValue);
    });

    Configs::Get().Watch<bool>("Debug.VerboseWindowLogging", [this](bool oldValue, bool newValue) {
        info("Window logging setting changed from " + std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseWindowLogging(newValue);
    });

    Configs::Get().Watch<bool>("Debug.VerboseConditionLogging", [this](bool oldValue, bool newValue) {
        info("Condition logging setting changed from " + std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseConditionLogging(newValue);
    });
}
} // namespace havel