#pragma once
#include "../media//MPVController.hpp"
#include "../utils/Utils.hpp"
#include "../window/WindowManager.hpp"
#include "BrightnessManager.hpp"
#include "ConditionSystem.hpp"
#include "ConfigManager.hpp"
#include "IO.hpp"
#include "ScriptEngine.hpp"
#include "automation/AutoClicker.hpp"
#include "automation/AutoKeyPresser.hpp"
#include "automation/AutoRunner.hpp"
#include "automation/AutomationManager.hpp"
#include "core/io/KeyTap.hpp"
#include "gui/ScreenshotManager.hpp"
#include "io/MouseController.hpp"
#include "media/AudioManager.hpp"
#include "qt.hpp"
#include "utils/Timer.hpp"
#include <atomic>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace havel {
struct HotkeyDefinition {
  std::string key;
  std::function<void()> trueAction;
  std::function<void()> falseAction; // Optional
  int id;
};

struct ConditionalHotkey {
  int id;
  std::string key;
  std::string condition; // String condition (for legacy)
  std::function<bool()>
      conditionFunc; // Function condition (for new functionality)
  std::function<void()> trueAction;
  std::function<void()> falseAction;
  bool currentlyGrabbed = false;
  bool lastConditionResult = false;
  bool usesFunctionCondition =
      false; // Flag to indicate which condition type to use
};

// Callback type for any key press
using AnyKeyPressCallback = std::function<void(const std::string &key)>;

class MPVController; // Forward declaration
class HotkeyManager {
public:
  HotkeyManager(IO &io, WindowManager &windowManager, MPVController &mpv,
                AudioManager &audioManager, ScriptEngine &scriptEngine,
                ScreenshotManager &screenshotManager,
                BrightnessManager &brightnessManager);
  std::unique_ptr<MouseController> mouseController;
  virtual ~HotkeyManager() { cleanup(); }
  void Zoom(int zoom);
  void printHotkeys() const;
  // Debug flags
  bool verboseKeyLogging = false;
  bool verboseWindowLogging = false;
  bool verboseConditionLogging = false;
  int winOffset = 10;

  int speed = 5;
  float acc = 1.0f;

  bool evaluateCondition(const std::string &condition);

  std::unique_ptr<ConditionEngine> conditionEngine;

  void setupConditionEngine();
  void updateWindowProperties();
  void setVerboseKeyLogging(bool value) { verboseKeyLogging = value; }

  void setVerboseWindowLogging(bool value) { verboseWindowLogging = value; }

  void setVerboseConditionLogging(bool value) {
    verboseConditionLogging = value;
  }

  // Load debug settings from config
  void loadDebugSettings();

  // Apply current debug settings
  void applyDebugSettings();

  // Cache statistics
  void printCacheStats();

  void RegisterDefaultHotkeys();

  void RegisterMediaHotkeys();

  void RegisterWindowHotkeys();

  void RegisterSystemHotkeys();

  void updateAllConditionalHotkeys();

  void forceUpdateAllConditionalHotkeys();

  void onActiveWindowChanged(wID newWindow);

  void LoadHotkeyConfigurations();

  void ReloadConfigurations();

  // Mode management
  void setMode(const std::string &mode);

  std::string getMode() const;

  // Public cleanup for safe shutdown
  void cleanup();

  // Automation
  std::shared_ptr<automation::AutomationManager> automationManager_;
  std::unordered_map<std::string, automation::TaskPtr> automationTasks_;
  std::mutex automationMutex_;

  void registerAutomationHotkeys();
  void startAutoClicker(const std::string &button = "left");
  void startAutoRunner(const std::string &direction = "w");
  void startAutoKeyPresser(const std::string &key = "space");
  void stopAutomationTask(const std::string &taskType);
  void toggleAutomationTask(const std::string &taskType,
                            const std::string &param = "");

  bool AddHotkey(const std::string &hotkeyStr, std::function<void()> callback);
  bool AddHotkey(const std::string &hotkeyStr, const std::string &action);

  bool RemoveHotkey(const std::string &hotkeyStr);
  // Contextual hotkey support
  int AddContextualHotkey(const std::string &key, const std::string &condition,
                          std::function<void()> trueAction,
                          std::function<void()> falseAction = nullptr,
                          int id = 0);
  int AddContextualHotkey(const std::string &key,
                          std::function<bool()> condition,
                          std::function<void()> trueAction,
                          std::function<void()> falseAction = nullptr,
                          int id = 0);
  int AddGamingHotkey(const std::string &key, std::function<void()> trueAction,
                      std::function<void()> falseAction = nullptr, int id = 0);
  IO &io;

private:
  bool altTabPressed = false;
  std::thread monitorThread;
  // Condition evaluation state
  std::chrono::steady_clock::time_point lastConditionCheck =
      std::chrono::steady_clock::now();
  static constexpr int CONDITION_CHECK_INTERVAL_MS =
      1; // Check every 1ms for immediate response to window changes
  std::atomic<std::chrono::steady_clock::time_point> lastModeSwitch =
      std::chrono::steady_clock::now();
  static constexpr int MODE_SWITCH_DEBOUNCE_MS = 150; // 150ms debounce for mode switching
  std::unique_ptr<KeyTap> lwin;
  std::unique_ptr<KeyTap> ralt;

  // Watchdog for detecting input freezes
  std::atomic<std::chrono::steady_clock::time_point> lastInputTime{
      std::chrono::steady_clock::now()
  };
  std::thread watchdogThread;
  std::atomic<bool> watchdogRunning{true};

  // Configurable timeout for input freeze detection (in seconds)
  int inputFreezeTimeoutSeconds{300}; // Default to 5 minutes

  // Deferred update queue for conditional hotkeys
  std::queue<int> deferredUpdateQueue;
  std::mutex deferredUpdateMutex;

  // Flag to indicate we're in cleanup mode to prevent deadlocks
  std::atomic<bool> inCleanupMode{false};
  // Cached condition results
  struct CachedCondition {
    bool result;
    std::chrono::steady_clock::time_point timestamp;
  };
  std::unordered_map<std::string, CachedCondition> conditionCache;
  static constexpr int CACHE_DURATION_MS = 50; // Cache results for 50ms

  static std::mutex modeMutex;
  static std::string currentMode;
  static std::mutex conditionCacheMutex; // Protects conditionCache

  bool isZooming() const { return m_isZooming; }
  void setZooming(bool zooming) { m_isZooming = zooming; }

  // Helper method for gaming process detection
  static bool IsGamingProcess(pid_t pid);

  // Overlay functionality
  void toggleFakeDesktopOverlay();
  void showBlackOverlay();

  // Window management
  void minimizeActiveWindow();

  void maximizeActiveWindow();

  void tileWindows();

  void centerActiveWindow();

  void restoreWindow();

  // Active window info
  void printActiveWindowInfo();

  void toggleWindowFocusTracking();

  static bool isGamingWindow();

  void PlayPause();
  std::atomic<bool> winKeyComboDetected{false};
  std::chrono::steady_clock::time_point winKeyPressTime;
  std::atomic<bool> genshinAutomationActive = false;
  std::thread genshinThread;

  // Key tap callback storage
  std::vector<AnyKeyPressCallback> onAnyKeyPressedCallbacks;
  mutable std::mutex callbacksMutex; // Protect callback vector

  // Genshin automation timer management
  std::shared_ptr<std::atomic<bool>> fTimer = nullptr;
  std::shared_ptr<std::atomic<bool>> spaceTimer = nullptr;
  bool fRunning = false;
  WindowManager &windowManager;
  MPVController &mpv;
  AudioManager &audioManager;
  ScriptEngine &scriptEngine;
  ScreenshotManager &screenshotManager;
  BrightnessManager &brightnessManager;

  // Mode management
  bool m_isZooming{false};
  bool videoPlaying{false};
  time_t lastVideoCheck{0};
  const int VIDEO_TIMEOUT_SECONDS{1800}; // 30 minutes
  bool holdClick = false;
  enum class ControlMode { BRIGHTNESS, GAMMA, TEMPERATURE, SHADOW_LIFT };
  enum class TargetMonitor { ALL, MONITOR_0, MONITOR_1 };

  ControlMode currentBrightnessMode = ControlMode::BRIGHTNESS;
  TargetMonitor targetBrightnessMonitor = TargetMonitor::ALL;

  // Window groups
  std::vector<std::string> videoSites; // Will be loaded from config
  bool mouse1Pressed{false};
  bool mouse2Pressed{false};

  std::unique_ptr<automation::AutoClicker> autoClicker;
  std::unique_ptr<automation::AutoRunner> autoRunner;
  std::unique_ptr<automation::AutoKeyPresser> autoKeyPresser;
  wID autoclickerWindowID = 0;
  double zoomLevel = 1.0;
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
      {"scrolllock", "Scroll_Lock"}};

  // Helper functions
  void showNotification(const std::string &title, const std::string &message);

  std::string convertKeyName(const std::string &keyName);

  std::string parseHotkeyString(const std::string &hotkeyStr);

  // Autoclicker helpers
  void stopAllAutoclickers();

  void startAutoclicker(const std::string &button);

  // Key name conversion helpers
  std::string handleKeycode(const std::string &input);

  std::string handleScancode(const std::string &input);

  std::string normalizeKeyName(const std::string &keyName);

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
  void logKeyEvent(const std::string &key, const std::string &eventType,
                   const std::string &details = "");

  void logWindowEvent(const std::string &eventType,
                      const std::string &details = "");

  std::string getWindowInfo(wID windowId = 0);

  // Logging helpers
  void logHotkeyEvent(const std::string &eventType, const std::string &details);

  void logKeyConversion(const std::string &from, const std::string &to);

  void logModeSwitch(const std::string &from, const std::string &to);

  // New methods for video window group and playback status
  bool isVideoSiteActive();

  void updateVideoPlaybackStatus();

  void handleMediaCommand(const std::vector<std::string> &mpvCommand);

  // New methods for video timeout
  void loadVideoSites();

  bool hasVideoTimedOut() const;

  void updateLastVideoCheck();

public:
  // Key tap callback registration
  void RegisterAnyKeyPressCallback(AnyKeyPressCallback callback);

  // Internal method to notify all key presses
  void NotifyAnyKeyPressed(const std::string &key);

  // Method to notify that input was received (for watchdog)
  void NotifyInputReceived();

  static std::vector<ConditionalHotkey> conditionalHotkeys;

  // Static accessors (for backward compatibility)
  static std::string getCurrentMode();
  static bool getCurrentGamingWindowStatusStatic();

  // Instance accessors for IO suspend functionality
  bool getCurrentGamingWindowStatus() const;
  void reevaluateConditionalHotkeys(IO& io);
  void reevaluateConditionalHotkeysInstance(IO& io); // For use with shared_ptr

private:
  // Store IDs of MPV hotkeys for grab/ungrab
  std::vector<int> conditionalHotkeyIds;
  std::vector<int> gamingHotkeyIds;
  std::vector<HotkeyDefinition> mpvHotkeys;
  std::mutex hotkeyMutex; // Protects conditionalHotkeys and conditionCache
  std::atomic<bool> updateLoopPaused{false}; // Flag to pause update loop when window changes
  void InvalidateConditionalHotkeys();
  void updateConditionalHotkey(ConditionalHotkey &hotkey);
  void updateHotkeyState(ConditionalHotkey &hotkey, bool conditionMet);
  void batchUpdateConditionalHotkeys();
  ConditionalHotkey* findConditionalHotkey(int id);
  // Window focus tracking
  bool trackWindowFocus;
  wID lastActiveWindowId;


  // Update loop members
  std::thread updateLoopThread;
  std::atomic<bool> updateLoopRunning{false};
  std::condition_variable updateLoopCv;
  std::mutex updateLoopMutex;
  void UpdateLoop();
  void WatchdogLoop();
};
} // namespace havel