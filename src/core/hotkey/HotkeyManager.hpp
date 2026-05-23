#pragma once

#include "core/condition/ConditionalHotkeyManager.hpp"
#include "core/mode/ModeManager.hpp"
#include "core/CallbackTypes.hpp"
#include "core/MouseGestureTypes.hpp"
#include "core/io/MouseGestureEngine.hpp"
#include "../havel-lang/compiler/runtime/EventQueue.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel {

struct HotKey;

class IO;
class WindowManager;
class MPVController;
class AudioManager;
class Interpreter;
class ScreenshotManager;
class BrightnessManager;

namespace net {
class NetworkManager;
}

class HotkeyManager {
public:
  struct HotkeyInfo {
    int id = 0;
    std::string alias;
    bool enabled = false;
  };

  struct ConditionalHotkeyInfo {
    int id = 0;
    std::string key;
    std::string condition;
    bool enabled = false;
    bool active = false;
  };

  explicit HotkeyManager(std::shared_ptr<IO> io);
  HotkeyManager(std::shared_ptr<IO> io, WindowManager &, MPVController &, AudioManager &,
                ScreenshotManager *, BrightnessManager &,
                std::shared_ptr<net::NetworkManager>);
  ~HotkeyManager();

  bool AddHotkey(const std::string &key, std::function<void()> callback);
  bool AddHotkey(const std::string &key, std::function<void()> callback, int id);
  bool AddHotkey(const std::string &key, const std::string &action);
  bool RemoveHotkey(const std::string &key);
  bool RemoveHotkey(int id);  // Remove by id
  void EnableHotkey(const std::string &key);
  void DisableHotkey(const std::string &key);
  void handleHotkeyTrigger(int hotkeyId);
  bool GrabHotkey(int id);      // Grab hotkey by id
  bool UngrabHotkey(int id);    // Ungrab hotkey by id

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

  void LoadHotkeyConfigurations();
  void ReloadConfigurations();
  void clearAllHotkeys();
  std::vector<HotkeyInfo> getHotkeyList() const;
  std::vector<ConditionalHotkeyInfo> getConditionalHotkeyList() const;
  void printHotkeys() const;

  void updateAllConditionalHotkeys();
  void forceUpdateAllConditionalHotkeys();
  void reevaluateConditionalHotkeys(IO &io);
  void setConditionalHotkeysEnabled(bool enabled);
  void setMode(const std::string& mode);  // Set current mode for conditional hotkeys
  std::string getMode() const;  // Get current mode
 void setEventQueue(compiler::EventQueue* eq);
 compiler::EventQueue* getEventQueue() const { return eventQueue_; }
 std::mutex &getHotkeyMutex();

  // Test harness: programmatically trigger a hotkey by alias
  // Implementation in .cpp to avoid incomplete type issues
  void triggerForTest(const std::string &alias);

  // Test harness: get current queue sizes
  void getQueueStatsForTest(size_t &total, size_t &enabled) const;

  // ModeManager access
  std::shared_ptr<ModeManager>& getModeManager() { return modeManager; }
  const std::shared_ptr<ModeManager>& getModeManager() const { return modeManager; }

  
  ConditionalHotkeyManager& getConditionalHotkeyManager() { return conditionalManager; }
  const ConditionalHotkeyManager& getConditionalHotkeyManager() const { return conditionalManager; }

  void loadDebugSettings();
  void applyDebugSettings();
  void cleanup();

  void toggleFakeDesktopOverlay();
  void showBlackOverlay();
  void printActiveWindowInfo();
  void toggleWindowFocusTracking();

  void RegisterAnyKeyPressCallback(AnyKeyPressCallback callback);
  void NotifyAnyKeyPressed(const std::string &key);
  void NotifyInputReceived();

  bool HandleInputEvent(const InputEvent &event);

static std::unordered_map<int, HotKey> &RegisteredHotkeys();
  static std::mutex &RegisteredHotkeysMutex();

// Internal: track registered hotkey callbacks for test triggering
// Maps alias -> callback function
std::unordered_map<std::string, std::function<void()>> testCallbacks_;
std::vector<ConditionalHotkey>* activeConditionalHotkeys = nullptr;
bool conditionalHotkeysEnabled = true;

private:
  void initializeInputCallbacks();

  std::shared_ptr<IO> io;  // Shared ownership to ensure IO stays alive
  ConditionalHotkeyManager conditionalManager;
  std::shared_ptr<ModeManager> modeManager;  // Shared ownership for mode management
  std::vector<AnyKeyPressCallback> anyKeyCallbacks;
  mutable std::mutex anyKeyCallbacksMutex;
  bool inputCallbacksInitialized = false;
  bool focusTrackingEnabled = false;

  struct ModifierState {
    bool leftCtrl = false;
    bool rightCtrl = false;
    bool leftShift = false;
    bool rightShift = false;
    bool leftAlt = false;
    bool rightAlt = false;
    bool leftMeta = false;
    bool rightMeta = false;
  };

  struct ActiveInput {
    std::chrono::steady_clock::time_point timestamp;
    int modifiers = 0;
  };

  void updateModifierState(int keyCode, bool down);
  int currentModifierMask() const;
  bool checkModifierMatch(int required, bool wildcard) const;
  bool evaluateCombo(const HotKey &hotkey) const;
  bool evaluateWheelCombo(const HotKey &hotkey, int wheelDirection) const;
  void executeHotkey(const HotKey &hotkey) const;

  mutable std::shared_mutex stateMutex;
  std::unordered_map<int, ActiveInput> activeInputs;
  std::unordered_map<int, bool> physicalKeyStates;
  std::unordered_map<int, bool> mouseButtonStates;
  ModifierState modifierState;
  bool isProcessingWheelEvent = false;
  int currentWheelDirection = 0;
  std::chrono::steady_clock::time_point lastWheelUpTime{};
  std::chrono::steady_clock::time_point lastWheelDownTime{};

 MouseGestureEngine mouseGestureEngine;
 std::unordered_set<int> registeredGestureHotkeys;
 std::chrono::steady_clock::time_point lastMovementHotkeyTime{};
    compiler::EventQueue* eventQueue_ = nullptr;
};

} // namespace havel
