#include "HotkeyManager.hpp"
#include "../process/Launcher.hpp"
#include "../utils/Logger.hpp"
#include "IO.hpp"
#include "automation/AutoRunner.hpp"
#include "core/BrightnessManager.hpp"
#include "core/ConditionSystem.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/io/EventListener.hpp" // Include EventListener for access to its members
#include "core/io/KeyTap.hpp"
#include "core/process/ProcessManager.hpp"
#include "gui/AutomationSuite.hpp"
#include "gui/ClipboardManager.hpp"
#include "gui/HavelApp.hpp"
#include "media/AudioManager.hpp"
#include "process/ProcessManager.hpp"
#include "utils/Timer.hpp"
#include "utils/Util.hpp"
#include "utils/Utils.hpp"
#include "window/CompositorBridge.hpp"
#include "window/Window.hpp"
#include "window/WindowManager.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <fstream>
#ifdef ENABLE_HAVEL_LANG
#include "havel-lang/runtime/Interpreter.hpp"
#endif
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <qclipboard.h>
#include <qpainter.h>
#include <qscreen.h>
#include <regex>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>
// Include XRandR for multi-monitor support
#ifdef __linux__
#include <X11/extensions/Xrandr.h>
#endif
namespace havel {
std::string HotkeyManager::currentMode = "default";
std::mutex HotkeyManager::modeMutex;
std::mutex HotkeyManager::conditionCacheMutex;
std::vector<ConditionalHotkey> HotkeyManager::conditionalHotkeys;

std::string HotkeyManager::getMode() const {
  std::lock_guard<std::mutex> lock(modeMutex);
  return currentMode;
}

bool HotkeyManager::getCurrentGamingWindowStatus() const {
  return isGamingWindow();
}

void HotkeyManager::reevaluateConditionalHotkeys(IO &io) {
  std::lock_guard<std::mutex> lock(hotkeyMutex);
  for (auto &ch : conditionalHotkeys) {
    // Evaluate condition based on common patterns
    bool shouldGrab = false;

    // For function-based conditions, use the function directly
    if (ch.usesFunctionCondition) {
      if (ch.conditionFunc) {
        shouldGrab = ch.conditionFunc();
      }
    } else {
      // For mode-based conditions
      std::string currentActiveMode = getMode();
      if (ch.condition.find("mode == 'gaming'") != std::string::npos) {
        shouldGrab = (currentActiveMode == "gaming");
      } else if (ch.condition.find("mode != 'gaming'") != std::string::npos) {
        shouldGrab = (currentActiveMode != "gaming");
      } else {
        // For other conditions, default to checking gaming window state
        shouldGrab = getCurrentGamingWindowStatus();
      }
    }

    // Update grab state based on condition evaluation
    if (shouldGrab && !ch.currentlyGrabbed) {
      io.GrabHotkey(ch.id);
      ch.currentlyGrabbed = true;
      ch.lastConditionResult = true;
    } else if (!shouldGrab && ch.currentlyGrabbed) {
      io.UngrabHotkey(ch.id);
      ch.currentlyGrabbed = false;
      ch.lastConditionResult = false;
    }
  }
}

void HotkeyManager::Zoom(int zoom) {
  if (zoom < 0)
    zoom = 0;
  else if (zoom > 4)
    zoom = 4;

  // Check if running on KDE/KWin and use qdbus for zoom commands
  if (CompositorBridge::IsKDERunning()) {
    std::string command;
    switch (zoom) {
    case 0: // zoomOut (from original function: zoom = 0 calls
            // io.Send("@^{Down}"))
      command = "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomOutDBus";
      break;
    case 1: // zoomIn (from original function: zoom = 1 calls io.Send("@^{Up}"))
      command = "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomInDBus";
      break;
    case 2: // resetZoom
      command = "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.resetZoomDBus";
      break;
    case 3:
      char buffer[128];
      std::snprintf(
          buffer, sizeof(buffer),
          "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomToValueDBus %f",
          Configs::Get().Get<double>("Zoom.Zoom3", 2.0));
      command = std::string(buffer);
      break;
    case 4: // zoomTo140
      command = "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.zoomTo140DBus";
      break;
    default:
      break;
    }

    if (!command.empty()) {
      if (CompositorBridge::SendKWinZoomCommand(command)) {
        info("KWin zoom command executed: {}", command);
        try {
          std::string result = CompositorBridge::SendKWinZoomCommandWithOutput(
              "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.getZoomLevelDBus");
          if (!result.empty()) {
            zoomLevel = std::stod(result);
          }
        } catch (const std::exception &e) {
          warn("Failed to parse zoom level after zoom command: {}", e.what());
        }
      } else {
        warn("KWin zoom command failed, falling back to hotkeys: {}", command);
        // Fall back to original hotkey methods
        if (zoom == 1) {
          io.Send("@^{Up}");
          zoomLevel += 0.1;
        } else if (zoom == 0) {
          io.Send("@^{Down}");
          zoomLevel -= 0.1;
        } else if (zoom == 2) {
          io.Send("@^/");
          zoomLevel = 1.0;
        } else if (zoom == 3) {
          io.Send("@^+/");
          zoomLevel = 1.5;
        }
      }
    }
  } else {
    // Use original hotkey methods
    if (zoom == 1) {
      io.Send("@^{Up}");
      zoomLevel += 0.1;
    } else if (zoom == 0) {
      io.Send("@^{Down}");
      zoomLevel -= 0.1;
    } else if (zoom == 2) {
      io.Send("@^/");
      zoomLevel = 1.0;
    } else if (zoom == 3) {
      io.Send("@^+/");
      zoomLevel = 1.5;
    }
  }

  if (zoomLevel < 1.0) {
    zoomLevel = 1.0;
  } else if (zoomLevel > 100.0) {
    zoomLevel = 100.0;
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

  for (const auto &[id, hotkey] : IO::hotkeys) {
    std::string status =
        "Hotkey[" + std::to_string(id) + "] " + "alias='" + hotkey.alias +
        "' " + "key=" + std::to_string(hotkey.key) + " " +
        "mod=" + std::to_string(hotkey.modifiers) + " " + "action='" +
        hotkey.action + "' " + "enabled=" + (hotkey.enabled ? "Y" : "N") + " " +
        "grab=" + (hotkey.grab ? "Y" : "N") + " " +
        "succ=" + (hotkey.success ? "Y" : "N") + " " +
        "susp=" + (hotkey.suspend ? "Y" : "N") + " " +
        "repeat=" + std::to_string(hotkey.repeatInterval) + " " +
        "combo=" + std::to_string(hotkey.comboTimeWindow) + " " +
        "repeatInterval=" + std::to_string(hotkey.repeatInterval) + " " +
        "comboTimeWindow=" + std::to_string(hotkey.comboTimeWindow);
    info(status);
  }
  info("-------------------------------------");
  // red color
  std::cout << "\033[31m";
  info("==== FAILED HOTKEYS ====");
  int i = 0;
  // Collect failed aliases into a set
  std::set<std::string> failedAliases;
  std::ostringstream failedStream;

  for (const auto &failed : io.failedHotkeys) {
    failedAliases.insert(failed.alias);
    std::cout << failed.alias;
    if (failedStream.tellp() > 0)
      failedStream << " ";
    failedStream << failed.alias;
  }
  std::cout << "\n";

  std::string joined = failedStream.str();
  info("Failed hotkeys: " + joined);

  // Filter working hotkeys
  std::ostringstream workingStream;
  for (const auto &[key, hotkey] : io.hotkeys) {
    if (failedAliases.find(hotkey.alias) == failedAliases.end()) {
      if (workingStream.tellp() > 0)
        workingStream << " ";
      workingStream << hotkey.alias;
    }
  }
  // Reset color
  std::cout << "\033[0m";

  std::string workingHotkeys = workingStream.str();
  info("Working hotkeys: " + workingHotkeys);

  info("=== End Hotkey Report ===");
}
HotkeyManager::HotkeyManager(
    IO &io, WindowManager &windowManager, MPVController &mpv,
    AudioManager &audioManager, ScriptEngine &scriptEngine,
    ScreenshotManager &screenshotManager, BrightnessManager &brightnessManager,
    std::shared_ptr<net::NetworkManager> networkManager)
    : io(io), windowManager(windowManager), mpv(mpv),
      audioManager(audioManager), scriptEngine(scriptEngine),
      brightnessManager(brightnessManager),
      screenshotManager(screenshotManager), networkManager(networkManager) {
  mouseController = std::make_unique<MouseController>(io);
  conditionEngine = std::make_unique<ConditionEngine>();
  setupConditionEngine();
  loadVideoSites();
  loadDebugSettings();
  applyDebugSettings();

  // Initialize auto clicker with IO reference
  autoClicker = std::make_unique<automation::AutoClicker>(
      std::shared_ptr<IO>(&io, [](IO *) {}));
  // Initialize automation manager
  automationManager_ = std::make_shared<automation::AutomationManager>(
      std::shared_ptr<IO>(&io, [](IO *) {}));
  currentMode = "default";

  // Start the update loop thread - this will handle all condition checking
  {
    std::lock_guard<std::mutex> lock(modeMutex);
    currentMode = "default"; // Start in default mode to prevent all gaming
                             // hotkeys from being enabled initially
  }
  // Set initial key mappings to default
  io.Map("Left", "Left");
  io.Map("Right", "Right");
  io.Map("Up", "Up");
  io.Map("Down", "Down");
  updateLoopRunning.store(true);
  updateLoopThread = std::thread(&HotkeyManager::UpdateLoop, this);

  // Initialize lastInputTime to current time
  lastInputTime.store(std::chrono::steady_clock::now());

  // Load configurable input freeze timeout (default to 300 seconds = 5 minutes)
  inputFreezeTimeoutSeconds =
      Configs::Get().Get<int>("Input.FreezeTimeoutSeconds", 300);

  // Start the watchdog thread
  watchdogRunning.store(true);
  watchdogThread = std::thread(&HotkeyManager::WatchdogLoop, this);

  // Set up input notification callback if using EventListener
  if (io.GetEventListener() && io.IsUsingNewEventListener()) {
    io.GetEventListener()->inputNotificationCallback = [this]() {
      NotifyInputReceived();
    };
  }
}

void HotkeyManager::loadVideoSites() {
  // Clear existing sites
  videoSites.clear();

  // Get video sites from config
  std::string sitesStr = Configs::Get().Get<std::string>(
      "VideoSites.Sites", "netflix,animelon,youtube");

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
    for (const auto &site : videoSites) {
      if (!siteList.empty())
        siteList += ", ";
      siteList += site;
    }
    logWindowEvent("CONFIG", "Loaded video sites: " + siteList);
  }
}

bool HotkeyManager::hasVideoTimedOut() const {
  if (lastVideoCheck == 0)
    return true;
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
      logWindowEvent("VIDEO_TIMEOUT",
                     "Video playback status reset due to timeout");
    }
    videoPlaying = false;
    return;
  }

  // Update the last check time since we found a video site
  updateLastVideoCheck();
  videoPlaying = true;

  if (verboseWindowLogging) {
    logWindowEvent("VIDEO_STATUS",
                   videoPlaying ? "Video is playing" : "No video playing");
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

  io.Hotkey("!Esc", [this]() {
    if (App::instance()) {
      info("Quitting application");
      App::quit();
    }
  });
  io.Hotkey("#!Esc", [this]() {
    auto pid = ProcessManager::getCurrentPid();
    std::string exec = ProcessManager::getProcessExecutablePath(pid);

    info("Restarting {}", exec);

    // flush logs
    fflush(nullptr);

    std::vector<char *> args;
    args.push_back(const_cast<char *>(exec.c_str()));
    args.push_back(nullptr);

    execvp(exec.c_str(), args.data());

    // If we get here, exec failed.
    error("Failed to exec: {}", strerror(errno));
    if (App::instance()) {
      App::quit();
    }
  });

  // Media Controls
  io.Hotkey("#f1", []() { Launcher::runShell("playerctl previous"); });

  io.Hotkey("#f2", []() { Launcher::runShell("playerctl play-pause"); });

  io.Hotkey("#f3", []() { Launcher::runShell("playerctl next"); });
  io.Hotkey("numpaddiv", [this]() {
    audioManager.toggleMute();
    bool muted = audioManager.isMuted();
    showNotification("Mute", muted ? "Muted" : "Unmuted");
  });
  io.Hotkey("^+!a", [this]() {
    auto devices = audioManager.getDevices();
    for (const auto &device : devices) {
      info("Device: {} ({}) Vol: {:.0f}%", device.name, device.description,
           device.volume * 100);
    }
  });
  io.Hotkey("^+#a", [this]() {
    auto device = audioManager.findDeviceByIndex(0);
    if (device) {
      audioManager.setDefaultOutput(device->name);
      info("Current device: {}", audioManager.getDefaultOutput());
      // play audio
      audioManager.playTestSound();
    }
  });
  io.Hotkey("^+s", [this]() {
    auto device = audioManager.findDeviceByIndex(1);
    if (device) {
      audioManager.setDefaultOutput(device->name);
      info("Current device: {}", audioManager.getDefaultOutput());
      // play audio
      audioManager.playTestSound();
    }
  });
  io.Hotkey("^+d", [this]() {
    auto device = audioManager.findDeviceByIndex(2);
    if (device) {
      audioManager.setDefaultOutput(device->name);
      info("Current device: {}", audioManager.getDefaultOutput());
      // play audio
      audioManager.playTestSound();
    }
  });
  io.Hotkey("^+!a", [this]() {
    auto device = audioManager.findDeviceByIndex(3);
    if (device) {
      audioManager.setDefaultOutput(device->name);
      info("Current device: {}", audioManager.getDefaultOutput());
      // play audio
      audioManager.playTestSound();
    }
  });
  io.Hotkey("^numpadsub", [this]() {
    auto phone = audioManager.findDeviceByName("G30");
    auto bt = audioManager.findDeviceByName("200");
    if (bt) {
      audioManager.increaseVolume(bt->name, 0.05);
    }
    if (phone) {
      audioManager.increaseVolume(phone->name, 0.05);
      double vol = audioManager.getVolume(phone->name);
      showNotification("Volume (G30)",
                       std::to_string(static_cast<int>(vol * 100)) + "%");
      info("Current volume (G30): {:.0f}%", vol * 100);
    }
  });
  io.Hotkey("^numpadadd", [this]() {
    auto phone = audioManager.findDeviceByName("G30");
    auto bt = audioManager.findDeviceByName("200");
    if (bt) {
      audioManager.decreaseVolume(bt->name, 0.05);
    }
    if (phone) {
      audioManager.decreaseVolume(phone->name, 0.05);
      double vol = audioManager.getVolume(phone->name);
      showNotification("Volume (G30)",
                       std::to_string(static_cast<int>(vol * 100)) + "%");
      info("Current volume (G30): {:.0f}%", vol * 100);
    }
  });
  io.Hotkey("^numpad0", [this]() {
    auto phone = audioManager.findDeviceByName("G30");
    auto bt = audioManager.findDeviceByName("200");
    if (bt) {
      audioManager.setVolume(bt->name, 0);
    }
    if (phone) {
      audioManager.setVolume(phone->name, 0);
    }
    showNotification("Volume (G30)", "0%");
    info("Current volume (G30): 0%");
  });
  io.Hotkey("+numpadsub", [this]() {
    auto builtIn = audioManager.findDeviceByName("Built-in Audio");
    if (builtIn) {
      audioManager.increaseVolume(builtIn->name, 0.05);
      double vol = audioManager.getVolume(builtIn->name);
      showNotification("Volume (Built-in)",
                       std::to_string(static_cast<int>(vol * 100)) + "%");
      info("Current volume (Built-in Audio): {:.0f}%", vol * 100);
    }
  });
  io.Hotkey("+numpadadd", [this]() {
    auto builtIn = audioManager.findDeviceByName("Built-in Audio");
    if (builtIn) {
      audioManager.decreaseVolume(builtIn->name, 0.05);
      double vol = audioManager.getVolume(builtIn->name);
      showNotification("Volume (Built-in)",
                       std::to_string(static_cast<int>(vol * 100)) + "%");
      info("Current volume (Built-in Audio): {:.0f}%", vol * 100);
    }
  });
  io.Hotkey("+numpad0", [this]() {
    auto builtIn = audioManager.findDeviceByName("Built-in Audio");
    if (builtIn) {
      audioManager.setVolume(builtIn->name, 0);
      showNotification("Volume (Built-in)", "0%");
      info("Current volume (Built-in Audio): 0%");
    }
  });
  io.Hotkey("@numpadsub", [this]() {
    audioManager.decreaseVolume(0.05);
    double vol = audioManager.getVolume();
    showNotification("Volume",
                     std::to_string(static_cast<int>(vol * 100)) + "%");
    info("Current volume (Default): {:.0f}%", vol * 100);
  });

  io.Hotkey("@numpadadd", [this]() {
    audioManager.increaseVolume(0.05);
    double vol = audioManager.getVolume();
    showNotification("Volume",
                     std::to_string(static_cast<int>(vol * 100)) + "%");
    info("Current volume (Default): {:.0f}%", vol * 100);
  });

  io.Hotkey("f6", [this]() { PlayPause(); });

  // Application Shortcuts
  io.Hotkey("@|rwin", [this]() { io.Send("@!{backspace}"); });

  // LWin key handling using KeyTap with function-based conditions instead of
  // complex condition strings
  lwin = std::make_unique<KeyTap>(
      io, *this, "lwin",
      [this]() {
        if (!CompositorBridge::IsKDERunning()) {
          Launcher::runAsync("/bin/xfce4-popup-whiskermenu");
        } else {
          CompositorBridge::SendKWinZoomCommand(
              "org.kde.plasmashell /PlasmaShell "
              "org.kde.PlasmaShell.activateLauncherMenu");
        }
      },                 // Tap action
      [this]() -> bool { // Tap condition function
        // Don't trigger in gaming mode
        if (getMode() == "gaming") {
          return false;
        }

        // Don't trigger in disabled window classes
        std::string currentWindowClass = WindowManager::GetActiveWindowClass();
        std::string disabledClasses = Configs::Get().Get<std::string>(
            "General.LWinDisabledClasses",
            "remote-viewer,virt-viewer,gnome-boxes");

        if (isWindowClassInList(currentWindowClass, disabledClasses)) {
          return false;
        }

        return true; // Allow tap
      },
      [this]() { PlayPause(); }, // Combo action
      "mode == 'gaming'", false, true);
  lwin->setup();

  ralt = std::make_unique<KeyTap>(
      io, *this, "ralt",
      [this]() {
        if (CompositorBridge::IsKDERunning()) {
          auto win = havel::Window(WindowManager::GetActiveWindow());
          if (win.Pos().x < 0) {
            io.Send("#+{Right}");
          } else {
            io.Send("#+{Left}");
          }
        } else {
          WindowManager::MoveWindowToNextMonitor();
        }
      },
      "", nullptr, "", false, true);
  ralt->setup();

  // Browser navigation hotkeys
  //  io.Hotkey("@+Rshift", [this]() { io.Send("{back}"); });      // +Rshift
  //  sends browser back io.Hotkey("^RCtrl", [this]() { io.Send("{forward}");
  //  }); io.Hotkey("#^+Rshift", [this]() { io.Send("{homepage}"); });     //
  //  #^+Rshift sends browser home
  io.Hotkey("#b", []() {
    Launcher::runShell("brave --new-window");
  }); // #b opens a new browser window
  io.Hotkey("#!b", []() {
    Launcher::runShell("brave --new-tab");
  }); // #!b opens a new browser tab
  io.Hotkey("#c", []() {
    Launcher::runShell("~/scripts/py/livecaptions.py");
  }); // #c runs /bin/livecaptions
  io.Hotkey("#!c", []() {
    Launcher::runShell("~/scripts/caption.sh 9 en");
  }); // #!c runs ~/scripts/caption.sh 9 en
  io.Hotkey("#^c", []() {
    Launcher::runShell("~/scripts/caption.sh 3 auto");
  }); // #^c runs ~/scripts/caption.sh 3 auto
  io.Hotkey("#+c", []() {
    Launcher::runShell("~/scripts/mimi.sh");
  }); // #+c runs ~/scripts/mimi.sh
  io.Hotkey("^!P", [this]() { io.Send("{CapsLock}"); });

  AddGamingHotkey(
      "u",
      [this]() {
        if (!holdClick) {
          io.Click(MouseButton::Left, MouseAction::Hold);
          holdClick = true;
        } else {
          io.Click(MouseButton::Left, MouseAction::Release);
          holdClick = false;
        }
      },
      nullptr, 0);

  io.Hotkey("~Button1", [this]() {
    info("Button1");
    mouse1Pressed = true;
  });
  io.Hotkey("~Button2", [this]() {
    info("Button2");
    mouse2Pressed = true;
  });

  // Mouse forward button to reset zoom
  io.Hotkey("@|*f13", [this]() { // When zooming
    try {
      std::string result = CompositorBridge::SendKWinZoomCommandWithOutput(
          "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.getZoomLevelDBus");
      if (!result.empty()) {
        zoomLevel = std::stod(result);
      }
    } catch (const std::exception &e) {
      warn("Failed to parse zoom level for @kc89: {}", e.what());
    }
    if (zoomLevel <= 1.0) {
      Zoom(3);
    } else {
      Zoom(2);
    }
  });

  io.Hotkey("@+numpad7", [this]() {
    info("Zoom 1");
    Zoom(1);
  });

  io.Hotkey("@+numpad1", [this]() {
    info("Zoom 0");
    Zoom(0);
  });

  io.Hotkey("@+numpad5", [this]() {
    info("Zoom 2");
    Zoom(2);
  });

  AddHotkey("^f1", "~/scripts/f1.sh -1");
  AddHotkey("+f1", "~/scripts/f1.sh 0");

  AddHotkey("!l", "~/scripts/livelink.sh");
  AddHotkey("+!l", "livelink screen toggle 1");
  AddHotkey("!f10", "~/scripts/str");
  AddHotkey("+!k", "livelink screen toggle 2");
  AddHotkey("^f10", "~/scripts/mpvv");
  AddHotkey("!^f", "~/scripts/freeze.sh thorium");
  AddHotkey("!f", [] {
    auto activePid = WindowManager::GetActiveWindowPID();
    // freeze process
    auto state = ProcessManager::getProcessState(static_cast<pid_t>(activePid));
    int sig =
        state == ProcessManager::ProcessState::RUNNING ? SIGSTOP : SIGCONT;
    ProcessManager::sendSignal(static_cast<pid_t>(activePid), sig);
  });
  AddHotkey("!^k", [] {
    auto activePid = WindowManager::GetActiveWindowPID();
    ProcessManager::sendSignal(static_cast<pid_t>(activePid), SIGKILL);
  });
  // Context-sensitive hotkeys
  AddHotkey(
      "@|kc89",
      [this]() { // When zooming
        try {
          std::string result = CompositorBridge::SendKWinZoomCommandWithOutput(
              "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.getZoomLevelDBus");
          if (!result.empty()) {
            zoomLevel = std::stod(result);
          }
        } catch (const std::exception &e) {
          warn("Failed to parse zoom level for @kc89: {}", e.what());
        }
        if (zoomLevel <= 1.0) {
          Zoom(3);
        } else {
          Zoom(2);
        }
      });
  AddContextualHotkey(
      "!x", "!(window.title ~ 'emacs' || window.title ~c 'alacritty')",
      [=]() {
        std::string terminal =
            Configs::Get().Get<std::string>("General.Terminal", "st");
        if (terminal == "alacritty") {
          Launcher::runShell("alacritty -e sh -c 'cd ~ && exec tmux'");
        } else {
          Launcher::runShell(terminal);
        }
      },
      nullptr, 0);

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

  const std::map<std::string, std::pair<std::string, std::function<void()>>>
      emergencyHotkeys = {
          {"#Esc",
           {"Restart application",
            []() {
#ifdef ENABLE_HAVEL_LANG
              auto *interp = HavelApp::instance->getInterpreter();
              if (!interp) {
                info("Restart application output: Interpreter not available");
                return;
              }
              auto out = interp->Execute("app.restart()");
              // Since HavelRuntimeError is not in HavelValue variant, we can't
              // check for it directly For now, just log the output
              info("Restart application output: {}", toString(out));
#else
                info("Restart application output: Havel Lang disabled");
#endif
            }}},
          {"^#esc", {"Reload configuration", [this]() {
                       info("Reloading configuration");
                       ReloadConfigurations();
                       debug("Configuration reload complete");
                     }}}};

  for (const auto &[key, hotkeyInfo] : emergencyHotkeys) {
    const auto &[description, action] = hotkeyInfo;
    io.Hotkey(key, [description, action]() {
      info("Executing emergency hotkey: " + description);
      action();
    });
  }

  // Brightness and temperature control
  io.Hotkey("f3", [this]() {
    info("Setting defaults");
    brightnessManager.setBrightness(
        Configs::Get().Get<double>("Display.DefaultBrightness", 1.0));
    brightnessManager.setShadowLift(
        Configs::Get().Get<double>("Display.DefaultShadowLift", 0.0));
    brightnessManager.setGammaRGB(
        Configs::Get().Get<double>("Display.DefaultGammaR", 1.0),
        Configs::Get().Get<double>("Display.DefaultGammaG", 1.0),
        Configs::Get().Get<double>("Display.DefaultGammaB", 1.0));
    brightnessManager.setTemperature(
        Configs::Get().Get<double>("Display.DefaultTemperature", 6500));
    info("Brightness set to: " + std::to_string(Configs::Get().Get<double>(
                                     "Display.DefaultBrightness", 1.0)));
    info("Temperature set to: " + std::to_string(Configs::Get().Get<double>(
                                      "Display.DefaultTemperature", 6500)));
  });
  io.Hotkey("+f3", [this]() {
    info("Setting default temperature");
    brightnessManager.setTemperature(
        Configs::Get().Get<double>("Temperature.Default", 6500));
    info("Temperature set to: " + std::to_string(Configs::Get().Get<double>(
                                      "Temperature.Default", 6500)));
  });
  io.Hotkey("^f3", [this]() {
    info("Setting default brightness");
    brightnessManager.setBrightness(
        1, Configs::Get().Get<double>("Brightness.Default", 1.0));
    info("Brightness set to: " +
         std::to_string(Configs::Get().Get<double>("Brightness.Default", 1.0)));
  });

  io.Hotkey("f7", [this]() {
    info("Decreasing brightness");
    brightnessManager.decreaseBrightness(0.05);
    info("Current brightness: " +
         std::to_string(brightnessManager.getBrightness()));
  });

  io.Hotkey("^f7", [this]() {
    info("Decreasing brightness");
    brightnessManager.decreaseBrightness(0, 0.05);
    info("Current brightness: " +
         std::to_string(brightnessManager.getBrightness(0)));
  });

  io.Hotkey("f8", [this]() {
    info("Increasing brightness");
    brightnessManager.increaseBrightness(0.05);
    info("Current brightness: " +
         std::to_string(brightnessManager.getBrightness()));
  });
  io.Hotkey("^f8", [this]() {
    info("Increasing brightness");
    brightnessManager.increaseBrightness(0, 0.05);
    info("Current brightness: " +
         std::to_string(brightnessManager.getBrightness(0)));
  });

  io.Hotkey("^!f8", [this]() {
    info("Increasing brightness");
    brightnessManager.increaseBrightness(1, 0.05);
    info("Current brightness: " +
         std::to_string(brightnessManager.getBrightness(1)));
  });
  io.Hotkey("^!f7", [this]() {
    info("Decreasing brightness");
    brightnessManager.decreaseBrightness(1, 0.05);
    info("Current brightness: " +
         std::to_string(brightnessManager.getBrightness(1)));
  });
  io.Hotkey("@!f7", [this]() {
    info("Decreasing shadow lift");
    auto shadowLift = brightnessManager.getShadowLift();
    brightnessManager.setShadowLift(shadowLift - 0.05);
    info("Current shadow lift: " +
         std::to_string(brightnessManager.getShadowLift()));
  });
  io.Hotkey("@!f8", [this]() {
    info("Increasing shadow lift");
    auto shadowLift = brightnessManager.getShadowLift();
    brightnessManager.setShadowLift(shadowLift + 0.05);
    info("Current shadow lift: " +
         std::to_string(brightnessManager.getShadowLift()));
  });
  io.Hotkey("@^f9", [this]() {
    info("Decreasing shadow lift");
    auto shadowLift = brightnessManager.getShadowLift();
    brightnessManager.setShadowLift(0, shadowLift - 0.05);
    info("Current shadow lift: " +
         std::to_string(brightnessManager.getShadowLift(0)));
  });
  io.Hotkey("@^f10", [this]() {
    info("Increasing shadow lift");
    auto shadowLift = brightnessManager.getShadowLift(0);
    brightnessManager.setShadowLift(0, shadowLift + 0.05);
    info("Current shadow lift: " +
         std::to_string(brightnessManager.getShadowLift(0)));
  });
  io.Hotkey("@^+f9", [this]() {
    info("Decreasing shadow lift");
    auto shadowLift = brightnessManager.getShadowLift(1);
    brightnessManager.setShadowLift(1, shadowLift - 0.05);
    info("Current shadow lift: " +
         std::to_string(brightnessManager.getShadowLift(1)));
  });
  io.Hotkey("@^+f10", [this]() {
    info("Increasing shadow lift");
    auto shadowLift = brightnessManager.getShadowLift(1);
    brightnessManager.setShadowLift(1, shadowLift + 0.05);
    info("Current shadow lift: " +
         std::to_string(brightnessManager.getShadowLift(1)));
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
    brightnessManager.decreaseGamma(0, 200);
  });
  io.Hotkey("@+f10", [this]() {
    info("Increasing gamma");
    brightnessManager.increaseGamma(0, 200);
  });
  io.Hotkey("@!+f10", [this]() {
    info("Increasing gamma");
    brightnessManager.increaseGamma(1, 200);
  });
  io.Hotkey("!+f9", [this]() {
    info("Decreasing gamma");
    brightnessManager.decreaseGamma(1, 200);
  });
  io.Hotkey("+f7", [this]() {
    info("Decreasing temperature");
    brightnessManager.decreaseTemperature(200);
    info("Current temperature: " +
         std::to_string(brightnessManager.getTemperature()));
  });
  io.Hotkey("^+f7", [this]() {
    info("Decreasing temperature");
    brightnessManager.decreaseTemperature(0, 200);
    info("Current temperature: " +
         std::to_string(brightnessManager.getTemperature(0)));
  });
  io.Hotkey("^!+f7", [this]() {
    info("Decreasing temperature");
    brightnessManager.decreaseTemperature(1, 200);
    info("Current temperature: " +
         std::to_string(brightnessManager.getTemperature(1)));
  });

  io.Hotkey("+f8", [this]() {
    info("Increasing temperature");
    brightnessManager.increaseTemperature(200);
    info("Current temperature: " +
         std::to_string(brightnessManager.getTemperature()));
  });
  io.Hotkey("^+f8", [this]() {
    info("Increasing temperature");
    brightnessManager.increaseTemperature(0, 200);
    info("Current temperature: " +
         std::to_string(brightnessManager.getTemperature(0)));
  });
  io.Hotkey("^!+f8", [this]() {
    info("Increasing temperature");
    brightnessManager.increaseTemperature(1, 200);
    info("Current temperature: " +
         std::to_string(brightnessManager.getTemperature(1)));
  });

  AddHotkey("~^Down", [this]() {
    // if(zoomLevel > 1.0) zoomLevel -= 0.1;
    try {
      std::string result = CompositorBridge::SendKWinZoomCommandWithOutput(
          "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.getZoomLevelDBus");
      if (!result.empty()) {
        zoomLevel = std::stod(result);
      }
    } catch (const std::exception &e) {
      // Handle the exception gracefully - use default zoom level or keep
      // current
      warn("Failed to parse zoom level for ~^Down: {}", e.what());
    }
  });
  AddHotkey("~^Up", [this]() {
    // if(zoomLevel < 2.0) zoomLevel += 0.1;
    try {
      std::string result = CompositorBridge::SendKWinZoomCommandWithOutput(
          "org.kde.KWin /Zoom org.kde.KWin.Effect.Zoom.getZoomLevelDBus");
      if (!result.empty()) {
        zoomLevel = std::stod(result);
      }
    } catch (const std::exception &e) {
      // Handle the exception gracefully - use default zoom level or keep
      // current
      warn("Failed to parse zoom level for ~^Up: {}", e.what());
    }
  });
  // Mouse wheel + click combinations
  io.Hotkey("@^#WheelUp", [this]() { io.Send("#{PgUp}"); });
  io.Hotkey("@^#WheelDown", [this]() { io.Send("#{PgDn}"); });
  /*io.Hotkey("@#!WheelUp", [this]() {
    io.Send("!{PgUp}");
  });
  io.Hotkey("@#!WheelDown", [this]() {
    io.Send("!{PgDn}");
  });*/
  io.Hotkey("@#+WheelDown", [this]() { io.Send("!9"); });
  io.Hotkey("@#+WheelUp", [this]() { io.Send("!0"); });

  /*io.Hotkey("@^!WheelDown", [this]() {
    if(!altTabPressed){
      io.Send("{LAlt:down}{Tab}");
    } else {
      io.Send("{Tab}");
    }
  });
  io.Hotkey("@^!WheelUp", [this]() {
    if(!altTabPressed){
      io.Send("+{LAlt:down}{Tab}");
    } else {
      io.Send("+{Tab}");
    }
  });*/
  AddHotkey("#k", "xkill");
  AddHotkey("#!2", "xprop");
  io.Hotkey("@^+WheelUp",
            [this]() { brightnessManager.increaseBrightness(0.05); });
  io.Hotkey("@^+WheelDown",
            [this]() { brightnessManager.decreaseBrightness(0.05); });
  io.Hotkey("@~!Tab", [this]() { altTabPressed = true; });
  io.Hotkey("@~LAlt:up", [this]() { altTabPressed = false; });
  io.Hotkey("@~RAlt:up", [this]() { altTabPressed = false; });
  io.Hotkey("@!WheelUp", [this]() {
    if (altTabPressed) {
      io.PressKey("LShift", true);
      io.PressKey("Tab", true);
      io.PressKey("Tab", false);
      io.PressKey("LShift", false);
    } else {
      Zoom(1);
    }
  });
  io.Hotkey("@#WheelUp", [this]() { Zoom(1); });
  io.Hotkey("@#WheelDown", [this]() { Zoom(0); });
  io.Hotkey("@!WheelDown", [this]() {
    if (altTabPressed) {
      io.PressKey("Tab", true);
      io.PressKey("Tab", false);
    } else {
      Zoom(0);
    }
  });
  io.Hotkey("@RShift & WheelUp", [this]() { Zoom(1); });
  io.Hotkey("RShift & WheelDown", [this]() { Zoom(0); });
  io.Hotkey("@LButton & RButton", [this]() { Zoom(2); });
  io.Hotkey("@RButton & LButton", [this]() { Zoom(1); });
  io.Hotkey("@RButton & WheelUp", [this]() { Zoom(1); });
  io.Hotkey("@RButton & WheelDown", [this]() { Zoom(0); });
  io.Hotkey("@~^l & g", []() { Launcher::runAsync("/usr/bin/lutris"); });
  io.Hotkey("@~^s & g", []() { Launcher::runAsync("/usr/bin/steam"); });
  io.Hotkey("@~^h & g", []() {
    Launcher::runAsync("flatpak run com.heroicgameslauncher.hgl");
  });
  io.Map("CapsLock", "LAlt");
  io.Hotkey("@+CapsLock", [this]() { io.Send("{CapsLock}"); });
  io.Hotkey("@^CapsLock", [this]() { io.Send("{CapsLock}"); });
  dpi = Configs::Get().Get<int>("Mouse.DPI", 400);
  io.Hotkey("@!-", [this]() {
    dpi -= 5;
    Launcher::runShell("~/scripts/dpi.sh " + std::to_string(dpi));
    info("Mouse DPI: {}", dpi); // ✅ Use format string
    Configs::Get().Set("Mouse.DPI", dpi);
  });

  io.Hotkey("@!=", [this]() {
    dpi += 5;
    Launcher::runShell("~/scripts/dpi.sh " + std::to_string(dpi));
    info("Mouse DPI: {}", dpi); // ✅ Use format string
    Configs::Get().Set("Mouse.DPI", dpi);
  });
  io.Hotkey("@!#-", [this]() {
    io.mouseSensitivity -= std::max(
        0.0, Configs::Get().Get<double>("Mouse.SensitivityIncrement", 0.02));
    if (io.mouseSensitivity < 0)
      io.mouseSensitivity = 0;
    Configs::Get().Set("Mouse.Sensitivity", io.mouseSensitivity);
    info("Mouse sensitivity: " + std::to_string(io.mouseSensitivity));
  });
  io.Hotkey("@!#=", [this]() {
    io.mouseSensitivity += std::min(
        1.0, Configs::Get().Get<double>("Mouse.SensitivityIncrement", 0.02));
    if (io.mouseSensitivity > 2.0)
      io.mouseSensitivity = 2.0;
    info("Mouse sensitivity: " + std::to_string(io.mouseSensitivity));
    Configs::Get().Set("Mouse.Sensitivity", io.mouseSensitivity);
  });
  AddHotkey("#a", []() { Launcher::runAsync("/bin/pavucontrol"); });
  // Emergency exit
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
    Window win(WindowManager::GetActiveWindow());
    Rect pos = win.Pos();
    WindowManager::MoveResize(win.ID(), pos.x + x, pos.y + y, pos.w + w,
                              pos.h + h);
  };
  AddHotkey("@!Home", [this]() {
    info("Toggle fullscreen");
    WindowManager::ToggleFullscreen(WindowManager::GetActiveWindow());
  });

  AddHotkey("@^!Home", [WinMove]() {
    info("Move to fullscreen on current monitor");
    auto win = Window(WindowManager::GetActiveWindow());
    auto rect = win.Pos();
    auto monitor = DisplayManager::GetMonitorAt(rect.x, rect.y);
    // Move to monitor position, not relative
    WindowManager::MoveResize(win.ID(), monitor.x, monitor.y, monitor.width,
                              monitor.height);
  });
  AddHotkey("!5", [this, WinMove]() {
    info("NP5 Move down");
    WinMove(0, winOffset, 0, 0);
  });

  AddHotkey("!6", [this, WinMove]() {
    info("NP8 Move up");
    WinMove(0, -winOffset, 0, 0);
  });

  AddHotkey("!7", [this, WinMove]() { WinMove(-winOffset, 0, 0, 0); });

  AddHotkey("!8", [this, WinMove]() { WinMove(winOffset, 0, 0, 0); });

  AddHotkey("!+5", [this, WinMove]() { WinMove(0, 0, 0, -winOffset); });

  AddHotkey("!+6", [this, WinMove]() { WinMove(0, 0, 0, winOffset); });

  AddHotkey("!+7", [this, WinMove]() { WinMove(0, 0, -winOffset, 0); });

  AddHotkey("!+8", [this, WinMove]() { WinMove(0, 0, winOffset, 0); });

  AddHotkey("@!3", []() {
    // Use QClipboard directly for better performance
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
      QString text = clipboard->text();
      // get first 20000 characters
      text = text.left(20000);
      clipboard->setText(text);
    }
  });
  AddHotkey("@!4", []() {
    // Use QClipboard directly for better performance
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
      QString text = clipboard->text();
      // get last 20000 characters
      text = text.right(20000);
      clipboard->setText(text);
    }
  });
  // Register screenshot hotkeys
  AddHotkey("@|#Print", "~/scripts/ocrs.sh");

  io.Hotkey("@|Print", [this]() { screenshotManager.takeScreenshot(); });

  io.Hotkey("@|+Print", [this]() { screenshotManager.takeRegionScreenshot(); });

  io.Hotkey("@|Pause",
            [this]() { screenshotManager.takeScreenshotOfCurrentMonitor(); });
  AddHotkey("@|numpad5",
            [this]() { io.Click(MouseButton::Left, MouseAction::Hold); });

  AddHotkey("@numpad5:up",
            [this]() { io.Click(MouseButton::Left, MouseAction::Release); });

  AddHotkey("@numpadmult",
            [this]() { io.Click(MouseButton::Right, MouseAction::Hold); });

  AddHotkey("@numpadmult:up",
            [this]() { io.Click(MouseButton::Right, MouseAction::Release); });

  AddHotkey("@numpadenter+",
            [this]() { io.Click(MouseButton::Middle, MouseAction::Hold); });

  AddHotkey("@numpadenter:up",
            [this]() { io.Click(MouseButton::Middle, MouseAction::Release); });

  AddHotkey("@numpad0", [this]() { io.Scroll(-2, 0); });

  AddHotkey("@numpaddec", [this]() { io.Scroll(2, 0); });
  AddHotkey("@!numpad0", [this]() { io.Scroll(1, 0); });

  AddHotkey("@+numpaddec", [this]() { io.Scroll(0, 1); });

  AddHotkey("@+numpad0", [this]() { io.Scroll(0, -1); });

  AddHotkey("@numpad1", [&]() { mouseController->move(-1, 1); });

  AddHotkey("@numpad2", [&]() { mouseController->move(0, 1); });

  AddHotkey("@numpad3", [&]() { mouseController->move(1, 1); });

  AddHotkey("@numpad4", [&]() { mouseController->move(-1, 0); });

  AddHotkey("@numpad6", [&]() { mouseController->move(1, 0); });

  AddHotkey("@numpad7", [&]() { mouseController->move(-1, -1); });

  AddHotkey("@numpad8", [&]() { mouseController->move(0, -1); });

  AddHotkey("@numpad9", [&]() { mouseController->move(1, -1); });

  AddHotkey("@numpad1:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad2:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad3:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad4:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad6:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad7:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad8:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("@numpad9:up", [&]() { mouseController->resetAcceleration(); });
  AddHotkey("!d", [this]() { toggleFakeDesktopOverlay(); });

  static bool keyDown = false;
  static bool registeredMouseKeys = false;
  AddGamingHotkey("@!m", [this]() {
    if (registeredMouseKeys) {
      return;
    }
    registeredMouseKeys = true;
    auto sendKey = [this](const std::string &key) {
      io.Send("{" + key + " down}");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      io.Send("{" + key + " up}");
    };
    AddGamingHotkey("@mouseleft", [this, sendKey]() { sendKey("f"); });
    AddGamingHotkey("@mouseright", [this, sendKey]() { sendKey("g"); });
    AddGamingHotkey("@mouseup", [this, sendKey]() { sendKey("i"); });
    AddGamingHotkey("@mousedown", [this, sendKey]() { sendKey("k"); });
    AddGamingHotkey("@LButton", [this, sendKey]() { sendKey("e"); });
    AddGamingHotkey("@RButton", [this, sendKey]() { sendKey("q"); });
  });
  AddGamingHotkey("@w:up", [this]() { keyDown = false; }, nullptr, 0);
  AddGamingHotkey(
      "@|y",
      [this]() {
        if (io.GetKeyState("w")) {
          io.Send("{w up}");
        } else {
          keyDown = !keyDown;

          if (keyDown) {
            io.Send("{w down}");
          } else {
            io.Send("{w up}");
          }
        }
      },
      nullptr, 0);

  AddGamingHotkey(
      "'",
      [this]() {
        info("Gaming hotkey: Moving mouse to 1600,700 and autoclicking");
        io.MouseMoveTo(1600, 700, 10, 1.0f);
        startAutoclicker("Button1");
      },
      nullptr, 0);

  AddHotkey("!delete", [this]() {
    info("Starting autoclicker");
    startAutoclicker("Button1");
  });

  static automation::AutoRunner genshinAutoRunner(
      std::shared_ptr<IO>(&io, [](IO *) {}));
  AddGamingHotkey(
      "/",
      []() {
        info("Genshin Impact detected - Starting specialized auto actions");
        genshinAutoRunner.toggle();
      },
      nullptr, 0);
  AddHotkey("^.", [this]() {
    std::string app = WindowManager::GetActiveWindowTitle();
    audioManager.increaseActiveApplicationVolume();
    double vol = audioManager.getActiveApplicationVolume();
    showNotification("App Volume",
                     std::to_string(static_cast<int>(vol * 100)) + "%");
    info("Volume for {}: {:.0f}%", app, vol * 100);
  });
  AddHotkey("^,", [this]() {
    std::string app = WindowManager::GetActiveWindowTitle();
    audioManager.decreaseActiveApplicationVolume();
    double vol = audioManager.getActiveApplicationVolume();
    showNotification("App Volume",
                     std::to_string(static_cast<int>(vol * 100)) + "%");
    info("Volume for {}: {:.0f}%", app, vol * 100);
  });
  AddContextualHotkey("Enter", "window.title ~ 'Chatterino'",
                      []() { info("Enter pressed in chatterino"); });
  AddContextualHotkey("h", "window.title ~ 'Genshin Impact'", [this]() {
    auto winId = WindowManager::GetActiveWindow();

    // ✅ CHECK STATE FIRST
    if (io.GetKeyState("lctrl")) {
      // Stop mode
      if (fTimer) {
        TimerManager::StopTimer(fTimer);
        fTimer = nullptr;
      }
      fRunning = false;
      info("Stopped F spamming");
      return; // 👈 EARLY EXIT
    }

    if (fRunning) {
      if (fTimer) {
        TimerManager::StopTimer(fTimer);
        fTimer = nullptr;
      }
      fRunning = false;
    }

    // Now start
    fRunning = true;
    fTimer = TimerManager::SetTimer(
        100,
        [this, winId]() {
          if (WindowManager::GetActiveWindow() != winId) {
            info("Window changed, stopping F timer");
            if (fTimer) {
              TimerManager::StopTimer(fTimer);
              fTimer = nullptr;
            }
            fRunning = false;
            return;
          }

          if (io.GetKeyState("lctrl")) {
            // Stop mode
            if (fTimer) {
              TimerManager::StopTimer(fTimer);
              fTimer = nullptr;
            }
            fRunning = false;
            info("Stopped F spamming");
            return; // 👈 EARLY EXIT
          }
          io.Send("f");
        },
        true);

    // Auto-stop after 15 seconds
    SetTimeout(15000, [this]() {
      if (fRunning && fTimer) {
        info("F timer auto-stopped after 15s");
        TimerManager::StopTimer(fTimer);
        fTimer = nullptr;
        fRunning = false;
      }
    });

    info("Started F spamming");
  });
  AddContextualHotkey("~space", "window.title ~ 'Genshin Impact'", [this]() {
    // ✅ PREVENT DOUBLE-START
    if (spaceTimer) {
      info("Space timer already running");
      return;
    }

    info("Space pressed - starting spam");
    io.Send("{space:up}");
    std::this_thread::sleep_for(std::chrono::milliseconds(
        100)); // 100ms delay to ensure the keyup is registered
    if (!io.GetKeyState("space")) {
      info("Space key is not physically held down, aborting spam");
      return;
    }
    io.DisableHotkey("~space");

    auto winId = WindowManager::GetActiveWindow();
    spaceTimer = TimerManager::SetTimer(
        100,
        [this, winId]() {
          if (WindowManager::GetActiveWindow() != winId) {
            info("Window changed, stopping space timer");
            if (spaceTimer) {
              TimerManager::StopTimer(spaceTimer);
              spaceTimer = nullptr;
            }
            // ✅ RE-ENABLE THE HOTKEY
            io.EnableHotkey("~space");
            return;
          }
          io.Send("{space}");
        },
        true);
  });

  AddContextualHotkey("~space:up", "window.title ~ 'Genshin Impact'", [this]() {
    info("Space released - stopping spam");
    if (spaceTimer) {
      TimerManager::StopTimer(spaceTimer);
      spaceTimer = nullptr;
    }
    io.Send("{space:up}");
    io.EnableHotkey("~space");
  });
  AddContextualHotkey(
      "enter", "window.title ~ 'Genshin Impact'",
      [this]() {
        // Toggle automation
        if (genshinAutomationActive) {
          automationManager_->stopAll();
          genshinAutomationActive = false;
          info("Stopped Genshin automation");
          return;
        }

        info("Starting Genshin automation");
        genshinAutomationActive = true;

        try {
          // Create and start fast auto-clicker (20ms = ~50 CPS)
          auto clicker = automationManager_->createAutoClicker("left", 60);
          clicker->start();
          info("Started fast auto-clicker");

          // Skill rotation
          std::vector<automation::AutomationManager::TimedAction>
              skillRotation = {
                  {[this]() { io.Send("e"); },
                   std::chrono::milliseconds(100)}, // Skill E
                  {[this]() { io.Send("q"); },
                   std::chrono::milliseconds(1000)} // Burst Q
              };

          // Start skill rotation (looping)
          automationManager_->createChainedTask("genshinSkills", skillRotation,
                                                true);
          info("Started skill rotation");

        } catch (const std::exception &e) {
          error("Failed to start Genshin automation: {}", e.what());
          automationManager_->stopAll();
          genshinAutomationActive = false;
        }
      },
      0);
  AddContextualHotkey(
      "+s", "window.title ~ 'Genshin Impact'",
      [this]() {
        info("Genshin Impact detected - Skipping cutscene");
        SetTimer(100, [this]() {
          Rect pos = {1600, 700};
          float speed = 100.0f;
          float accel = 5.0f;
          io.MouseClick(MouseButton::Left, pos.x, pos.y, speed, accel);
          io.Send("{enter}");
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          io.Send("f");
        });
      },
      nullptr, 0);

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

  // Register network hotkeys
  RegisterNetworkHotkeys();
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
      {"+0", [this]() { mpv.VolumeUp(); }, nullptr, mpvBaseId++},
      {"+9", [this]() { mpv.VolumeDown(); }, nullptr, mpvBaseId++},
      {"+-", [this]() { mpv.ToggleMute(); }, nullptr, mpvBaseId++},

      // Playback
      {"@|rctrl",
       [this]() {
         info("rctrl PlayPause");
         PlayPause();
       },
       nullptr, mpvBaseId++},
      {"+Esc", [this]() { mpv.Stop(); }, nullptr, mpvBaseId++},
      {"+PgUp", [this]() { mpv.Next(); }, nullptr, mpvBaseId++},
      {"+PgDn", [this]() { mpv.Previous(); }, nullptr, mpvBaseId++},
      // Seek
      {"o", [this]() { mpv.SendCommand({"seek", "-3"}); }, nullptr,
       mpvBaseId++},
      {"p", [this]() { mpv.SendCommand({"seek", "3"}); }, nullptr, mpvBaseId++},

      // Speed
      {"+o", [this]() { mpv.SendCommand({"add", "speed", "-0.1"}); }, nullptr,
       mpvBaseId++},
      {"+p", [this]() { mpv.SendCommand({"add", "speed", "0.1"}); }, nullptr,
       mpvBaseId++},

      // Subtitles
      {"|n", [this]() { mpv.SendCommand({"cycle", "sub-visibility"}); },
       nullptr, mpvBaseId++},
      {"+|n",
       [this]() { mpv.SendCommand({"cycle", "secondary-sub-visibility"}); },
       nullptr, mpvBaseId++},
      {"7", [this]() { mpv.SendCommand({"add", "sub-scale", "-0.1"}); },
       nullptr, mpvBaseId++},
      {"8", [this]() { mpv.SendCommand({"add", "sub-scale", "0.1"}); }, nullptr,
       mpvBaseId++},
      {"+z", [this]() { mpv.SendCommand({"add", "sub-delay", "-0.1"}); },
       nullptr, mpvBaseId++},
      {"+x", [this]() { mpv.SendCommand({"add", "sub-delay", "0.1"}); },
       nullptr, mpvBaseId++},
      {"9", [this]() { mpv.SendCommand({"cycle", "sub"}); }, nullptr,
       mpvBaseId++},
      {"0", [this]() { mpv.SendCommand({"sub-seek", "0"}); }, nullptr,
       mpvBaseId++},
      {"+c",
       [this]() {
         mpv.SendCommand({"script-binding", "copy_current_subtitle"});
       },
       nullptr, mpvBaseId++},
      {"minus", [this]() { mpv.SendCommand({"sub-seek", "-1"}); }, nullptr,
       mpvBaseId++},
      {"equal", [this]() { mpv.SendCommand({"sub-seek", "1"}); }, nullptr,
       mpvBaseId++},

      {"<",
       [this]() {
         logHotkeyEvent("KEYPRESS", COLOR_YELLOW + "Keycode 94" + COLOR_RESET);
         PlayPause();
       },
       nullptr, mpvBaseId++}};
  for (const auto &hk : mpvHotkeys) {
    AddGamingHotkey(hk.key, hk.trueAction, hk.falseAction, hk.id);
  }
}

void HotkeyManager::RegisterWindowHotkeys() {
  // Window movement
  io.Hotkey("^!Up", []() { WindowManager::MoveToCorners(1); });

  io.Hotkey("^!Down", []() { WindowManager::MoveToCorners(2); });

  io.Hotkey("^!Left", []() { WindowManager::MoveToCorners(3); });

  io.Hotkey("^!Right", []() { WindowManager::MoveToCorners(4); });

  // Window resizing
  io.Hotkey("+!Up", []() { WindowManager::ResizeToCorner(1); });

  io.Hotkey("!+Down", []() { WindowManager::ResizeToCorner(2); });

  io.Hotkey("!+Left", []() { WindowManager::ResizeToCorner(3); });

  io.Hotkey("!+Right", []() { WindowManager::ResizeToCorner(4); });

  // Window always on top
  io.Hotkey("!a", []() { WindowManager::ToggleAlwaysOnTop(); });
}

void HotkeyManager::registerAutomationHotkeys() {
  // Register hotkeys for automation tasks
  AddHotkey("!delete",
            [this]() { toggleAutomationTask("autoclicker", "left"); });
  AddGamingHotkey(
      "@rshift", [this]() { toggleAutomationTask("autokeypresser", "space"); });
}

void HotkeyManager::startAutoClicker(const std::string &button) {
  try {
    std::lock_guard<std::mutex> lock(automationMutex_);
    auto taskName = "autoclicker_" + button;

    if (automationTasks_.find(taskName) == automationTasks_.end()) {
      auto task = automationManager_->createAutoClicker(
          button, 100); // Default 100ms interval
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
  } catch (const std::exception &e) {
    error("Failed to start AutoClicker: " + std::string(e.what()));
  }
}

void HotkeyManager::startAutoRunner(const std::string &direction) {
  try {
    std::lock_guard<std::mutex> lock(automationMutex_);
    auto taskName = "autorunner_" + direction;

    if (automationTasks_.find(taskName) == automationTasks_.end()) {
      auto task = automationManager_->createAutoRunner(
          direction, 50); // Default 50ms interval
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
  } catch (const std::exception &e) {
    error("Failed to start AutoRunner: " + std::string(e.what()));
  }
}

void HotkeyManager::startAutoKeyPresser(const std::string &key) {
  try {
    std::lock_guard<std::mutex> lock(automationMutex_);
    auto taskName = "autokeypresser_" + key;

    if (automationTasks_.find(taskName) == automationTasks_.end()) {
      auto task = automationManager_->createAutoKeyPresser(
          key, 100); // Default 100ms interval
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
  } catch (const std::exception &e) {
    error("Failed to start AutoKeyPresser: " + std::string(e.what()));
  }
}

void HotkeyManager::stopAutomationTask(const std::string &taskType) {
  std::lock_guard<std::mutex> lock(automationMutex_);

  if (taskType.empty()) {
    // Stop all tasks if no specific type provided
    for (auto &[name, task] : automationTasks_) {
      if (task) {
        task->stop();
        info("Stopped automation task: " + name);
      }
    }
    return;
  }

  // Stop all tasks of the given type
  bool found = false;
  for (auto it = automationTasks_.begin(); it != automationTasks_.end();) {
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

void HotkeyManager::toggleAutomationTask(const std::string &taskType,
                                         const std::string &param) {
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
  io.Hotkey("#l", []() { Launcher::runShell("xdg-screensaver lock"); });

  io.Hotkey("+!Esc", []() { Launcher::runShell("gnome-system-monitor &"); });

  AddHotkey("#!d", [this]() {
    showBlackOverlay();
    logWindowEvent("BLACK_OVERLAY", "Showing full-screen black overlay");
  });

  AddHotkey("#2", [this]() { printActiveWindowInfo(); });

  AddHotkey("!+i", [this]() { toggleWindowFocusTracking(); });

  AddHotkey("^!d", [this]() {
    setVerboseConditionLogging(!verboseConditionLogging);
    Configs::Get().Set<bool>("Debug.VerboseConditionLogging",
                             verboseConditionLogging);
    Configs::Get().Save();

    std::string status = verboseConditionLogging ? "enabled" : "disabled";
    info("Verbose condition logging " + status);
    showNotification("Debug Setting", "Condition logging " + status);
  });
}

bool HotkeyManager::AddHotkey(const std::string &hotkeyStr,
                              std::function<void()> callback) {
  return io.Hotkey(hotkeyStr, [this, hotkeyStr, callback]() {
    logWindowEvent("ACTIVE", "Key pressed: " + hotkeyStr);
    callback();
  });
}

bool HotkeyManager::AddHotkey(const std::string &hotkeyStr,
                              const std::string &action) {
  return io.Hotkey(hotkeyStr,
                   [action]() { Launcher::runShellDetached(action.c_str()); });
}

bool HotkeyManager::RemoveHotkey(const std::string &hotkeyStr) {
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

void HotkeyManager::InvalidateConditionalHotkeys() {
  std::lock_guard<std::mutex> lock(hotkeyMutex);

  if (verboseConditionLogging) {
    debug("Invalidating all conditional hotkeys");
  }

  // Clear the condition cache to force re-evaluation with appropriate mutex
  {
    std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
    conditionCache.clear();
  }

  // Process all conditional hotkeys using batch update
  batchUpdateConditionalHotkeys();
}

void HotkeyManager::updateAllConditionalHotkeys() {
  auto now = std::chrono::steady_clock::now();

  // Only check conditions at the specified interval to reduce spam
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            lastConditionCheck)
          .count() < CONDITION_CHECK_INTERVAL_MS) {
    return;
  }
  lastConditionCheck = now;

  bool gamingWindowActive = isGamingWindow();
  std::string currentActiveMode;
  {
    std::lock_guard<std::mutex> lock(modeMutex);
    currentActiveMode = currentMode;
  }

  // Debounced mode switching - determine new mode outside hotkey processing
  bool shouldChangeMode = false;
  std::string newMode;
  auto lastSwitch = lastModeSwitch.load();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSwitch)
          .count() >= MODE_SWITCH_DEBOUNCE_MS) {
    if (gamingWindowActive && currentActiveMode != "gaming") {
      newMode = "gaming";
      shouldChangeMode = true;
    } else if (!gamingWindowActive && currentActiveMode != "default") {
      newMode = "default";
      shouldChangeMode = true;
    }
  }

  // If mode needs to change, change it first
  if (shouldChangeMode) {
    if (newMode == "gaming") {
      io.Map("Left", "a");
      io.Map("Right", "d");
      io.Map("Up", "w");
      io.Map("Down", "s");
    } else {
      io.Map("Left", "Left");
      io.Map("Right", "Right");
      io.Map("Up", "Up");
      io.Map("Down", "Down");
    }
    setMode(newMode);
    lastModeSwitch.store(now);
  }

  // Process all conditional hotkeys using batch update
  batchUpdateConditionalHotkeys();
}

void HotkeyManager::forceUpdateAllConditionalHotkeys() {
  bool gamingWindowActive = isGamingWindow();
  std::string currentActiveMode;
  {
    std::lock_guard<std::mutex> lock(modeMutex);
    currentActiveMode = currentMode;
  }

  // Debounced mode switching - determine new mode outside hotkey processing
  bool shouldChangeMode = false;
  std::string newMode;
  std::string logMessage;
  auto now = std::chrono::steady_clock::now();
  auto lastSwitch = lastModeSwitch.load();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSwitch)
          .count() >= MODE_SWITCH_DEBOUNCE_MS) {
    // Update mode if needed
    if (gamingWindowActive && currentActiveMode != "gaming") {
      newMode = "gaming";
      shouldChangeMode = true;
      logMessage = "ForceUpdate: Switched to gaming mode";
    } else if (!gamingWindowActive && currentActiveMode != "default") {
      newMode = "default";
      shouldChangeMode = true;
      logMessage = "ForceUpdate: Switched to default mode";
    }
  }

  // If mode needs to change, change it first
  if (shouldChangeMode) {
    if (newMode == "gaming") {
      io.Map("Left", "a");
      io.Map("Right", "d");
      io.Map("Up", "w");
      io.Map("Down", "s");
    } else {
      io.Map("Left", "Left");
      io.Map("Right", "Right");
      io.Map("Up", "Up");
      io.Map("Down", "Down");
    }
    setMode(newMode);
    lastModeSwitch.store(now);
    if (verboseConditionLogging) {
      info(logMessage);
    }
  }

  // Process all conditional hotkeys using batch update
  batchUpdateConditionalHotkeys();
}

void HotkeyManager::onActiveWindowChanged(wID newWindow) {
  if (verboseConditionLogging) {
    debug("🪟 Active window changed: {}", newWindow);
  }

  // Pause the update loop temporarily
  {
    std::lock_guard<std::mutex> lock(updateLoopMutex);
    updateLoopPaused.store(true);
  }

  // Force update
  forceUpdateAllConditionalHotkeys();

  // Resume update loop after a delay to avoid redundant checks using condition
  // variable
  std::thread([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
      std::lock_guard<std::mutex> lock(updateLoopMutex);
      updateLoopPaused.store(false);
    }
    updateLoopCv.notify_one(); // Wake up the update loop
  }).detach();
}

void HotkeyManager::updateConditionalHotkey(ConditionalHotkey &hotkey) {
  if (!conditionalHotkeysEnabled) {
    info("Conditional hotkeys are disabled");
    return;
  }
  if (verboseConditionLogging) {
    if (hotkey.usesFunctionCondition) {
      debug("Updating conditional hotkey - Key: '{}', Function Condition, "
            "CurrentlyGrabbed: {}",
            hotkey.key, hotkey.currentlyGrabbed);
    } else {
      debug("Updating conditional hotkey - Key: '{}', Condition: '{}', "
            "CurrentlyGrabbed: {}",
            hotkey.key, hotkey.condition, hotkey.currentlyGrabbed);
    }
  }

  bool conditionMet;
  auto now = std::chrono::steady_clock::now();

  if (hotkey.usesFunctionCondition) {
    // Use the function-based condition
    if (hotkey.conditionFunc) {
      conditionMet = hotkey.conditionFunc();
    } else {
      conditionMet = false; // Default to false if no function
    }
  } else {
    // Use the string-based condition (original behavior)
    {
      // Check cache first with condition cache mutex
      std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
      auto cacheIt = conditionCache.find(hotkey.condition);
      if (cacheIt != conditionCache.end()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - cacheIt->second.timestamp)
                       .count();
        if (age < CACHE_DURATION_MS) {
          conditionMet = cacheIt->second.result;

          // Only log when condition changes
          if (conditionMet != hotkey.lastConditionResult &&
              verboseConditionLogging) {
            info("Condition from cache: {} for {} ({}) - was:{} now:{}",
                 conditionMet ? 1 : 0, hotkey.condition, hotkey.key,
                 hotkey.lastConditionResult, conditionMet);
          }

          // Update hotkey state based on cached condition
          // We update lastConditionResult here since we're returning early
          hotkey.lastConditionResult = conditionMet;
          // Call updateHotkeyState separately and safely
          updateHotkeyState(hotkey, conditionMet);
          return;
        }
      }
    }

    // If we get here, either cache miss or cache expired
    conditionMet = evaluateCondition(hotkey.condition);

    // Update cache with the new result with condition cache mutex
    {
      std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
      conditionCache[hotkey.condition] = {conditionMet, now};
    }
  }

  // Only log when condition changes
  if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
    if (hotkey.usesFunctionCondition) {
      info("Function condition changed: {} for {} - was:{} now:{}",
           conditionMet ? 1 : 0, hotkey.key, hotkey.lastConditionResult,
           conditionMet);
    } else {
      info("Condition changed: {} for {} ({}) - was:{} now:{}",
           conditionMet ? 1 : 0, hotkey.condition, hotkey.key,
           hotkey.lastConditionResult, conditionMet);
    }
  }

  // Update hotkey state based on new condition
  // The hotkey.lastConditionResult will be updated inside updateHotkeyState
  updateHotkeyState(hotkey, conditionMet);
}

void HotkeyManager::updateHotkeyState(ConditionalHotkey &hotkey,
                                      bool conditionMet) {
  // Only update hotkey state if needed
  if (conditionMet && !hotkey.currentlyGrabbed) {
    io.GrabHotkey(hotkey.id);
    hotkey.currentlyGrabbed = true;
    if (verboseConditionLogging) {
      debug("Grabbed conditional hotkey: {} ({})", hotkey.key,
            hotkey.condition);
    }
  } else if (!conditionMet && hotkey.currentlyGrabbed) {
    io.UngrabHotkey(hotkey.id);
    hotkey.currentlyGrabbed = false;
    if (verboseConditionLogging) {
      debug("Ungrabbed conditional hotkey: {} ({})", hotkey.key,
            hotkey.condition);
    }
  }

  hotkey.lastConditionResult = conditionMet;
}

void HotkeyManager::batchUpdateConditionalHotkeys() {
  // Skip if we're in cleanup mode to prevent deadlocks
  if (inCleanupMode.load() || !conditionalHotkeysEnabled) {
    return;
  }

  std::vector<int> toGrab;
  std::vector<int> toUngrab;
  std::vector<ConditionalHotkey *> updatedHotkeys;

  // First pass: determine what needs to change and collect hotkeys to update
  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);

    // Process deferred updates first
    std::queue<int> localQueue;
    {
      std::lock_guard<std::mutex> queueLock(deferredUpdateMutex);
      std::swap(localQueue, deferredUpdateQueue);
    }

    while (!localQueue.empty()) {
      int id = localQueue.front();
      localQueue.pop();

      for (auto &ch : conditionalHotkeys) {
        if (ch.id == id) {
          updatedHotkeys.push_back(&ch);
          break;
        }
      }
    }

    // Process all conditional hotkeys that need evaluation
    for (auto &ch : conditionalHotkeys) {
      // If it's not in our update list and doesn't depend on mode, skip it
      bool needsUpdate = false;
      for (auto *hotkey : updatedHotkeys) {
        if (hotkey->id == ch.id) {
          needsUpdate = true;
          break;
        }
      }

      // Always add hotkeys that have "mode" in their condition to be updated
      if (!needsUpdate && ch.condition.find("mode") != std::string::npos) {
        needsUpdate = true;
        updatedHotkeys.push_back(&ch);
      }

      if (needsUpdate) {
        bool shouldGrab = evaluateCondition(ch.condition);

        if (shouldGrab && !ch.currentlyGrabbed) {
          toGrab.push_back(ch.id);
        } else if (!shouldGrab && ch.currentlyGrabbed) {
          toUngrab.push_back(ch.id);
        }
      }
    }
  }

  // Second pass: actually grab/ungrab (without holding hotkey lock)
  for (int id : toUngrab) {
    io.UngrabHotkey(id);
  }

  for (int id : toGrab) {
    io.GrabHotkey(id);
  }

  // Third pass: update state (with minimal locking)
  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);

    for (int id : toUngrab) {
      for (auto &ch : conditionalHotkeys) {
        if (ch.id == id) {
          ch.currentlyGrabbed = false;
          ch.lastConditionResult = false;
          break;
        }
      }
    }

    for (int id : toGrab) {
      for (auto &ch : conditionalHotkeys) {
        if (ch.id == id) {
          ch.currentlyGrabbed = true;
          ch.lastConditionResult = true;
          break;
        }
      }
    }
  }

  if (!toGrab.empty() || !toUngrab.empty()) {
    if (verboseConditionLogging) {
      debug("Batch hotkey update: grabbed={}, ungrabbed={}", toGrab.size(),
            toUngrab.size());
    } else {
      info("Batch hotkey update: grabbed={}, ungrabbed={}", toGrab.size(),
           toUngrab.size());
    }
  }
}

ConditionalHotkey *HotkeyManager::findConditionalHotkey(int id) {
  for (auto &ch : conditionalHotkeys) {
    if (ch.id == id) {
      return &ch;
    }
  }
  return nullptr;
}

int HotkeyManager::AddGamingHotkey(const std::string &key,
                                   std::function<void()> trueAction,
                                   std::function<void()> falseAction, int id) {
  int gamingHotkeyId =
      AddContextualHotkey(key, "mode == 'gaming'", trueAction, falseAction, id);
  gamingHotkeyIds.push_back(gamingHotkeyId);
  return gamingHotkeyId;
}
int HotkeyManager::AddContextualHotkey(const std::string &key,
                                       const std::string &condition,
                                       std::function<void()> trueAction,
                                       std::function<void()> falseAction,
                                       int id) {
  debug("Registering contextual hotkey - Key: '{}', Condition: '{}', ID: {}",
        key, condition, id);
  if (id == 0) {
    static int nextId = 1000;
    id = nextId++;
  }

  auto action = [this, condition, trueAction, falseAction]() {
    if (evaluateCondition(condition)) {
      if (trueAction)
        trueAction();
    } else {
      if (falseAction)
        falseAction();
    }
  };

  // Store the conditional hotkey for dynamic management
  ConditionalHotkey ch;
  ch.id = id;
  ch.key = key;
  ch.condition = condition;
  ch.conditionFunc =
      nullptr; // No function condition for string-based condition
  ch.trueAction = trueAction;
  ch.falseAction = falseAction;
  ch.currentlyGrabbed = true;
  ch.usesFunctionCondition = false;

  conditionalHotkeys.push_back(ch);
  conditionalHotkeyIds.push_back(id);
  // Register but don't grab yet
  io.Hotkey(key, action, condition, id);

  // Initial evaluation and grab if needed
  updateConditionalHotkey(conditionalHotkeys.back());

  return id;
}

int HotkeyManager::AddContextualHotkey(const std::string &key,
                                       std::function<bool()> condition,
                                       std::function<void()> trueAction,
                                       std::function<void()> falseAction,
                                       int id) {
  debug("Registering contextual hotkey - Key: '{}', Lambda Condition, ID: {}",
        key, id);
  if (id == 0) {
    static int nextId = 1000;
    id = nextId++;
  }

  auto action = [condition, trueAction, falseAction]() {
    if (condition()) {
      if (trueAction)
        trueAction();
    } else {
      if (falseAction)
        falseAction();
    }
  };

  // Store the conditional hotkey for dynamic management
  ConditionalHotkey ch;
  ch.id = id;
  ch.key = key;
  ch.condition = "";            // No string condition, using function
  ch.conditionFunc = condition; // Store the condition function
  ch.trueAction = trueAction;
  ch.falseAction = falseAction;
  ch.currentlyGrabbed = true;
  ch.usesFunctionCondition = true; // Mark as function-based condition

  conditionalHotkeys.push_back(ch);
  conditionalHotkeyIds.push_back(id);
  // Register but don't grab yet
  io.Hotkey(key, action, "<function>", id);

  // For function-based conditions, we need to update them separately
  // The conditional hotkey update logic will handle grabbing/ungrabbing
  updateConditionalHotkey(conditionalHotkeys.back());

  return id;
}

void HotkeyManager::setupConditionEngine() {
  conditionEngine->registerProperty(
      "window.title", PropertyType::STRING,
      []() -> std::string { return WindowManager::GetActiveWindowTitle(); });

  conditionEngine->registerProperty(
      "window.class", PropertyType::STRING,
      []() -> std::string { return WindowManager::GetActiveWindowClass(); });

  conditionEngine->registerProperty(
      "window.pid", PropertyType::INTEGER, []() -> std::string {
        return std::to_string(WindowManager::GetActiveWindowPID());
      });

  conditionEngine->registerProperty(
      "mode", PropertyType::STRING, []() -> std::string {
        std::lock_guard<std::mutex> lock(modeMutex);
        return currentMode;
      });

  conditionEngine->registerBoolProperty(
      "gaming.active", []() -> bool { return isGamingWindow(); });
  conditionEngine->registerProperty("time.hour", PropertyType::INTEGER,
                                    []() -> std::string {
                                      auto now = std::time(nullptr);
                                      auto *tm = std::localtime(&now);
                                      return std::to_string(tm->tm_hour);
                                    });

  conditionEngine->registerListProperty("gaming.apps",
                                        []() -> std::vector<std::string> {
                                          return Configs::Get().GetGamingApps();
                                        });
}

bool HotkeyManager::evaluateCondition(const std::string &condition) {
  if (!conditionEngine) {
    if (verboseConditionLogging) {
      logWindowEvent("CONDITION_EVAL_ERROR",
                     "Condition engine not initialized");
    }
    return false;
  }

  bool result;
  {
    conditionEngine->invalidateCache();
    result = conditionEngine->evaluateCondition(condition);
  }

  if (verboseConditionLogging) {
    logWindowEvent("CONDITION_EVAL",
                   condition + " -> " + (result ? "TRUE" : "FALSE"));
  }
  return result;
}
void HotkeyManager::showNotification(const std::string &title,
                                     const std::string &message) {
  std::string cmd = "notify-send \"" + title + "\" \"" + message + "\"";
  Launcher::runShell(cmd.c_str());
}

bool HotkeyManager::IsGamingProcess(pid_t pid) {
  // Check for Steam game ID
  std::string steamGameId = ProcessManager::getProcessEnvironment(
      static_cast<int32_t>(pid), "SteamGameId");
  if (!steamGameId.empty()) {
    debug("Active process is Steam game: {}", steamGameId);
    return true;
  }

  // Check process name/path
  std::string exe =
      ProcessManager::getProcessExecutablePath(static_cast<int32_t>(pid));
  std::string name = ProcessManager::getProcessName(static_cast<int32_t>(pid));

  // Gaming process whitelist
  static const std::vector<std::string> gamingProcesses = {
      "steam_app_", // Steam games
      "wine",       // Wine games
      "proton",     // Proton games
      "lutris",     // Lutris
      "gamemode",   // GameMode wrapper
      "minecraft",  "factorio",
      "java", // Many games use Java (check carefully)
  };

  for (const auto &pattern : gamingProcesses) {
    if (exe.find(pattern) != std::string::npos ||
        name.find(pattern) != std::string::npos) {
      debug("Gaming process detected: {} (exe: {})", name, exe);
      return true;
    }
  }

  return false;
}

bool HotkeyManager::isGamingWindow() {
  // Method 1: Compositor bridge (Wayland with KWin/wlroots)
  auto *bridge = WindowManager::GetCompositorBridge();
  if (bridge && bridge->IsAvailable()) {
    auto windowInfo = bridge->GetActiveWindow();
    if (windowInfo.valid) {
      // Check window title
      std::string title = ToLower(windowInfo.title);
      static const std::vector<std::string> gameTitlePatterns = {
          "steam",    "game",     "dota",     "counter-strike", "minecraft",
          "factorio", "terraria", "rimworld", "stardew"};

      for (const auto &pattern : gameTitlePatterns) {
        if (title.find(pattern) != std::string::npos) {
          debug("Gaming window detected via title: {}", windowInfo.title);
          return true;
        }
      }

      // Check app ID
      std::string appId = ToLower(windowInfo.appId);
      static const std::vector<std::string> gameAppIdPatterns = {
          "steam_app", "lutris", "wine", "proton"};

      for (const auto &pattern : gameAppIdPatterns) {
        if (appId.find(pattern) != std::string::npos) {
          debug("Gaming window detected via appId: {}", windowInfo.appId);
          return true;
        }
      }

      // Fall through to PID check with compositor-provided PID
      if (windowInfo.pid != 0) {
        if (HotkeyManager::IsGamingProcess(windowInfo.pid)) {
          return true;
        }
      }
    }
  }

  // Method 2: X11 window class/title detection fallback
  std::string windowClass = WindowManager::GetActiveWindowClass();
  std::string windowTitle = WindowManager::GetActiveWindowTitle();

  std::transform(windowClass.begin(), windowClass.end(), windowClass.begin(),
                 ::tolower);
  std::transform(windowTitle.begin(), windowTitle.end(), windowTitle.begin(),
                 ::tolower);

  // Check for excluded classes first
  const std::vector<std::string> gamingAppsExclude =
      Configs::Get().GetGamingAppsExclude();
  for (const auto &app : gamingAppsExclude) {
    if (windowClass.find(app) != std::string::npos) {
      return false; // Explicitly excluded by class
    }
  }

  // Check for excluded titles
  const std::vector<std::string> gamingAppsExcludeTitle =
      Configs::Get().GetGamingAppsExcludeTitle();
  for (const auto &app : gamingAppsExcludeTitle) {
    if (windowTitle.find(app) != std::string::npos) {
      return false; // Explicitly excluded by title
    }
  }

  // Check for specifically included titles
  const std::vector<std::string> gamingAppsTitle =
      Configs::Get().GetGamingAppsTitle();
  for (const auto &app : gamingAppsTitle) {
    if (windowTitle.find(app) != std::string::npos) {
      return true; // Explicitly included by title
    }
  }

  // Finally check for regular gaming apps by class
  const std::vector<std::string> gamingApps = Configs::Get().GetGamingApps();
  for (const auto &app : gamingApps) {
    if (windowClass.find(app) != std::string::npos) {
      return true;
    }
  }

  // Method 3: PID-based gaming process detection (universal fallback)
  pid_t activePid = WindowManager::GetActiveWindowPID();
  if (activePid != 0) {
    return HotkeyManager::IsGamingProcess(activePid);
  }

  return false;
}
void HotkeyManager::startAutoclicker(const std::string &button) {
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
    info("Starting autoclicker (" + button +
         ") in window: " + std::to_string(autoclickerWindowID));

    autoClicker = std::make_unique<automation::AutoClicker>(
        std::shared_ptr<IO>(&io, [](IO *) {}));

    // Set the appropriate click type based on button
    if (button == "Button1" || button == "Left") {
      autoClicker->setClickType(automation::AutoClicker::ClickType::Left);
    } else if (button == "Button2" || button == "Right") {
      autoClicker->setClickType(automation::AutoClicker::ClickType::Right);
    } else if (button == "Button3" || button == "Middle") {
      autoClicker->setClickType(automation::AutoClicker::ClickType::Middle);
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

  } catch (const std::exception &e) {
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

std::string HotkeyManager::handleKeycode(const std::string &input) {
  if (input.size() < 2 || input[0] != 'k' || input[1] != 'e') {
    return input;
  }

  std::string numStr = input.substr(2);
  try {
    int keycode = std::stoi(numStr);
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
      error("Failed to open X display for keycode conversion");
      return input;
    }

    KeySym keysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
    char *keyName = XKeysymToString(keysym);
    XCloseDisplay(display);

    if (keyName) {
      std::string result(keyName);
      return result;
    }
  } catch (const std::exception &e) {
    error("Failed to convert keycode: " + input + " - " + e.what());
  }
  return input;
}

std::string HotkeyManager::handleScancode(const std::string &input) {
  std::string numStr = input.substr(2);
  try {
    int scancode = std::stoi(numStr);
    int keycode = scancode + 8; // Common offset on Linux
    return handleKeycode("kc" + std::to_string(keycode));
  } catch (const std::exception &e) {
    error("Failed to convert scancode: " + input + " - " + e.what());
  }
  return input;
}

std::string HotkeyManager::normalizeKeyName(const std::string &keyName) {
  std::string normalized = keyName;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 ::tolower);
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

std::string HotkeyManager::convertKeyName(const std::string &keyName) {
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

  result = normalizeKeyName(keyName);
  if (result != keyName) {
    logKeyConversion(keyName, result);
  }
  return result;
}

std::string HotkeyManager::parseHotkeyString(const std::string &hotkeyStr) {
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
    if (i > 0)
      result += "+";
    result += parts[i];
  }

  return result;
}

void HotkeyManager::logHotkeyEvent(const std::string &eventType,
                                   const std::string &details) {
  std::string timestamp =
      "[" + COLOR_DIM + std::to_string(time(nullptr)) + COLOR_RESET + "]";
  std::string type =
      COLOR_BOLD + COLOR_CYAN + "[" + eventType + "]" + COLOR_RESET;
  info(timestamp + " " + type + " " + details);
}

void HotkeyManager::logKeyConversion(const std::string &from,
                                     const std::string &to) {
  std::string arrow = COLOR_BOLD + COLOR_BLUE + " → " + COLOR_RESET;
  std::string fromStr = COLOR_YELLOW + from + COLOR_RESET;
  std::string toStr = COLOR_GREEN + to + COLOR_RESET;
  logHotkeyEvent("KEY_CONVERT", fromStr + arrow + toStr);
}

void HotkeyManager::logModeSwitch(const std::string &from,
                                  const std::string &to) {
  std::string arrow = COLOR_BOLD + COLOR_MAGENTA + " → " + COLOR_RESET;
  std::string fromStr = COLOR_YELLOW + from + COLOR_RESET;
  std::string toStr = COLOR_GREEN + to + COLOR_RESET;
  logHotkeyEvent("MODE_SWITCH", fromStr + arrow + toStr);
}

void HotkeyManager::logKeyEvent(const std::string &key,
                                const std::string &eventType,
                                const std::string &details) {
  if (!verboseKeyLogging)
    return;

  std::string timestamp =
      "[" + COLOR_DIM + std::to_string(time(nullptr)) + COLOR_RESET + "]";
  std::string type =
      COLOR_BOLD + COLOR_CYAN + "[KEY_" + eventType + "]" + COLOR_RESET;
  std::string keyInfo = COLOR_YELLOW + key + COLOR_RESET;
  std::string detailInfo =
      details.empty() ? "" : " (" + COLOR_GREEN + details + COLOR_RESET + ")";

  info(timestamp + " " + type + " " + keyInfo + detailInfo);
}

void HotkeyManager::logWindowEvent(const std::string &eventType,
                                   const std::string &details) {
  if (!verboseWindowLogging)
    return;

  std::string timestamp =
      "[" + COLOR_DIM + std::to_string(time(nullptr)) + COLOR_RESET + "]";
  std::string type =
      COLOR_BOLD + COLOR_MAGENTA + "[WINDOW_" + eventType + "]" + COLOR_RESET;
  wID activeWindow = WindowManager::GetActiveWindow();
  std::string windowClass = WindowManager::GetActiveWindowClass();
  std::string windowTitle;
  try {
    Window window(std::to_string(activeWindow), activeWindow);
    windowTitle = window.Title();
  } catch (...) {
    windowTitle = "<error getting title>";
  }

  std::string windowInfo = COLOR_BOLD + COLOR_CYAN + "Class: " + COLOR_RESET +
                           windowClass + COLOR_BOLD + COLOR_CYAN +
                           " | Title: " + COLOR_RESET + windowTitle +
                           COLOR_BOLD + COLOR_CYAN + " | ID: " + COLOR_RESET +
                           std::to_string(activeWindow);

  std::string detailInfo =
      details.empty() ? "" : " (" + COLOR_GREEN + details + COLOR_RESET + ")";

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
         COLOR_BOLD + COLOR_CYAN + " | ID: " + COLOR_RESET +
         std::to_string(windowId);
}

bool HotkeyManager::isVideoSiteActive() {
  wID windowId = WindowManager::GetActiveWindow();
  if (windowId == 0)
    return false;

  Window window(std::to_string(windowId), windowId);
  std::string windowTitle = window.Title();
  std::transform(windowTitle.begin(), windowTitle.end(), windowTitle.begin(),
                 ::tolower);

  for (const auto &site : videoSites) {
    if (windowTitle.find(site) != std::string::npos) {
      if (verboseWindowLogging) {
        logWindowEvent("VIDEO_SITE", "Detected video site: " + site);
      }
      return true;
    }
  }
  return false;
}

void HotkeyManager::handleMediaCommand(
    const std::vector<std::string> &mpvCommand) {
  updateVideoPlaybackStatus();

  if (isVideoSiteActive() && videoPlaying) {
    if (verboseWindowLogging) {
      logWindowEvent("MEDIA_CONTROL", "Using media keys for web video");
    }

    if (!mpvCommand.empty()) {
      const std::string &cmd = mpvCommand[0];
      if (cmd == "cycle" || cmd == "pause") {
        Launcher::runShell("playerctl play-pause");
      } else if (cmd == "seek" && mpvCommand.size() > 1) {
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
      for (const auto &arg : mpvCommand) {
        commandStr += arg + " ";
      }
      logWindowEvent("MEDIA_CONTROL", "Sending MPV command: " + commandStr);
    }

    std::vector<std::string> commandToSend = mpvCommand;
    mpv.SendCommand(commandToSend);
  }
}
void HotkeyManager::setMode(const std::string &newMode) {
  // Check cleanup mode first to prevent deadlocks during shutdown
  if (inCleanupMode.load()) {
    // Just set the mode directly during cleanup to avoid complex operations
    {
      std::lock_guard<std::mutex> lock(modeMutex);
      if (currentMode == newMode) {
        return;
      }
      currentMode = newMode;
    }
    return;
  }

  std::string oldMode;
  bool modeChanged = false;

  // Check mode change without holding lock
  {
    std::lock_guard<std::mutex> lock(modeMutex);
    if (currentMode == newMode)
      return;

    oldMode = currentMode;
    currentMode = newMode;
    modeChanged = true;
  }

  if (!modeChanged)
    return;

  info("Mode changing: {} → {}", oldMode, newMode);

  logModeSwitch(oldMode, newMode);

  // Invalidate condition cache for mode-dependent hotkeys
  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);

    // Only update hotkeys that actually depend on mode
    for (auto &ch : conditionalHotkeys) {
      if (ch.condition.find("mode") != std::string::npos) {
        // Add to deferred update queue instead of immediate update
        std::lock_guard<std::mutex> queueLock(deferredUpdateMutex);
        deferredUpdateQueue.push(ch.id);
      }
    }
  }

  // Clear caches separately to avoid locking both mutexes at once
  {
    std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
    conditionCache.clear();
  }

  if (conditionEngine) {
    conditionEngine->invalidateCache();
  }

  if (verboseConditionLogging) {
    debug("Mode changed: {} → {} - Queued deferred updates", oldMode, newMode);
  }

  // Process deferred updates in batch
  batchUpdateConditionalHotkeys();

  info("Mode changed: {} → {}", currentMode, newMode);
}
void HotkeyManager::printCacheStats() {
  size_t cacheSize;
  std::vector<std::pair<std::string, CachedCondition>> cacheCopy;

  // Get a copy of the cache with proper locking to avoid deadlocks
  {
    std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
    cacheSize = conditionCache.size();
    cacheCopy.reserve(conditionCache.size());
    for (const auto &[condition, cache] : conditionCache) {
      cacheCopy.push_back({condition, cache});
    }
  }

  info("Condition cache: {} entries", cacheSize);

  if (cacheCopy.empty()) {
    return;
  }

  auto now = std::chrono::steady_clock::now();
  int expired = 0;

  for (const auto &[condition, cache] : cacheCopy) {
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now - cache.timestamp)
                   .count();
    if (age >= CACHE_DURATION_MS) {
      expired++;
    }

    if (verboseConditionLogging) {
      debug("  - '{}': {} ({}ms old)", condition,
            cache.result ? "true" : "false", age);
    }
  }

  info("Cache stats: {} fresh, {} expired ({}% hit rate)", cacheSize - expired,
       expired, cacheSize > 0 ? (100 * (cacheSize - expired) / cacheSize) : 0);
}

void HotkeyManager::toggleFakeDesktopOverlay() {
  info("Toggling fake desktop overlay");

  // Create a new QMainWindow for the overlay
  static QMainWindow *fakeDesktopOverlay = nullptr;

  // If overlay already exists, close and delete it
  if (fakeDesktopOverlay) {
    // Restore audio if it was muted by this overlay
    if (audioManager.isMuted()) {
      audioManager.setMute(false);
      info("Audio unmuted");
    }

    fakeDesktopOverlay->close();
    delete fakeDesktopOverlay;
    fakeDesktopOverlay = nullptr;
    info("Fake desktop overlay hidden");
    return;
  }

  // Mute audio when showing the overlay
  if (!audioManager.isMuted()) {
    audioManager.setMute(true);
    info("Audio muted");
  }

  // Create a new overlay window
  fakeDesktopOverlay = new QMainWindow();

  // Set window properties for overlay
  fakeDesktopOverlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                                     Qt::WindowStaysOnTopHint |
                                     Qt::X11BypassWindowManagerHint);

  // Make the window transparent for input
  fakeDesktopOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  // Set background to look like a fake desktop
  QWidget *container = new QWidget();
  QVBoxLayout *layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Create a custom widget for the animated background
  class AnimatedBackground : public QWidget {
  public:
    AnimatedBackground(QWidget *parent = nullptr) : QWidget(parent) {
      // Set up animation timer
      connect(&timer, &QTimer::timeout, this, [this]() {
        // Update gradient colors
        colorHue = fmod(colorHue + 0.001f, 1.0f);

        // Update floating curves
        for (auto &curve : curves) {
          curve.x += curve.speedX;
          curve.y += curve.speedY;

          // Bounce off edges
          if (curve.x < 0 || curve.x > 1)
            curve.speedX *= -1;
          if (curve.y < 0 || curve.y > 1)
            curve.speedY *= -1;

          // Random direction changes
          if (rand() % 100 < 1) {
            curve.speedX = -0.5f + static_cast<float>(rand()) /
                                       static_cast<float>(RAND_MAX);
            curve.speedY = -0.5f + static_cast<float>(rand()) /
                                       static_cast<float>(RAND_MAX);

            // Normalize speed
            float len =
                sqrt(curve.speedX * curve.speedX + curve.speedY * curve.speedY);
            curve.speedX = curve.speedX / len * 0.001f;
            curve.speedY = curve.speedY / len * 0.001f;
          }
        }

        update();
      });
      timer.start(16); // ~60 FPS

      // Initialize random curves
      srand(static_cast<unsigned>(time(nullptr)));
      for (int i = 0; i < 8; ++i) {
        Curve curve;
        curve.x = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        curve.y = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        curve.radius = 50 + rand() % 150;
        curve.hue = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        curve.speedX =
            -0.5f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        curve.speedY =
            -0.5f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

        // Normalize speed
        float len =
            sqrt(curve.speedX * curve.speedX + curve.speedY * curve.speedY);
        curve.speedX = curve.speedX / len * 0.001f;
        curve.speedY = curve.speedY / len * 0.001f;

        curves.push_back(curve);
      }
    }

  protected:
    void paintEvent(::QPaintEvent *event) override {
      QPainter painter(this);
      painter.setRenderHint(QPainter::Antialiasing);

      // Draw animated gradient background
      QLinearGradient gradient(0, 0, width(), height());
      QColor color1 = QColor::fromHslF(colorHue, 0.7, 0.15);
      QColor color2 = QColor::fromHslF(fmod(colorHue + 0.3, 1.0), 0.7, 0.2);
      gradient.setColorAt(0, color1);
      gradient.setColorAt(1, color2);

      painter.fillRect(rect(), gradient);

      // Draw floating curves
      for (const auto &curve : curves) {
        int x = static_cast<int>(curve.x * width());
        int y = static_cast<int>(curve.y * height());
        int r = curve.radius;

        // Create a radial gradient for the curve
        QRadialGradient radialGrad(x, y, r);
        QColor curveColor = QColor::fromHslF(curve.hue, 0.6, 0.5, 0.1);
        radialGrad.setColorAt(0, curveColor);
        radialGrad.setColorAt(1, Qt::transparent);

        painter.setPen(Qt::NoPen);
        painter.setBrush(radialGrad);
        painter.drawEllipse(QPoint(x, y), r, r);
      }
    }

  private:
    struct Curve {
      float x, y;
      float radius;
      float hue;
      float speedX, speedY;
    };

    std::vector<Curve> curves;
    float colorHue = 0.0f;
    QTimer timer{this};
  };

  // Create the animated background
  AnimatedBackground *desktopBackground = new AnimatedBackground(container);

  // Create a container for the clock and calendar
  QWidget *infoContainer = new QWidget(container);
  QVBoxLayout *infoLayout = new QVBoxLayout(infoContainer);
  infoLayout->setContentsMargins(20, 20, 20, 20);
  infoLayout->setSpacing(10);
  infoLayout->setAlignment(Qt::AlignCenter);

  // Add clock
  QLabel *clockLabel = new QLabel(container);
  clockLabel->setStyleSheet(
      "color: white; font-size: 48px; font-weight: bold;");
  clockLabel->setAlignment(Qt::AlignCenter);

  // Add date display
  QLabel *dateLabel = new QLabel(container);
  dateLabel->setStyleSheet("color: #aaa; font-size: 24px;");
  dateLabel->setAlignment(Qt::AlignCenter);

  // Add calendar
  QLabel *calendarLabel = new QLabel(container);
  calendarLabel->setStyleSheet(
      "color: #ccc; font-family: monospace; font-size: 16px;");
  calendarLabel->setAlignment(Qt::AlignCenter);

  // Add focus mode message
  QLabel *message =
      new QLabel("<p style='color: #888; margin-top: 30px;'>Press the hotkey "
                 "again to exit focus mode</p>",
                 container);
  message->setAlignment(Qt::AlignCenter);

  // Add widgets to layout
  infoLayout->addWidget(clockLabel);
  infoLayout->addWidget(dateLabel);
  infoLayout->addSpacing(30);
  infoLayout->addWidget(calendarLabel);
  infoLayout->addWidget(message);

  // Add container to main layout
  QWidget *centerWidget = new QWidget(container);
  QVBoxLayout *centerLayout = new QVBoxLayout(centerWidget);
  centerLayout->addWidget(infoContainer);
  centerLayout->setAlignment(Qt::AlignCenter);

  // Add to main layout
  layout->addWidget(desktopBackground, 1);
  layout->addWidget(centerWidget);

  // Timer to update clock and calendar
  QTimer *timer = new QTimer(container);
  QObject::connect(timer, &QTimer::timeout, [=]() {
    // Update clock
    QTime currentTime = QTime::currentTime();
    clockLabel->setText(currentTime.toString("hh:mm:ss"));

    // Update date
    QDate currentDate = QDate::currentDate();
    dateLabel->setText(currentDate.toString("dddd, MMMM d, yyyy"));

    // Update calendar
    int year = currentDate.year();
    int month = currentDate.month();
    int day = currentDate.day();

    // Generate calendar
    QDate firstDayOfMonth(year, month, 1);
    int daysInMonth = firstDayOfMonth.daysInMonth();
    int firstDayOfWeek = firstDayOfMonth.dayOfWeek();

    QStringList days = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    QStringList calendarLines;
    calendarLines.append(" " + days.join(" ") + " ");

    QString week;
    // Add leading spaces for the first week
    for (int i = 1; i < firstDayOfWeek; ++i) {
      week += "   ";
    }

    // Add days
    for (int dayNum = 1; dayNum <= daysInMonth; ++dayNum) {
      QString dayStr = QString("%1").arg(dayNum, 2);
      if (dayNum == day) {
        dayStr = "[" + dayStr + "]";
      } else {
        dayStr = " " + dayStr + " ";
      }
      week += dayStr;

      if ((dayNum + firstDayOfWeek - 1) % 7 == 0 || dayNum == daysInMonth) {
        calendarLines.append(week);
        week.clear();
      }
    }

    calendarLabel->setText(calendarLines.join("\n"));
  });

  // Start timer with 1 second interval
  timer->start(1000);

  fakeDesktopOverlay->setCentralWidget(container);

  // Show the overlay on all screens
  QList<QScreen *> screens = QGuiApplication::screens();
  if (screens.isEmpty()) {
    error("No screens found");
    delete fakeDesktopOverlay;
    fakeDesktopOverlay = nullptr;
    return;
  }

  // Show on all screens
  for (QScreen *screen : screens) {
    QRect screenGeometry = screen->geometry();
    fakeDesktopOverlay->setGeometry(screenGeometry);
    fakeDesktopOverlay->showFullScreen();
  }

  info("Fake desktop overlay shown");
}

void HotkeyManager::showBlackOverlay() {
  info("Showing black overlay window on all monitors");

  // Create a new QMainWindow for the overlay
  static QMainWindow *blackOverlay = nullptr;

  // If overlay already exists, close and delete it
  if (blackOverlay) {
    blackOverlay->close();
    delete blackOverlay;
    blackOverlay = nullptr;
    info("Black overlay hidden");
    return;
  }

  // Create a new overlay window
  blackOverlay = new QMainWindow();

  // Set window properties for overlay
  blackOverlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                               Qt::WindowStaysOnTopHint |
                               Qt::X11BypassWindowManagerHint);

  // Make the window transparent for input
  blackOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  // Calculate the combined geometry of all screens
  QRect combinedGeometry;
  for (QScreen *screen : QGuiApplication::screens()) {
    combinedGeometry = combinedGeometry.united(screen->geometry());
  }

  // Set the window geometry to cover all screens
  blackOverlay->setGeometry(combinedGeometry);

  // Set black background with some transparency (adjust alpha as needed)
  blackOverlay->setStyleSheet("background-color: rgba(0, 0, 0, 200);");

  // Show the overlay
  blackOverlay->showFullScreen();

  // Make sure the window is on top and has focus
  blackOverlay->raise();
  blackOverlay->activateWindow();

  info("Black overlay shown");
}

void HotkeyManager::printActiveWindowInfo() {
  wID activeWin = windowManager.GetActiveWindow();
  if (activeWin == 0) {
    info("╔══════════════════════════════════════╗");
    info("║      NO ACTIVE WINDOW DETECTED       ║");
    info("╚══════════════════════════════════════╝");
    return;
  }
  std::string windowClass = WindowManager::GetActiveWindowClass();
  std::string windowTitle;
  int x = 0, y = 0, width = 0, height = 0;

  try {
    Window window(activeWin);
    windowTitle = window.Title();

    // Get window geometry
    ::Window root;
    unsigned int border_width, depth;
    Display *display = XOpenDisplay(nullptr);
    if (display) {
      XGetGeometry(display, activeWin, &root, &x, &y, (unsigned int *)&width,
                   (unsigned int *)&height, &border_width, &depth);
      XCloseDisplay(display);
    }
  } catch (const std::exception &e) {
    windowTitle = "<error>";
    error("Failed to get window information: " + std::string(e.what()));
  } catch (...) {
    windowTitle = "<unknown error>";
    error("Unknown error getting window information");
  }
  std::string geometry = std::to_string(width) + "x" + std::to_string(height) +
                         "+" + std::to_string(x) + "+" + std::to_string(y);
  bool isGaming = isGamingWindow();

  auto formatLine = [](const std::string &label,
                       const std::string &value) -> std::string {
    std::string line = label + value;
    if (line.length() > 52) {
      line = line.substr(0, 49) + "...";
    }
    return "║ " + line + std::string(52 - line.length(), ' ') + "║";
  };

  info("╔══════════════════════════════════════════════════════════╗");
  info("║             ACTIVE WINDOW INFORMATION                    ║");
  info("╠══════════════════════════════════════════════════════════╣");
  info(formatLine("Window ID: ", std::to_string(activeWin)));
  info(formatLine("Window Title: \"", windowTitle + "\""));
  info(formatLine("Window Class: \"", windowClass + "\""));
  info(formatLine("Window Geometry: ", geometry));

  std::string gamingStatus =
      isGaming ? (COLOR_GREEN + std::string("YES ✓") + COLOR_RESET)
               : (COLOR_RED + std::string("NO ✗") + COLOR_RESET);
  info(formatLine("Is Gaming Window: ", gamingStatus));

  {
    std::lock_guard<std::mutex> lock(modeMutex);
    info(formatLine("Current Mode: ", currentMode));
  }
  info("╚══════════════════════════════════════════════════════════╝");

  logWindowEvent("WINDOW_INFO",
                 "Title: \"" + windowTitle + "\", Class: \"" + windowClass +
                     "\", Gaming: " + (isGamingWindow() ? "YES" : "NO") +
                     ", Geometry: " + geometry);
}

void HotkeyManager::cleanup() {
  // Set cleanup flag first to prevent deadlocks from other threads
  inCleanupMode.store(true);

  // Signal the update loop to stop
  {
    std::lock_guard<std::mutex> lock(updateLoopMutex);
    updateLoopRunning = false;
    updateLoopCv.notify_all();
  }

  // Wait for the update loop thread to finish
  if (updateLoopThread.joinable()) {
    updateLoopThread.join();
  }

  // Clean up other resources
  stopAllAutoclickers();

  // Safely set mode to default by just setting it directly without complex
  // updates
  {
    std::lock_guard<std::mutex> lock(modeMutex);
    currentMode = "default";
  }

  // Clear caches separately to avoid locking both mutexes at once for long
  // periods
  {
    std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
    conditionCache.clear();
  }

  if (conditionEngine) {
    conditionEngine->invalidateCache();
  }

  genshinAutomationActive = false;
  if (monitorThread.joinable()) {
    monitorThread.join();
  }
  // Stop all automation tasks
  {
    std::lock_guard<std::mutex> lock(automationMutex_);
    for (auto &[name, task] : automationTasks_) {
      if (task) {
        task->stop();
        if (verboseWindowLogging) {
          logWindowEvent("AUTOMATION_CLEANUP",
                         "Stopped automation task: " + name);
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

  // Stop the watchdog thread
  watchdogRunning = false;
  if (watchdogThread.joinable()) {
    watchdogThread.join();
  }

  if (verboseWindowLogging) {
    logWindowEvent("CLEANUP", "HotkeyManager resources cleaned up");
  }
}

void HotkeyManager::UpdateLoop() {
  if (inCleanupMode.load()) {
    return; // Don't start update loop if cleaning up
  }

  const auto updateInterval = std::chrono::milliseconds(
      2); // 500 updates per second for responsive hotkey handling

  while (updateLoopRunning && !inCleanupMode.load()) {
    auto startTime = std::chrono::steady_clock::now();

    try {
      // Wait if paused
      if (updateLoopPaused.load()) {
        std::unique_lock<std::mutex> lock(updateLoopMutex);
        updateLoopCv.wait(lock, [this]() {
          return !updateLoopPaused.load() || !updateLoopRunning.load();
        });

        if (!updateLoopRunning.load()) {
          break; // Exit if we're shutting down
        }
      }

      // Update conditional hotkeys only if not paused
      if (!updateLoopPaused.load()) {
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastConditionCheck)
                .count() >= CONDITION_CHECK_INTERVAL_MS) {
          // Only check conditions without mode switching and without interval
          // restriction
          batchUpdateConditionalHotkeys();
          lastConditionCheck = now; // Update the timestamp
        }
      }

      // Update window properties for condition engine
      updateWindowProperties();

      // Update video playback status
      updateVideoPlaybackStatus();

    } catch (const std::exception &e) {
      error("Exception in UpdateLoop: {}", e.what());
    } catch (...) {
      error("Unknown exception in UpdateLoop");
    }

    // Sleep for the remaining time in the update interval (only if not paused)
    if (!updateLoopPaused.load()) {
      auto endTime = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          endTime - startTime);

      if (elapsed < updateInterval) {
        std::unique_lock<std::mutex> lock(updateLoopMutex);
        updateLoopCv.wait_for(lock, updateInterval - elapsed, [this] {
          return !updateLoopRunning || updateLoopPaused.load();
        });
      }
    }
  }
}
void HotkeyManager::updateWindowProperties() { return; }
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
  bool windowLogging =
      Configs::Get().Get<bool>("Debug.VerboseWindowLogging", false);
  bool conditionLogging =
      Configs::Get().Get<bool>("Debug.VerboseConditionLogging", false);

  setVerboseKeyLogging(keyLogging);
  setVerboseWindowLogging(windowLogging);
  setVerboseConditionLogging(conditionLogging);

  info("Debug settings: KeyLogging=" + std::to_string(verboseKeyLogging) +
       ", WindowLogging=" + std::to_string(verboseWindowLogging) +
       ", ConditionLogging=" + std::to_string(verboseConditionLogging));
}

void HotkeyManager::RegisterAnyKeyPressCallback(AnyKeyPressCallback callback) {
  std::lock_guard<std::mutex> lock(callbacksMutex);
  onAnyKeyPressedCallbacks.push_back(callback);

  // Set up the IO callback only once if not already set
  if (onAnyKeyPressedCallbacks.size() == 1) {
    // Register the callback with IO to receive all key press events
    io.SetAnyKeyPressCallback(
        [this](const std::string &key) { NotifyAnyKeyPressed(key); });
  }
}

void HotkeyManager::NotifyAnyKeyPressed(const std::string &key) {
  std::vector<AnyKeyPressCallback> callbacksCopy;
  {
    std::lock_guard<std::mutex> lock(callbacksMutex);
    callbacksCopy = onAnyKeyPressedCallbacks;
  }

  // Call all callbacks without holding the lock
  for (const auto &callback : callbacksCopy) {
    if (callback) {
      callback(key);
    }
  }
}

void HotkeyManager::NotifyInputReceived() {
  lastInputTime.store(std::chrono::steady_clock::now());
}

void HotkeyManager::WatchdogLoop() {
  while (watchdogRunning.load()) {
    // Check if freeze detection should be disabled
    if (inputFreezeTimeoutSeconds <= 0) {
      return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastInputTime.load());

    if (elapsed.count() > inputFreezeTimeoutSeconds) {
      error("INPUT FREEZE DETECTED! No input for {} seconds (threshold: {}s)",
            elapsed.count(), inputFreezeTimeoutSeconds);

      // Try non-blocking restart in a separate thread to avoid watchdog
      // blocking
      std::thread([this]() {
        error("Emergency restart of input system...");

        try {
          auto *listener = io.GetEventListener();
          if (!listener) {
            error("EventListener not available");
            return;
          }

          // Stop first
          listener->Stop();

          // Get devices (cached, no locks)
          std::vector<std::string> devices = io.GetInputDevices();

          if (devices.empty()) {
            error("No input devices found");
            return;
          }

          // Restart
          listener->Start(devices, true);

          // Re-establish callback
          if (io.IsUsingNewEventListener()) {
            listener->inputNotificationCallback = [this]() {
              NotifyInputReceived();
            };
          }

          info("EventListener restarted successfully");

        } catch (const std::exception &e) {
          error("Failed to restart EventListener: {}", e.what());
        }
      }).detach(); // Detach to avoid blocking watchdog

      lastInputTime.store(now);
    }

    // Use a reasonable sleep interval based on the timeout, but don't make it
    // too long to allow for responsive timeout changes
    std::this_thread::sleep_for(std::chrono::seconds(
        std::min(5, std::max(1, inputFreezeTimeoutSeconds / 10))));
  }
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

  // Load initial timeout value
  inputFreezeTimeoutSeconds =
      Configs::Get().Get<int>("Input.FreezeTimeoutSeconds", 300);

  Configs::Get().Watch<bool>(
      "Debug.VerboseKeyLogging", [this](bool oldValue, bool newValue) {
        info("Key logging setting changed from " + std::to_string(oldValue) +
             " to " + std::to_string(newValue));
        setVerboseKeyLogging(newValue);
      });

  Configs::Get().Watch<bool>(
      "Debug.VerboseWindowLogging", [this](bool oldValue, bool newValue) {
        info("Window logging setting changed from " + std::to_string(oldValue) +
             " to " + std::to_string(newValue));
        setVerboseWindowLogging(newValue);
      });

  Configs::Get().Watch<bool>(
      "Debug.VerboseConditionLogging", [this](bool oldValue, bool newValue) {
        info("Condition logging setting changed from " +
             std::to_string(oldValue) + " to " + std::to_string(newValue));
        setVerboseConditionLogging(newValue);
      });

  // Watch for changes to the freeze timeout configuration
  Configs::Get().Watch<int>("Input.FreezeTimeoutSeconds", [this](int oldValue,
                                                                 int newValue) {
    info("Input freeze timeout changed from {}s to {}s", oldValue, newValue);
    inputFreezeTimeoutSeconds = newValue;
  });
}

bool HotkeyManager::isWindowClassInList(const std::string &windowClass,
                                        const std::string &classList) {
  if (classList.empty()) {
    return false;
  }

  // Split the comma-separated list
  size_t start = 0;
  size_t end = classList.find(',');
  while (end != std::string::npos) {
    std::string cls = classList.substr(start, end - start);
    // Remove leading/trailing spaces
    cls.erase(0, cls.find_first_not_of(" \t"));
    cls.erase(cls.find_last_not_of(" \t") + 1);

    if (!cls.empty() && windowClass.find(cls) != std::string::npos) {
      return true;
    }
    start = end + 1;
    end = classList.find(',', start);
  }
  // Check the last part
  std::string cls = classList.substr(start);
  cls.erase(0, cls.find_first_not_of(" \t"));
  cls.erase(cls.find_last_not_of(" \t") + 1);

  if (!cls.empty() && windowClass.find(cls) != std::string::npos) {
    return true;
  }

  return false;
}

// Network functions implementation
void HotkeyManager::makeHttpRequest(const std::string &url,
                                    const std::string &method) {
  if (!networkManager) {
    showNotification("Network Error", "NetworkManager not available");
    return;
  }

  try {
    // Parse URL to get host and path
    size_t protocolPos = url.find("://");
    if (protocolPos == std::string::npos) {
      showNotification("Network Error", "Invalid URL format");
      return;
    }

    std::string hostPath = url.substr(protocolPos + 3);
    size_t pathPos = hostPath.find('/');
    std::string host =
        (pathPos == std::string::npos) ? hostPath : hostPath.substr(0, pathPos);
    std::string path =
        (pathPos == std::string::npos) ? "/" : hostPath.substr(pathPos);

    // Create HTTP client configuration
    net::NetworkConfig config;
    config.host = host;
    config.port = 80; // Default HTTP port
    config.timeoutMs = 10000;

    // Create HTTP client
    int clientId = networkManager->createHttpClient(config);
    auto *client = networkManager->getComponentAs<net::HttpClient>(clientId);

    if (client) {
      client->setCallback([this](const net::NetworkEvent &event) {
        if (event.type == net::NetworkEventType::Error) {
          showNotification("HTTP Request Failed", event.error);
        } else if (event.type == net::NetworkEventType::DataReceived) {
          showNotification("HTTP Response",
                           "Response received: " +
                               std::to_string(event.data.length()) + " bytes");
        }
      });

      // Make request based on method
      net::HttpClient::HttpResponse response;
      if (method == "GET") {
        response = client->get(path);
      } else if (method == "POST") {
        response = client->post(path, "");
      } else if (method == "PUT") {
        response = client->put(path, "");
      } else if (method == "DELETE") {
        response = client->del(path);
      } else {
        showNotification("Network Error", "Unsupported HTTP method: " + method);
        networkManager->destroyComponent(clientId);
        return;
      }

      if (response.error.empty()) {
        showNotification("HTTP Request Success",
                         "Status: " + std::to_string(response.statusCode) +
                             " " + response.statusText);
      } else {
        showNotification("HTTP Request Failed", response.error);
      }

      networkManager->destroyComponent(clientId);
    } else {
      showNotification("Network Error", "Failed to create HTTP client");
    }
  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::downloadFile(const std::string &url,
                                 const std::string &outputPath) {
  if (!networkManager) {
    showNotification("Network Error", "NetworkManager not available");
    return;
  }

  try {
    // Parse URL to get host and path
    size_t protocolPos = url.find("://");
    if (protocolPos == std::string::npos) {
      showNotification("Network Error", "Invalid URL format");
      return;
    }

    std::string hostPath = url.substr(protocolPos + 3);
    size_t pathPos = hostPath.find('/');
    std::string host =
        (pathPos == std::string::npos) ? hostPath : hostPath.substr(0, pathPos);
    std::string path =
        (pathPos == std::string::npos) ? "/" : hostPath.substr(pathPos);

    // Extract filename from URL or use default
    std::string filename = outputPath;
    if (filename.empty()) {
      size_t lastSlash = path.find_last_of('/');
      filename = (lastSlash == std::string::npos) ? "downloaded_file"
                                                  : path.substr(lastSlash + 1);
      if (filename.empty())
        filename = "downloaded_file";
    }

    // Create HTTP client configuration
    net::NetworkConfig config;
    config.host = host;
    config.port = 80;
    config.timeoutMs = 30000; // Longer timeout for downloads

    // Create HTTP client
    int clientId = networkManager->createHttpClient(config);
    auto *client = networkManager->getComponentAs<net::HttpClient>(clientId);

    if (client) {
      client->setCallback([this, filename](const net::NetworkEvent &event) {
        if (event.type == net::NetworkEventType::Error) {
          showNotification("Download Failed", event.error);
        } else if (event.type == net::NetworkEventType::DataReceived) {
          // In a real implementation, you would save the data to file
          showNotification("Download Progress",
                           "Received " + std::to_string(event.data.length()) +
                               " bytes");
        }
      });

      // Make GET request to download file
      auto response = client->get(path);

      if (response.error.empty()) {
        // Save response body to file (simplified)
        std::ofstream outFile(filename);
        if (outFile) {
          outFile << response.body;
          outFile.close();
          showNotification("Download Complete",
                           "File saved as: " + filename + " (" +
                               std::to_string(response.body.length()) +
                               " bytes)");
        } else {
          showNotification("Download Failed",
                           "Could not create file: " + filename);
        }
      } else {
        showNotification("Download Failed", response.error);
      }

      networkManager->destroyComponent(clientId);
    } else {
      showNotification("Network Error", "Failed to create HTTP client");
    }
  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::pingHost(const std::string &host) {
  if (!networkManager) {
    showNotification("Network Error", "NetworkManager not available");
    return;
  }

  try {
    // Use port checking as a simple ping alternative
    bool reachable = net::NetworkManager::isPortOpen(host, 80);

    if (reachable) {
      showNotification("Ping Result", host + " is reachable (port 80)");
    } else {
      // Try other common ports
      reachable = net::NetworkManager::isPortOpen(host, 443);
      if (reachable) {
        showNotification("Ping Result", host + " is reachable (port 443)");
      } else {
        showNotification("Ping Result", host + " is not reachable");
      }
    }

    // Also validate IP/hostname format
    bool validIp = net::NetworkManager::isValidIpAddress(host);
    bool validHost = net::NetworkManager::isValidHostname(host);

    std::string validation = "Format: ";
    if (validIp)
      validation += "Valid IP";
    else if (validHost)
      validation += "Valid Hostname";
    else
      validation += "Invalid Format";

    showNotification("Host Validation", validation);

  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::checkNetworkStatus() {
  if (!networkManager) {
    showNotification("Network Error", "NetworkManager not available");
    return;
  }

  try {
    // Get local IP addresses
    auto localIps = net::NetworkManager::getLocalIpAddresses();

    std::string status = "Network Status:\n";
    status += "Local IPs: " + std::to_string(localIps.size()) + "\n";

    for (const auto &ip : localIps) {
      status += "  " + ip + "\n";
    }

    // Test connectivity to common hosts
    std::vector<std::string> testHosts = {"google.com", "cloudflare.com",
                                          "github.com"};
    int reachableCount = 0;

    for (const auto &host : testHosts) {
      if (net::NetworkManager::isPortOpen(host, 443)) {
        reachableCount++;
        status += host + ": Reachable\n";
      } else {
        status += host + ": Not Reachable\n";
      }
    }

    status += "Overall: " + std::to_string(reachableCount) + "/" +
              std::to_string(testHosts.size()) + " hosts reachable";

    showNotification("Network Status", status);

  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

// Shell-based network functions (use runShell for shell features)
void HotkeyManager::makeHttpRequestShell(const std::string &url,
                                         const std::string &method,
                                         const std::string &data) {
  try {
    std::string cmd =
        "curl -s -w '\\nHTTP Status: %{http_code}\\nTime: %{time_total}s\\n' ";

    if (method == "GET") {
      cmd += "'" + url + "'";
    } else if (method == "POST") {
      cmd += "-X POST -H 'Content-Type: application/json' -d '" + data + "' '" +
             url + "'";
    } else if (method == "PUT") {
      cmd += "-X PUT -H 'Content-Type: application/json' -d '" + data + "' '" +
             url + "'";
    } else if (method == "DELETE") {
      cmd += "-X DELETE '" + url + "'";
    } else {
      showNotification("Network Error", "Unsupported HTTP method: " + method);
      return;
    }

    auto result = Launcher::runShell(cmd);

    if (result.success) {
      showNotification("HTTP Request", "Shell request completed successfully");
      info("Shell HTTP Response: {}", result.stdout);
    } else {
      showNotification("HTTP Request Failed", result.error);
      error("Shell HTTP Error: {}", result.error);
    }
  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::downloadFileShell(const std::string &url,
                                      const std::string &outputPath) {
  try {
    std::string filename = outputPath;
    if (filename.empty()) {
      // Extract filename from URL
      size_t lastSlash = url.find_last_of('/');
      filename = (lastSlash == std::string::npos) ? "downloaded_file"
                                                  : url.substr(lastSlash + 1);
      if (filename.empty())
        filename = "downloaded_file";
    }

    std::string cmd =
        "curl -L -o '" + filename + "' '" + url + "' --progress-bar";

    auto result = Launcher::runShell(cmd);

    if (result.success) {
      showNotification("Download Complete", "File saved as: " + filename);
      info("Shell download completed: {}", filename);
    } else {
      showNotification("Download Failed", result.error);
      error("Shell download error: {}", result.error);
    }
  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::pingHostShell(const std::string &host) {
  try {
    // Use ping command with shell features
    std::string cmd = "ping -c 4 '" + host + "' 2>/dev/null | tail -1";

    auto result = Launcher::runShell(cmd);

    if (result.success && !result.stdout.empty()) {
      showNotification("Ping Result", host + ": " + result.stdout);
      info("Shell ping result for {}: {}", host, result.stdout);
    } else {
      // Try with -c 1 for faster response
      cmd = "ping -c 1 '" + host +
            "' >/dev/null 2>&1 && echo 'Host is reachable' || echo 'Host is "
            "unreachable'";
      result = Launcher::runShell(cmd);

      if (result.success) {
        showNotification("Ping Result", host + ": " + result.stdout);
      } else {
        showNotification("Ping Failed", result.error);
      }
    }
  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::checkNetworkStatusShell() {
  try {
    // Use shell commands for comprehensive network status
    std::string cmd = R"(
      echo "=== Network Status ==="
      echo "Local IPs:"
      ip -4 addr show | grep -oP '(?<=inet\s)\d+(\.\d+){3}' | head -3
      echo ""
      echo "Internet Connectivity:"
      for host in google.com cloudflare.com github.com; do
        if ping -c 1 -W 2 "$host" >/dev/null 2>&1; then
          echo "  $host: ✓ Reachable"
        else
          echo "  $host: ✗ Not Reachable"
        fi
      done
      echo ""
      echo "DNS Resolution:"
      if nslookup google.com >/dev/null 2>&1; then
        echo "  DNS: ✓ Working"
      else
        echo "  DNS: ✗ Failed"
      fi
    )";

    auto result = Launcher::runShell(cmd);

    if (result.success) {
      showNotification("Network Status", "Shell network check completed");
      info("Shell network status:\n{}", result.stdout);
    } else {
      showNotification("Network Status Failed", result.error);
      error("Shell network status error: {}", result.error);
    }
  } catch (const std::exception &e) {
    showNotification("Network Error", std::string("Exception: ") + e.what());
  }
}

void HotkeyManager::RegisterNetworkHotkeys() {
  // HTTP Request hotkeys (NetworkManager-based)
  io.Hotkey("^!h",
            [this]() { makeHttpRequest("http://httpbin.org/get", "GET"); });

  io.Hotkey("^!p", [this]() { pingHost("google.com"); });

  io.Hotkey("^!d", [this]() { downloadFile("http://httpbin.org/json", ""); });

  io.Hotkey("^!n", [this]() { checkNetworkStatus(); });

  // Shell-based network hotkeys (use runShell for shell features)
  io.Hotkey("^!+h", [this]() {
    makeHttpRequestShell("http://httpbin.org/get", "GET");
  });

  io.Hotkey("^!+p", [this]() { pingHostShell("google.com"); });

  io.Hotkey("^!+d",
            [this]() { downloadFileShell("http://httpbin.org/json", ""); });

  io.Hotkey("^!+n", [this]() { checkNetworkStatusShell(); });

  // Advanced shell-based examples
  io.Hotkey("^!+s", [this]() {
    // Shell example with pipes and command substitution
    std::string cmd = "echo 'Testing shell features:' && "
                      "echo 'Current time: $(date)' && "
                      "echo 'System info: $(uname -a)' && "
                      "echo 'Network interfaces: $(ip link show | grep -E "
                      "\"^[0-9]\" | awk \"{print \\$2}\" | sed \"s/://\")'";

    auto result = Launcher::runShell(cmd);
    if (result.success) {
      showNotification("Shell Test", "Shell features working");
      info("Shell test result:\n{}", result.stdout);
    }
  });

  // Custom HTTP request with data
  io.Hotkey("^!+x", [this]() {
    makeHttpRequestShell("https://httpbin.org/post", "POST",
                         "{\"test\": \"data\"}");
  });
}

} // namespace havel
