#pragma once

#include "ConditionalHotkeyManager.hpp"
#include "core/CallbackTypes.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

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

  explicit HotkeyManager(IO &io);
  HotkeyManager(IO &io, WindowManager &, MPVController &, AudioManager &,
                Interpreter &, ScreenshotManager *, BrightnessManager &,
                std::shared_ptr<net::NetworkManager>);
  ~HotkeyManager();

  bool AddHotkey(const std::string &key, std::function<void()> callback);
  bool AddHotkey(const std::string &key, const std::string &action);
  bool RemoveHotkey(const std::string &key);
  void HandleKeyEvent(const std::string &key);
  void EnableHotkey(const std::string &key);
  void DisableHotkey(const std::string &key);

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
  std::mutex &getHotkeyMutex();

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

  static std::unordered_map<int, HotKey> &RegisteredHotkeys();
  static std::mutex &RegisteredHotkeysMutex();

  bool conditionalHotkeysEnabled = true;
  std::vector<ConditionalHotkey> &activeConditionalHotkeys;

private:
  void initializeInputCallbacks();

  IO &io;
  ConditionalHotkeyManager conditionalManager;
  std::unordered_map<std::string, std::function<void()>> simpleHotkeys;
  std::unordered_map<std::string, bool> simpleHotkeyEnabled;
  mutable std::mutex simpleHotkeysMutex;
  std::vector<AnyKeyPressCallback> anyKeyCallbacks;
  mutable std::mutex anyKeyCallbacksMutex;
  bool inputCallbacksInitialized = false;
  bool focusTrackingEnabled = false;
};

} // namespace havel
