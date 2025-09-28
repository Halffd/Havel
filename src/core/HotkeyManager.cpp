#include "HotkeyManager.hpp"
#include "../utils/Logger.hpp"
#include "IO.hpp"
#include "core/BrightnessManager.hpp"
#include "core/ConditionSystem.hpp"
#include "automation/AutoRunner.hpp"
#include "utils/Chain.hpp"
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
#include "utils/Util.hpp"
// Include XRandR for multi-monitor support
#ifdef __linux__
#include <X11/extensions/Xrandr.h>
#endif
namespace havel {
std::string HotkeyManager::currentMode = "default";
std::mutex HotkeyManager::modeMutex;

std::string HotkeyManager::getMode() const {
    std::lock_guard<std::mutex> lock(modeMutex);
    return currentMode;
}

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
    info("-------------------------------------");
    //red color
    std::cout << "\033[31m";
    info("==== FAILED HOTKEYS ====");
    int i = 0;
    // Collect failed aliases into a set
std::set<std::string> failedAliases;
std::ostringstream failedStream;

for (const auto& failed : io.failedHotkeys) {
    failedAliases.insert(failed.alias);
    std::cout << failed.alias;
    if (failedStream.tellp() > 0) failedStream << " ";
    failedStream << failed.alias;
}
std::cout << "\n";

std::string joined = failedStream.str();
info("Failed hotkeys: " + joined);

// Filter working hotkeys
std::ostringstream workingStream;
for (const auto& [key, hotkey] : io.hotkeys) {
    if (failedAliases.find(hotkey.alias) == failedAliases.end()) {
        if (workingStream.tellp() > 0) workingStream << " ";
        workingStream << hotkey.alias;
    }
}
// Reset color
std::cout << "\033[0m";

std::string workingHotkeys = workingStream.str();
info("Working hotkeys: " + workingHotkeys);

    info("=== End Hotkey Report ===");
}
HotkeyManager::HotkeyManager(IO &io, WindowManager &windowManager, MPVController &mpv, AudioManager &audioManager, ScriptEngine &scriptEngine)
    : io(io), windowManager(windowManager), mpv(mpv), audioManager(audioManager), scriptEngine(scriptEngine) {
    config = Configs::Get();
    mouseController = std::make_unique<MouseController>(io);
    conditionEngine = std::make_unique<ConditionEngine>();
    setupConditionEngine();
    loadVideoSites();
    loadDebugSettings();
    applyDebugSettings();
    
    // Initialize auto clicker with IO reference
    autoClicker = std::make_unique<havel::automation::AutoClicker>(std::shared_ptr<IO>(&io, [](IO*){}));
    // Initialize automation manager
    automationManager_ = std::make_shared<havel::automation::AutomationManager>(std::shared_ptr<IO>(&io, [](IO*){}));   
    currentMode = "default";
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
    // Mode toggles
    io.Hotkey("^!g", [this]() {
        std::string oldMode = currentMode;
        currentMode = (currentMode == "gaming") ? "default" : "gaming";
        setMode(currentMode);
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

    io.Hotkey("@numpadAdd", [this]() {
        audioManager.increaseVolume(3);
    });

    io.Hotkey("@numpadSub", [this]() {
        audioManager.decreaseVolume(3);
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

    // If not gaming mode: open whisker menu on keyup
    AddContextualHotkey("@lwin:up", "mode != 'gaming'", 
        []() {
            Launcher::runAsync("/bin/xfce4-popup-whiskermenu");
        });
    AddGamingHotkey("@lwin", 
        [this]() {
            PlayPause();
        });

    AddGamingHotkey("u", 
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

    io.Hotkey("@+numpad7", [this]() {
        info("Zoom 1");
        Zoom(1, io);
    });

    io.Hotkey("@+numpad1", [this]() {
        info("Zoom 0");
        Zoom(0, io);
    });

    io.Hotkey("@+numpad5", [this]() {
        info("Zoom 2");
        Zoom(2, io);
    });

    AddHotkey("^f1", "~/scripts/f1.sh -1");
    AddHotkey("+f1", "~/scripts/f1.sh 0");

    AddHotkey("+!l", "~/scripts/livelink.sh");
    AddHotkey("^!l", "livelink screen toggle 1");
    AddHotkey("!f10", "~/scripts/str");
    AddHotkey("^!k", "livelink screen toggle 2");
    AddHotkey("^f10", "~/scripts/mpvv");

    // Context-sensitive hotkeys
    AddContextualHotkey(" @nosymbol", "IsZooming",
        [this]() { // When zooming
            std::cout << "kc0 t" << std::endl;
            Zoom(2, io);
        },
        [this]() { // When not zooming
            Zoom(3, io);
        },
        0
    );

    AddContextualHotkey("!x", "!(window.class ~ 'emacs')",
        []() {
            Launcher::runAsync("/bin/alacritty");
        },
        nullptr, 
        0
    );

    // Window Management
    io.Hotkey("#left", []() {
        debug("Moving window left");
        WindowManager::MoveToCorners(3);
    });

    io.Hotkey("#right", []() {
        debug("Moving window right");
        WindowManager::MoveToCorners(4);
    });
    io.Hotkey("$f9", [this]() {
        info("Suspending all hotkeys");
        io.Suspend(); 
        debug("Hotkeys suspended");
    });
    
    auto switchToLastWindow = []() {
        debug("Switching to last window");
        WindowManager::AltTab();
    };

    io.Hotkey("^!t", switchToLastWindow);

    const std::map<std::string, std::pair<std::string, std::function<void()>>> emergencyHotkeys = {
        {"#Esc", {"Restart application", []() {
            info("Restarting application");
            std::string exePath = havel::GetExecutablePath();
            if (!exePath.empty()) {
                debug("Executable path: " + exePath);
                if(fork() == 0) {
                    debug("Child process started, executing: " + exePath);
                    execl(exePath.c_str(), exePath.c_str(), nullptr);
                    error("Failed to restart application");
                    exit(1); 
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

    for (const auto& [key, hotkeyInfo] : emergencyHotkeys) {
        const auto& [description, action] = hotkeyInfo;
        io.Hotkey(key, [description, action]() {
            info("Executing emergency hotkey: " + description);
            action();
        });
    }

    // Brightness and temperature control
    io.Hotkey("f3", [this]() {
        info("Setting defaults");
        brightnessManager.setBrightness(Configs::Get().Get<double>("Brightness.Default", 1.0));
        brightnessManager.setShadowLift(Configs::Get().Get<double>("ShadowLift.Default", 0.0));
        brightnessManager.setGammaRGB(Configs::Get().Get<double>("Gamma.Default", 1.0),Configs::Get().Get<double>("Gamma.Default", 1.0),Configs::Get().Get<double>("Gamma.Default", 1.0));
        brightnessManager.setTemperature(Configs::Get().Get<double>("Temperature.Default", 6500));
        info("Brightness set to: " + std::to_string(Configs::Get().Get<double>("Brightness.Default", 1.0)));
        info("Temperature set to: " + std::to_string(Configs::Get().Get<double>("Temperature.Default", 6500)));
    });
    io.Hotkey("+f3", [this]() {
        info("Setting default temperature");
        brightnessManager.setTemperature(Configs::Get().Get<double>("Temperature.Default", 6500));
        info("Temperature set to: " + std::to_string(Configs::Get().Get<double>("Temperature.Default", 6500)));
    });
    io.Hotkey("^f3", [this]() {
        info("Setting default brightness");
        brightnessManager.setBrightness(1,Configs::Get().Get<double>("Brightness.Default", 1.0));
        info("Brightness set to: " + std::to_string(Configs::Get().Get<double>("Brightness.Default", 1.0)));
    });

    io.Hotkey("f7", [this]() {
        info("Decreasing brightness");
        brightnessManager.decreaseBrightness(0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness()));
    });

    io.Hotkey("^f7", [this]() {
        info("Decreasing brightness");
        brightnessManager.decreaseBrightness(0,0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness(0)));
    });

    io.Hotkey("f8", [this]() {
        info("Increasing brightness");
        brightnessManager.increaseBrightness(0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness()));
    });
    io.Hotkey("^f8", [this]() {
        info("Increasing brightness");
        brightnessManager.increaseBrightness(0,0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness(0)));
    });

    io.Hotkey("^!f8", [this]() {
        info("Increasing brightness");
        brightnessManager.increaseBrightness(1,0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness(1)));
    });
    io.Hotkey("^!f7", [this]() {
        info("Decreasing brightness");
        brightnessManager.decreaseBrightness(1,0.05);
        info("Current brightness: " + std::to_string(brightnessManager.getBrightness(1)));
    });
    io.Hotkey("@!f7", [this]() {
        info("Decreasing shadow lift");
        auto shadowLift = brightnessManager.getShadowLift();
        brightnessManager.setShadowLift(shadowLift - 0.05);
        info("Current shadow lift: " + std::to_string(brightnessManager.getShadowLift()));
    });
    io.Hotkey("@!f8", [this]() {
        info("Increasing shadow lift");
        auto shadowLift = brightnessManager.getShadowLift();
        brightnessManager.setShadowLift(shadowLift + 0.05);
        info("Current shadow lift: " + std::to_string(brightnessManager.getShadowLift()));
    });
    io.Hotkey("@^f9", [this]() {
        info("Decreasing shadow lift");
        auto shadowLift = brightnessManager.getShadowLift();
        brightnessManager.setShadowLift(0,shadowLift - 0.05);
        info("Current shadow lift: " + std::to_string(brightnessManager.getShadowLift(0)));
    });
    io.Hotkey("@^f10", [this]() {
        info("Increasing shadow lift");
        auto shadowLift = brightnessManager.getShadowLift(0);
        brightnessManager.setShadowLift(0,shadowLift + 0.05);
        info("Current shadow lift: " + std::to_string(brightnessManager.getShadowLift(0)));
    });
    io.Hotkey("@^+f9", [this]() {
        info("Decreasing shadow lift");
        auto shadowLift = brightnessManager.getShadowLift(1);
        brightnessManager.setShadowLift(1,shadowLift - 0.05);
        info("Current shadow lift: " + std::to_string(brightnessManager.getShadowLift(1)));
    });
    io.Hotkey("@^+f10", [this]() {
        info("Increasing shadow lift");
        auto shadowLift = brightnessManager.getShadowLift(1);
        brightnessManager.setShadowLift(1,shadowLift + 0.05);
        info("Current shadow lift: " + std::to_string(brightnessManager.getShadowLift(1)));
    });
    io.Hotkey("@#f7", [this]() {
        info("Decreasing gamma");
        brightnessManager.decreaseGamma(200);
    });
    io.Hotkey("@#f8", [this]() {
        info("Increasing gamma");
        brightnessManager.increaseGamma(200);
    });
    io.Hotkey("@+f9", [this]() {
        info("Decreasing gamma");
        brightnessManager.decreaseGamma(0,200);
    });
    io.Hotkey("@+f10", [this]() {
        info("Increasing gamma");
        brightnessManager.increaseGamma(0,200);
    });
    io.Hotkey("@!+f10", [this]() {
        info("Increasing gamma");
        brightnessManager.increaseGamma(1,200);
    });
    io.Hotkey("!+f9", [this]() {
        info("Decreasing gamma");
        brightnessManager.decreaseGamma(1,200);
    });
    io.Hotkey("+f7", [this]() {
        info("Decreasing temperature");
        brightnessManager.decreaseTemperature(500);
        info("Current temperature: " + std::to_string(brightnessManager.getTemperature()));
    });
    io.Hotkey("^+f7", [this]() {
        info("Decreasing temperature");
        brightnessManager.decreaseTemperature(0,500);
        info("Current temperature: " + std::to_string(brightnessManager.getTemperature(0)));
    });
    io.Hotkey("^!+f7", [this]() {
        info("Decreasing temperature");
        brightnessManager.decreaseTemperature(1,500);
        info("Current temperature: " + std::to_string(brightnessManager.getTemperature(1)));
    });

    io.Hotkey("+f8", [this]() {
        info("Increasing temperature");
        brightnessManager.increaseTemperature(500);
        info("Current temperature: " + std::to_string(brightnessManager.getTemperature()));
    });
    io.Hotkey("^+f8", [this]() {
        info("Increasing temperature");
        brightnessManager.increaseTemperature(0,500);
        info("Current temperature: " + std::to_string(brightnessManager.getTemperature(0)));
    });
    io.Hotkey("^!+f8", [this]() {
        info("Increasing temperature");
        brightnessManager.increaseTemperature(1,500);
        info("Current temperature: " + std::to_string(brightnessManager.getTemperature(1)));
    });
    float* gamma = new float(1.0f);
    io.Hotkey("@!q", [this, gamma]() {
        info("Decreasing gamma");
        *gamma -= 0.05f;
        Launcher::runShell(std::string("~/scripts/gamma-toggle.sh " + std::to_string(*gamma) + " " + std::to_string(brightnessManager.getBrightness())));
    });
    io.Hotkey("@!w", [this, gamma]() {
        info("Increasing gamma");
        *gamma += 0.05f;
        Launcher::runShell(std::string("~/scripts/gamma-toggle.sh " + std::to_string(*gamma) + " " + std::to_string(brightnessManager.getBrightness())));
    });
    io.Hotkey("@!g", [this, gamma]() {
        info("Reset gamma");
        if(*gamma != 1.0f){
            *gamma = 1.0f;
        } else {
            *gamma = 1.42f;
        }
        Launcher::runShell(std::string("~/scripts/gamma-toggle.sh " + std::to_string(*gamma) + " " + std::to_string(brightnessManager.getBrightness())));
    });

    // Mouse wheel + click combinations
    io.Hotkey("!Button5", [this]() {
        std::cout << "alt+Button5" << std::endl;
        Zoom(1, io);
    });

    //Emergency exit
    AddHotkey("@^!+#Esc", [this]() {
        info("Emergency exit");
        io.EmergencyReleaseAllKeys();
        App::quit();
    });
    AddHotkey("@+#Esc", [this]() {
        info("Emergency release all keys");
        io.EmergencyReleaseAllKeys();
    });
    
    auto WinMove = [](int x, int y, int w, int h) {
        havel::Window win(WindowManager::GetActiveWindow());
        Rect pos = win.Pos();
        WindowManager::MoveResize(win.ID(), pos.x + x, pos.y + y, pos.w + w, pos.h + h);
    };
    
    AddHotkey("!Home", [WinMove]() {
        info("Home Move full screen");
        auto win = Window(WindowManager::GetActiveWindow());
        auto rect = win.Pos();
        auto monitor = DisplayManager::GetMonitorAt(rect.x, rect.y);
        WinMove(monitor.x, monitor.y, monitor.width, monitor.height); 
    });
    AddHotkey("!@numpad5", [this, WinMove]() {
        info("NP5 Move down");
        WinMove(0, winOffset, 0, 0); 
    });
    
    AddHotkey("!@numpad8", [this, WinMove]() {
        info("NP8 Move up");
        WinMove(0, -winOffset, 0, 0); 
    });
    
    AddHotkey("!@numpad4", [this, WinMove]() {
        WinMove(-winOffset, 0, 0, 0); 
    });
    
    AddHotkey("!@numpad6", [this, WinMove]() {
        WinMove(winOffset, 0, 0, 0); 
    });
    
    AddHotkey("!+@numpad8", [this, WinMove]() {
        WinMove(0, 0, 0, -winOffset); 
    });
    
    AddHotkey("!+@numpad5", [this, WinMove]() {
        WinMove(0, 0, 0, winOffset); 
    });
    
    AddHotkey("!+@numpad4", [this, WinMove]() {
        WinMove(0, 0, -winOffset, 0);
    });
    
    AddHotkey("!+@numpad6", [this, WinMove]() {
        WinMove(0, 0, winOffset, 0); 
    });
    
    AddHotkey("@numpad5", [this]() { 
        info("NP5 Click");
        io.Click(MouseButton::Left, MouseAction::Hold);
    });
    
    AddHotkey("@numpad5:up", [this]() { 
        io.Click(MouseButton::Left, MouseAction::Release);
    });
    
    AddHotkey("@numpadmult", [this]() { 
        io.Click(MouseButton::Right, MouseAction::Hold);
    });
    
    AddHotkey("@numpadmult:up", [this]() { 
        io.Click(MouseButton::Right, MouseAction::Release);
    });
    
    AddHotkey("@numpaddiv", [this]() { 
        io.Click(MouseButton::Middle, MouseAction::Hold);
    });

    AddHotkey("@numpaddiv:up", [this]() { 
        io.Click(MouseButton::Middle, MouseAction::Release);
    });
    
    AddHotkey("@numpad0", [this]() { 
        io.Scroll(-1, 0);
    });
    
    AddHotkey("@numpaddec", [this]() { 
        io.Scroll(1, 0);
    });
    
      AddHotkey("@numpad1", [&]() { 
          mouseController->move(-1, 1);
      });
      
      AddHotkey("@numpad2", [&]() { 
          mouseController->move(0, 1);
      });
      
      AddHotkey("@numpad3", [&]() { 
          mouseController->move(1, 1);
      });
      
      AddHotkey("@numpad4", [&]() { 
          mouseController->move(-1, 0);
      });
      
      AddHotkey("@numpad6", [&]() { 
          mouseController->move(1, 0);
      });
      
      AddHotkey("@numpad7", [&]() { 
          mouseController->move(-1, -1);
      });
      
      AddHotkey("@numpad8", [&]() { 
          mouseController->move(0, -1);
      });
      
      AddHotkey("@numpad9", [&]() { 
          mouseController->move(1, -1);
      });
      
      AddHotkey("@numpad1:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad2:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad3:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad4:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad6:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad7:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad8:up", [&]() { mouseController->resetAcceleration(); });
      AddHotkey("@numpad9:up", [&]() { mouseController->resetAcceleration(); });
    AddHotkey("!+d", [this]() {
        showBlackOverlay();
    });

    AddGamingHotkey("@y",
        [this]() {
            info("Gaming hotkey: Holding 'w' key down");
            io.Send("{w down}");

            static bool keyDown = false;
            keyDown = !keyDown;

            if (keyDown) {
                info("W key pressed and held down");
            } else {
                io.Send("{w up}");
                info("W key released");
            }
        },
        nullptr,
        0
    );

    AddGamingHotkey("'",
        [this]() {
            info("Gaming hotkey: Moving mouse to 1600,700 and autoclicking");
            io.MouseMove(1600, 700, 10, 1.0f);
            startAutoclicker("Button1");
        },
        nullptr, 
        0
    );

    AddHotkey("!delete", 
        [this]() {
            info("Starting autoclicker");
            startAutoclicker("Button1");
        }
    );
    
    static automation::AutoRunner genshinAutoRunner(std::shared_ptr<IO>(&io, [](IO*){}));
    AddGamingHotkey("/",
        []() {
            info("Genshin Impact detected - Starting specialized auto actions");
            genshinAutoRunner.toggle();
        },
        nullptr,
        0
    );

    AddContextualHotkey("enter", "window.title ~ 'Genshin'", [this]() {
        if (genshinAutomationActive) {
            warning("Genshin automation is already active");
            return;
        }

        info("Genshin Impact detected - Starting specialized auto actions");
        showNotification("Genshin Automation", "Starting automation sequence");
        genshinAutomationActive = true;

        startAutoclicker("Button1");

        std::thread([this]() {
            const int maxIterations = 300;
            int counter = 0;

            while (counter < maxIterations && genshinAutomationActive && currentMode == "gaming") {
                if (!evaluateCondition("window.title ~ 'Genshin'")) {
                    info("Genshin automation: Window no longer active");
                    break;
                }

                io.Send("e");
                debug("Genshin automation: Pressed E (" + std::to_string(counter + 1) + "/" + std::to_string(maxIterations) + ")");

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

    AddContextualHotkey("+s", "window.title ~ 'Genshin'", [this]() {
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
        Launcher::runShell("playerctl play-pause");
    }
}
void HotkeyManager::RegisterMediaHotkeys() {
    int mpvBaseId = 10000;
std::vector<HotkeyDefinition> mpvHotkeys = {
    // Volume
    { "+0", [this]() { mpv.VolumeUp(); }, nullptr, mpvBaseId++ },
    { "+9", [this]() { mpv.VolumeDown(); }, nullptr, mpvBaseId++ },
    { "+-", [this]() { mpv.ToggleMute(); }, nullptr, mpvBaseId++ },

    // Playback
    { "@rctrl", [this]() { info("rctrl PlayPause"); PlayPause(); }, nullptr, mpvBaseId++ },
    { "+Esc", [this]() { mpv.Stop(); }, nullptr, mpvBaseId++ },
    { "+PgUp", [this]() { mpv.Next(); }, nullptr, mpvBaseId++ },
    { "+PgDn", [this]() { mpv.Previous(); }, nullptr, mpvBaseId++ },
    // Seek
    { "o", [this]() { mpv.SendCommand({"seek", "-3"}); }, nullptr, mpvBaseId++ },
    { "p", [this]() { mpv.SendCommand({"seek", "3"}); }, nullptr, mpvBaseId++ },

    // Speed
    { "+o", [this]() { mpv.SendCommand({"add", "speed", "-0.1"}); }, nullptr, mpvBaseId++ },
    { "+p", [this]() { mpv.SendCommand({"add", "speed", "0.1"}); }, nullptr, mpvBaseId++ },

    // Subtitles
    { "n", [this]() { mpv.SendCommand({"cycle", "sub-visibility"}); }, nullptr, mpvBaseId++ },
    { "+n", [this]() { mpv.SendCommand({"cycle", "secondary-sub-visibility"}); }, nullptr, mpvBaseId++ },
    { "7", [this]() { mpv.SendCommand({"add", "sub-scale", "-0.1"}); }, nullptr, mpvBaseId++ },
    { "8", [this]() { mpv.SendCommand({"add", "sub-scale", "0.1"}); }, nullptr, mpvBaseId++ },
    { "+z", [this]() { mpv.SendCommand({"add", "sub-delay", "-0.1"}); }, nullptr, mpvBaseId++ },
    { "+x", [this]() { mpv.SendCommand({"add", "sub-delay", "0.1"}); }, nullptr, mpvBaseId++ },
    { "9", [this]() { mpv.SendCommand({"cycle", "sub"}); }, nullptr, mpvBaseId++ },
    { "0", [this]() { mpv.SendCommand({"sub-seek", "0"}); }, nullptr, mpvBaseId++ },
    { "+c", [this]() { mpv.SendCommand({"script-binding", "copy_current_subtitle"}); }, nullptr, mpvBaseId++ },
    { "minus", [this]() { mpv.SendCommand({"sub-seek", "-1"}); }, nullptr, mpvBaseId++ },
    { "equal", [this]() { mpv.SendCommand({"sub-seek", "1"}); }, nullptr, mpvBaseId++ },
    
    { "<",
        [this]() {
            logHotkeyEvent("KEYPRESS", COLOR_YELLOW + "Keycode 94" + COLOR_RESET);
            PlayPause();
        }, nullptr, mpvBaseId++
    }
};
    for (const auto& hk : mpvHotkeys) {
        AddGamingHotkey(hk.key, hk.trueAction, hk.falseAction, hk.id);
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
}

void HotkeyManager::registerAutomationHotkeys() {
    // Register hotkeys for automation tasks
    AddHotkey("!delete", [this]() { toggleAutomationTask("autoclicker", "left"); });
    AddGamingHotkey("slash", [this]() { toggleAutomationTask("autorunner", "w"); });
    AddGamingHotkey("@rshift", [this]() { toggleAutomationTask("autokeypresser", "space"); });
}

void HotkeyManager::startAutoClicker(const std::string& button) {
    try {
        std::lock_guard<std::mutex> lock(automationMutex_);
        auto taskName = "autoclicker_" + button;
        
        if (automationTasks_.find(taskName) == automationTasks_.end()) {
            auto task = automationManager_->createAutoClicker(button, 100); // Default 100ms interval
            if (task) {
                automationTasks_[taskName] = task;
                info("Created AutoClicker for button: " + button);
            }
        }
        
        auto it = automationTasks_.find(taskName);
        if (it != automationTasks_.end() && it->second) {
            it->second->start();
            info("Started AutoClicker for button: " + button);
        }
    } catch (const std::exception& e) {
        error("Failed to start AutoClicker: " + std::string(e.what()));
    }
}

void HotkeyManager::startAutoRunner(const std::string& direction) {
    try {
        std::lock_guard<std::mutex> lock(automationMutex_);
        auto taskName = "autorunner_" + direction;
        
        if (automationTasks_.find(taskName) == automationTasks_.end()) {
            auto task = automationManager_->createAutoRunner(direction, 50); // Default 50ms interval
            if (task) {
                automationTasks_[taskName] = task;
                info("Created AutoRunner for direction: " + direction);
            }
        }
        
        auto it = automationTasks_.find(taskName);
        if (it != automationTasks_.end() && it->second) {
            it->second->start();
            info("Started AutoRunner for direction: " + direction);
        }
    } catch (const std::exception& e) {
        error("Failed to start AutoRunner: " + std::string(e.what()));
    }
}

void HotkeyManager::startAutoKeyPresser(const std::string& key) {
    try {
        std::lock_guard<std::mutex> lock(automationMutex_);
        auto taskName = "autokeypresser_" + key;
        
        if (automationTasks_.find(taskName) == automationTasks_.end()) {
            auto task = automationManager_->createAutoKeyPresser(key, 100); // Default 100ms interval
            if (task) {
                automationTasks_[taskName] = task;
                info("Created AutoKeyPresser for key: " + key);
            }
        }
        
        auto it = automationTasks_.find(taskName);
        if (it != automationTasks_.end() && it->second) {
            it->second->start();
            info("Started AutoKeyPresser for key: " + key);
        }
    } catch (const std::exception& e) {
        error("Failed to start AutoKeyPresser: " + std::string(e.what()));
    }
}

void HotkeyManager::stopAutomationTask(const std::string& taskType) {
    std::lock_guard<std::mutex> lock(automationMutex_);
    
    if (taskType.empty()) {
        // Stop all tasks if no specific type provided
        for (auto& [name, task] : automationTasks_) {
            if (task) {
                task->stop();
                info("Stopped automation task: " + name);
            }
        }
        return;
    }
    
    // Stop all tasks of the given type
    bool found = false;
    for (auto it = automationTasks_.begin(); it != automationTasks_.end(); ) {
        if (it->first.find(taskType) == 0) { // Starts with taskType
            if (it->second) {
                it->second->stop();
                info("Stopped automation task: " + it->first);
            }
            it = automationTasks_.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    
    if (!found) {
        info("No active automation tasks of type: " + taskType);
    }
}

void HotkeyManager::toggleAutomationTask(const std::string& taskType, const std::string& param) {
    std::lock_guard<std::mutex> lock(automationMutex_);
    auto taskName = taskType + "_" + param;
    
    // Check if task exists and is running
    auto it = automationTasks_.find(taskName);
    if (it != automationTasks_.end() && it->second->isRunning()) {
        it->second->stop();
        info("Stopped " + taskType + " for " + param);
        return;
    }
    
    // Otherwise, start the appropriate task
    if (taskType == "autoclicker") {
        startAutoClicker(param);
    } else if (taskType == "autorunner") {
        startAutoRunner(param);
    } else if (taskType == "autokeypresser") {
        startAutoKeyPresser(param);
    } else {
        error("Unknown automation task type: " + taskType);
    }
}

void HotkeyManager::RegisterSystemHotkeys() {
    // System commands
    io.Hotkey("#l", []() {
        Launcher::runShell("xdg-screensaver lock");
    });

    io.Hotkey("+!Esc", []() {
        Launcher::runShell("gnome-system-monitor &");
    });
    // Toggle zooming mode
    AddHotkey("!+z", [this]() {
        setZooming(!isZooming());
        logWindowEvent("ZOOM_MODE", (isZooming() ? "Enabled" : "Disabled"));
    });

    AddHotkey("!d", [this]() {
        showBlackOverlay();
        logWindowEvent("BLACK_OVERLAY", "Showing full-screen black overlay");
    });

    AddHotkey("#2", [this]() {
        printActiveWindowInfo();
    });

    AddHotkey("!+i", [this]() {
        toggleWindowFocusTracking();
    });

    AddHotkey("^!d", [this]() {
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
    info("Removing hotkey: " + hotkeyStr);
    return true;
}

void HotkeyManager::LoadHotkeyConfigurations() {
    info("Loading hotkey configurations...");
}

void HotkeyManager::ReloadConfigurations() {
    info("Reloading configurations");
    LoadHotkeyConfigurations();
    loadVideoSites();
}


void HotkeyManager::updateAllConditionalHotkeys() {
    auto now = std::chrono::steady_clock::now();
    
    // Only check conditions at the specified interval to reduce spam
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastConditionCheck).count() < CONDITION_CHECK_INTERVAL_MS) {
        return;
    }
    lastConditionCheck = now;
    
    // Update mode once per batch of hotkey updates
    if (isGamingWindow()) {
        setMode("gaming");
    } else {
        setMode("default");
    }
    
    // Process all conditional hotkeys
    for (auto& hotkey : conditionalHotkeys) {
        updateConditionalHotkey(hotkey);
    }
}

void HotkeyManager::updateConditionalHotkey(ConditionalHotkey& hotkey) {
    if (verboseConditionLogging) {
        debug("Updating conditional hotkey - Key: '{}', Condition: '{}', CurrentlyGrabbed: {}", 
              hotkey.key, hotkey.condition, hotkey.currentlyGrabbed);
    }
    // Check cache first
    auto now = std::chrono::steady_clock::now();
    bool conditionMet;
    
    auto cacheIt = conditionCache.find(hotkey.condition);
    if (cacheIt != conditionCache.end()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - cacheIt->second.timestamp).count();
        if (age < CACHE_DURATION_MS) {
            conditionMet = cacheIt->second.result;
        } else {
            // Cache expired, re-evaluate
            conditionMet = evaluateCondition(hotkey.condition);
            conditionCache[hotkey.condition] = {conditionMet, now};
        }
    } else {
        // Not in cache, evaluate and store
        conditionMet = evaluateCondition(hotkey.condition);
        conditionCache[hotkey.condition] = {conditionMet, now};
    }
    
    // Only log when condition changes
    if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
        info("Condition changed: {} for {} ({}) - was:{} now:{}", 
             conditionMet ? 1 : 0, hotkey.condition, hotkey.key, 
             hotkey.lastConditionResult, conditionMet);
    }
    
    // Only update hotkey state if needed
    if (conditionMet && !hotkey.currentlyGrabbed) {
        io.GrabHotkey(hotkey.id);
        hotkey.currentlyGrabbed = true;
        if (verboseConditionLogging) {
            debug("Grabbed conditional hotkey: {} ({})", hotkey.key, hotkey.condition);
        }
    } else if (!conditionMet && hotkey.currentlyGrabbed) {
        io.UngrabHotkey(hotkey.id);
        hotkey.currentlyGrabbed = false;
        if (verboseConditionLogging) {
            debug("Ungrabbed conditional hotkey: {} ({})", hotkey.key, hotkey.condition);
        }
    }
    
    hotkey.lastConditionResult = conditionMet;
}
int HotkeyManager::AddGamingHotkey(const std::string& key,
                                   std::function<void()> trueAction,
                                   std::function<void()> falseAction, int id) {
    int gamingHotkeyId = AddContextualHotkey(key, "mode == 'gaming'",
        [this]() { setMode("gaming"); },
        [this]() { setMode("default"); }, id);
    gamingHotkeyIds.push_back(gamingHotkeyId);
    return gamingHotkeyId;
}
int HotkeyManager::AddContextualHotkey(const std::string& key, const std::string& condition,
                       std::function<void()> trueAction,
                       std::function<void()> falseAction,
                       int id) {
    debug("Registering contextual hotkey - Key: '{}', Condition: '{}', ID: {}", key, condition, id);
    if (id == 0) {
        static int nextId = 1000;
        id = nextId++;
    }

    auto action = [this, condition, trueAction, falseAction]() {
        if (evaluateCondition(condition)) {
            if (trueAction) trueAction();
        } else {
            if (falseAction) falseAction();
        }
    };

    // Store the conditional hotkey for dynamic management
    ConditionalHotkey ch;
    ch.id = id;
    ch.key = key;
    ch.condition = condition;
    ch.trueAction = trueAction;
    ch.falseAction = falseAction;
    ch.currentlyGrabbed = true;
    
    conditionalHotkeys.push_back(ch);
    conditionalHotkeyIds.push_back(id);

    // Register but don't grab yet
    io.Hotkey(key, action, id);
    
    // Initial evaluation and grab if needed
    updateConditionalHotkey(conditionalHotkeys.back());

    return id;
}

    void HotkeyManager::setupConditionEngine() {
    conditionEngine->registerProperty("window.title", PropertyType::STRING, 
        []() -> std::string {
            wID activeWindow = WindowManager::GetActiveWindow();
            if (activeWindow == 0) return "";
            try {
                Window window(std::to_string(activeWindow), activeWindow);
                return window.Title();
            } catch (...) {
                return "";
            }
        });
    
    conditionEngine->registerProperty("window.class", PropertyType::STRING,
        []() -> std::string {
            return WindowManager::GetActiveWindowClass();
        });
    
    conditionEngine->registerProperty("window.pid", PropertyType::INTEGER,
        []() -> std::string {
            return std::to_string(WindowManager::GetActiveWindowPID());
        });

        conditionEngine->registerProperty("mode", PropertyType::STRING,
            []() -> std::string {
                std::lock_guard<std::mutex> lock(modeMutex);
                return currentMode;
            });
        
        conditionEngine->registerBoolProperty("gaming.active",
            []() -> bool {
                return isGamingWindow();
            });
    conditionEngine->registerProperty("time.hour", PropertyType::INTEGER,
        []() -> std::string {
            auto now = std::time(nullptr);
            auto* tm = std::localtime(&now);
            return std::to_string(tm->tm_hour);
        });
    
    conditionEngine->registerListProperty("gaming.apps", 
        []() -> std::vector<std::string> {
            return Configs::Get().GetGamingApps();
        });
}

bool HotkeyManager::evaluateCondition(const std::string& condition) {
    conditionEngine->invalidateCache();
    bool result = conditionEngine->evaluateCondition(condition);

        if (verboseConditionLogging) {
        logWindowEvent("CONDITION_EVAL", condition + " -> " + (result ? "TRUE" : "FALSE"));
        }
        return result;
    }
void HotkeyManager::showNotification(const std::string& title, const std::string& message) {
    std::string cmd = "notify-send \"" + title + "\" \"" + message + "\"";
    Launcher::runShell(cmd.c_str());
}

bool HotkeyManager::isGamingWindow() {
    std::string windowClass = WindowManager::GetActiveWindowClass();
    std::string windowTitle = "";
    
    wID activeWin = WindowManager::GetActiveWindow();
    if (activeWin != 0) {
        try {
            Window w(activeWin);
            windowTitle = w.Title();
        } catch (...) { /* ignore */ }
    }
    
    std::transform(windowClass.begin(), windowClass.end(), windowClass.begin(), ::tolower);
    std::transform(windowTitle.begin(), windowTitle.end(), windowTitle.begin(), ::tolower);

    const std::vector<std::string> gamingApps = Configs::Get().GetGamingApps();
    
    for (const auto& app : gamingApps) {
        if (windowClass.find(app) != std::string::npos || windowTitle.find(app) != std::string::npos) {
            return true;
        }
    }

    return false;
}
void HotkeyManager::startAutoclicker(const std::string& button) {
    // Stop if already running
    if (autoClicker && autoClicker->isRunning()) {
        info("Stopping autoclicker - toggled off");
        autoClicker->stop();
        autoClicker.reset();
        return;
    }

    if (!isGamingWindow()) {
        debug("Autoclicker not activated - not in gaming window");
        return;
    }
    
    wID currentWindow = WindowManager::GetActiveWindow();
    if (!currentWindow) {
        error("Failed to get active window for autoclicker");
        return;
    }

    autoclickerWindowID = currentWindow;
    
    try {
        info("Starting autoclicker (" + button + ") in window: " + std::to_string(autoclickerWindowID));
        
        autoClicker = std::make_unique<havel::automation::AutoClicker>(std::shared_ptr<IO>(&io, [](IO*){}));
        
        // Set the appropriate click type based on button
        if (button == "Button1" || button == "Left") {
            autoClicker->setClickType(havel::automation::AutoClicker::ClickType::Left);
        } else if (button == "Button2" || button == "Right") {
            autoClicker->setClickType(havel::automation::AutoClicker::ClickType::Right);
        } else if (button == "Button3" || button == "Middle") {
            autoClicker->setClickType(havel::automation::AutoClicker::ClickType::Middle);
        } else if (button == "Side1" || button == "Side2") {
            // For side buttons, use a custom click function
            autoClicker->setClickFunction([this, button]() {
                if (button == "Side1") {
                    io.Click(MouseButton::Side1, MouseAction::Click);
                } else {
                    io.Click(MouseButton::Side2, MouseAction::Click);
                }
            });
        } else {
            error("Invalid mouse button: " + button);
            return;
        }
        
        // Start the autoclicker with default interval
        autoClicker->setIntervalMs(50);
        autoClicker->start(); 
        
    } catch (const std::exception& e) {
        error("Failed to start autoclicker: " + std::string(e.what()));
        autoClicker.reset();
    }
}

void HotkeyManager::stopAllAutoclickers() {
    if (autoClicker && autoClicker->isRunning()) {
        info("Force stopping all autoclickers");
        autoClicker->stop();
        autoClicker.reset();
    }
    autoclickerWindowID = 0;
}

std::string HotkeyManager::handleKeycode(const std::string& input) {
    if (input.size() < 2 || input[0] != 'k' || input[1] != 'e') {
        return input;
    }
    
    std::string numStr = input.substr(2);
    try {
        int keycode = std::stoi(numStr);
        Display* display = XOpenDisplay(nullptr);
        if (!display) {
            error("Failed to open X display for keycode conversion");
            return input;
        }
        
        KeySym keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
        char* keyName = XKeysymToString(keysym);
        XCloseDisplay(display);

        if (keyName) {
            std::string result(keyName);
            return result;
        }
    } catch (const std::exception& e) {
        error("Failed to convert keycode: " + input + " - " + e.what());
    }
    return input;
}

std::string HotkeyManager::handleScancode(const std::string& input) {
    std::string numStr = input.substr(2);
    try {
        int scancode = std::stoi(numStr);
        int keycode = scancode + 8; // Common offset on Linux
        return handleKeycode("kc" + std::to_string(keycode));
    } catch (const std::exception& e) {
        error("Failed to convert scancode: " + input + " - " + e.what());
    }
    return input;
}

std::string HotkeyManager::normalizeKeyName(const std::string& keyName) {
    std::string normalized = keyName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    auto it = keyNameAliases.find(normalized);
    if (it != keyNameAliases.end()) {
        return it->second;
    }

    if (normalized.length() == 1 && std::isalpha(normalized[0])) {
        return normalized;
    }

    const std::regex fkeyRegex("^f([1-9]|1[0-9]|2[0-4])$");
    if (std::regex_match(normalized, fkeyRegex)) {
        return "F" + normalized.substr(1);
    }

    return keyName;
}

std::string HotkeyManager::convertKeyName(const std::string& keyName) {
    std::string result;
    if (keyName.substr(0, 2) == "kc") {
    result = keyName;
    logKeyConversion(keyName, result);
    return result;
}

if (keyName.substr(0, 2) == "sc") {
    result = handleScancode(keyName);
    logKeyConversion(keyName, result);
    return result;
}

if (keyName == "Menu") {
    result = "kc135";
    logKeyConversion(keyName, result);
    return result;
}

if (keyName == "NoSymbol") {
    result = "kc0";
    logKeyConversion(keyName, result);
    return result;
}

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
    while (std::getline(ss, part, '+')) {
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) {
            parts.push_back(convertKeyName(part));
        }
    }

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
    wID activeWindow = WindowManager::GetActiveWindow();
std::string windowClass = WindowManager::GetActiveWindowClass();
std::string windowTitle;
try {
    Window window(std::to_string(activeWindow), activeWindow);
    windowTitle = window.Title();
} catch (...) {
    windowTitle = "<error getting title>";
}

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

    if (windowId) {
        try {
            if (windowId == WindowManager::GetActiveWindow()) {
                windowClass = WindowManager::GetActiveWindowClass();
            } else {
                windowClass = "<not implemented for non-active>";
            }

            Window window(std::to_string(windowId), windowId);
            title = window.Title();
        } catch (...) {
            windowClass = "<error>";
            title = "<error getting title>";
        }
    } else {
        windowClass = "<no window>";
        title = "<no window>";
    }

    return COLOR_BOLD + COLOR_CYAN + "Class: " + COLOR_RESET + windowClass +
           COLOR_BOLD + COLOR_CYAN + " | Title: " + COLOR_RESET + title +
           COLOR_BOLD + COLOR_CYAN + " | ID: " + COLOR_RESET + std::to_string(windowId);
}

bool HotkeyManager::isVideoSiteActive() {
    wID windowId = WindowManager::GetActiveWindow();
    if (windowId == 0) return false;
    
    Window window(std::to_string(windowId), windowId);
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

        if (!mpvCommand.empty()) {
            const std::string& cmd = mpvCommand[0];
            if (cmd == "cycle" || cmd == "pause") {
                Launcher::runShell("playerctl play-pause");
            }
            else if (cmd == "seek" && mpvCommand.size() > 1) {
                if (mpvCommand[1] == "-3") {
                    Launcher::runShell("playerctl position 3-");
                } else if (mpvCommand[1] == "3") {
                    Launcher::runShell("playerctl position 3+");
                }
            }
        }
    } else {
        if (mpvCommand.empty()) {
            if (verboseWindowLogging) {
                logWindowEvent("MEDIA_CONTROL", "No MPV command provided");
            }
            return;
        }

        if (verboseWindowLogging) {
            std::string commandStr;
            for (const auto& arg : mpvCommand) {
                commandStr += arg + " ";
            }
            logWindowEvent("MEDIA_CONTROL", "Sending MPV command: " + commandStr);
        }
        
        // Create a copy of the command vector to pass to SendCommand
        std::vector<std::string> commandToSend = mpvCommand;
        mpv.SendCommand(commandToSend);
    }
}

void HotkeyManager::setMode(const std::string& newMode) {
    std::string oldMode;
    bool modeChanged = false;
    
    {
        std::lock_guard<std::mutex> lock(modeMutex);
        if (currentMode != newMode) {
            oldMode = currentMode;
            currentMode = newMode;
            modeChanged = true;
        } else {
            return; // No change needed
        }
    }
    
    if (modeChanged) {
        logModeSwitch(oldMode, newMode);
        
        // Clear condition cache when mode changes
        conditionCache.clear();
        if (verboseConditionLogging) {
            debug("Cleared condition cache due to mode change: {} → {}", oldMode, newMode);
        }
    }
}

void HotkeyManager::printCacheStats() {
    info("Condition cache: {} entries", conditionCache.size());
    
    if (conditionCache.empty()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    int expired = 0;
    
    for (const auto& [condition, cache] : conditionCache) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - cache.timestamp).count();
        if (age >= CACHE_DURATION_MS) {
            expired++;
        }
        
        if (verboseConditionLogging) {
            debug("  - '{}': {} ({}ms old)", 
                 condition, 
                 cache.result ? "true" : "false",
                 age);
        }
    }
    
    info("Cache stats: {} fresh, {} expired ({}% hit rate)", 
         conditionCache.size() - expired, 
         expired,
         conditionCache.size() > 0 ? 
             (100 * (conditionCache.size() - expired) / conditionCache.size()) : 
             0);
}

void HotkeyManager::showBlackOverlay() {
    info("Showing black overlay window on all monitors");
    QWidget* blackOverlay = new QWidget;
    blackOverlay->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    blackOverlay->setStyleSheet("background-color: black;");
    blackOverlay->showFullScreen();
    blackOverlay->raise();
    blackOverlay->activateWindow();
    blackOverlay->show();
}

void HotkeyManager::printActiveWindowInfo() {
    wID activeWindow = WindowManager::GetActiveWindow();
    if (activeWindow == 0) {
        info("╔══════════════════════════════════════╗");
        info("║      NO ACTIVE WINDOW DETECTED       ║");
        info("╚══════════════════════════════════════╝");
        return;
    }
    
    std::string windowClass = WindowManager::GetActiveWindowClass();
    std::string windowTitle;
    int x = 0, y = 0, width = 0, height = 0;
    
    try {
        havel::Window window("ActiveWindow", activeWindow);
        windowTitle = window.Title();
        
        // Get window geometry
        ::Window root;
        unsigned int border_width, depth;
        Display* display = XOpenDisplay(nullptr);
        if (display) {
            XGetGeometry(display, activeWindow, &root, &x, &y, 
                        (unsigned int*)&width, (unsigned int*)&height, 
                        &border_width, &depth);
            XCloseDisplay(display);
        }
    } catch (const std::exception& e) {
        windowTitle = "<error>";
        error("Failed to get window information: " + std::string(e.what()));
    } catch (...) {
        windowTitle = "<unknown error>";
        error("Unknown error getting window information");
    }
    std::string geometry = std::to_string(width) + "x" + std::to_string(height) + "+" + std::to_string(x) + "+" + std::to_string(y);
    bool isGaming = isGamingWindow();

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

    std::string gamingStatus = isGaming ? 
        (COLOR_GREEN + std::string("YES ✓") + COLOR_RESET) : 
        (COLOR_RED + std::string("NO ✗") + COLOR_RESET);
    info(formatLine("Is Gaming Window: ", gamingStatus));
    
    {
        std::lock_guard<std::mutex> lock(modeMutex);
        info(formatLine("Current Mode: ", currentMode));
    }
    info("╚══════════════════════════════════════════════════════════╝");

    logWindowEvent("WINDOW_INFO",
        "Title: \"" + windowTitle + "\", Class: \"" + windowClass +
        "\", Gaming: " + (isGamingWindow() ? "YES" : "NO") + ", Geometry: " + geometry);
}

void HotkeyManager::cleanup() {
    stopAllAutoclickers();
    setMode("default");
    io.Send("{LAlt up}{RAlt up}{LShift up}{RShift up}{LCtrl up}{RCtrl up}{LWin up}{RWin up}");
    
    // Stop all automation tasks
    {
        std::lock_guard<std::mutex> lock(automationMutex_);
        for (auto& [name, task] : automationTasks_) {
            if (task) {
                task->stop();
                if (verboseWindowLogging) {
                    logWindowEvent("AUTOMATION_CLEANUP", "Stopped automation task: " + name);
                }
            }
        }
        automationTasks_.clear();
    }
    
    // Clean up old automation objects if they exist
    if (autoClicker) {
        autoClicker->stop();
        autoClicker.reset();
    }
    if (autoRunner) {
        autoRunner->stop();
        autoRunner.reset();
    }
    if (autoKeyPresser) {
        autoKeyPresser->stop();
        autoKeyPresser.reset();
    }
    
    if (verboseWindowLogging) {
        logWindowEvent("CLEANUP", "HotkeyManager resources cleaned up");
    }
}

void HotkeyManager::toggleWindowFocusTracking() {
    trackWindowFocus = !trackWindowFocus;
    if (trackWindowFocus) {
        info("Window focus tracking ENABLED - will log all window changes");
        logWindowEvent("FOCUS_TRACKING", "Enabled");

        lastActiveWindowId = WindowManager::GetActiveWindow();
        if (lastActiveWindowId != 0) {
            printActiveWindowInfo();
        }
    } else {
        info("Window focus tracking DISABLED");
        logWindowEvent("FOCUS_TRACKING", "Disabled");
    }
}

void HotkeyManager::loadDebugSettings() {
    info("Loading debug settings from config");
    
    bool keyLogging = Configs::Get().Get<bool>("Debug.VerboseKeyLogging", false);
    bool windowLogging = Configs::Get().Get<bool>("Debug.VerboseWindowLogging", false);
    bool conditionLogging = Configs::Get().Get<bool>("Debug.VerboseConditionLogging", false);

    setVerboseKeyLogging(keyLogging);
    setVerboseWindowLogging(windowLogging);
    setVerboseConditionLogging(conditionLogging);

    info("Debug settings: KeyLogging=" + std::to_string(verboseKeyLogging) +
         ", WindowLogging=" + std::to_string(verboseWindowLogging) +
         ", ConditionLogging=" + std::to_string(verboseConditionLogging));
}

void HotkeyManager::applyDebugSettings() {
    if (verboseKeyLogging) {
        info("Verbose key logging is enabled");
    }
    
    if (verboseWindowLogging) {
        info("Verbose window logging is enabled");
    }

    if (verboseConditionLogging) {
        info("Verbose condition logging is enabled");
    }

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