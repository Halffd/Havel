#include "HotkeyManager.hpp"
#include "IO.hpp"
#include "utils/Logger.hpp"
#include <algorithm>

namespace havel {

static std::unordered_map<int, HotKey> g_registeredHotkeys;
static std::mutex g_registeredHotkeysMutex;

std::unordered_map<int, HotKey> &HotkeyManager::RegisteredHotkeys() {
  return g_registeredHotkeys;
}

std::mutex &HotkeyManager::RegisteredHotkeysMutex() {
  return g_registeredHotkeysMutex;
}

HotkeyManager::HotkeyManager(IO &io)
    : activeConditionalHotkeys(conditionalManager.GetHotkeys()), io(io),
      conditionalManager(io) {
  conditionalManager.SetEnabled(conditionalHotkeysEnabled);
  initializeInputCallbacks();
}

HotkeyManager::HotkeyManager(IO &io, WindowManager &, MPVController &,
                             AudioManager &, Interpreter &, ScreenshotManager *,
                             BrightnessManager &,
                             std::shared_ptr<net::NetworkManager>)
    : HotkeyManager(io) {}

HotkeyManager::~HotkeyManager() { cleanup(); }

bool HotkeyManager::AddHotkey(const std::string &key,
                              std::function<void()> callback) {
  {
    std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
    simpleHotkeys[key] = callback;
    simpleHotkeyEnabled[key] = true;
  }
  return io.Hotkey(key, std::move(callback));
}

bool HotkeyManager::AddHotkey(const std::string &key,
                              const std::string &action) {
  return AddHotkey(key, [action]() { info("Hotkey action: {}", action); });
}

bool HotkeyManager::RemoveHotkey(const std::string &key) {
  {
    std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
    simpleHotkeys.erase(key);
    simpleHotkeyEnabled.erase(key);
  }

  std::lock_guard<std::mutex> lock(RegisteredHotkeysMutex());
  auto &hotkeys = RegisteredHotkeys();
  for (auto it = hotkeys.begin(); it != hotkeys.end(); ++it) {
    if (it->second.alias == key) {
      io.UngrabHotkey(it->first);
      hotkeys.erase(it);
      return true;
    }
  }
  return false;
}

void HotkeyManager::HandleKeyEvent(const std::string &key) {
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
    auto enabled = simpleHotkeyEnabled.find(key);
    auto it = simpleHotkeys.find(key);
    if (it == simpleHotkeys.end() || enabled == simpleHotkeyEnabled.end() ||
        !enabled->second) {
      return;
    }
    callback = it->second;
  }

  try {
    callback();
  } catch (const std::exception &e) {
    error("Error executing hotkey callback for '{}': {}", key, e.what());
  }
}

void HotkeyManager::EnableHotkey(const std::string &key) {
  std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
  simpleHotkeyEnabled[key] = true;
}

void HotkeyManager::DisableHotkey(const std::string &key) {
  std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
  simpleHotkeyEnabled[key] = false;
}

int HotkeyManager::AddContextualHotkey(const std::string &key,
                                       const std::string &condition,
                                       std::function<void()> trueAction,
                                       std::function<void()> falseAction,
                                       int id) {
  return conditionalManager.AddConditionalHotkey(
      key, condition, std::move(trueAction), std::move(falseAction), id);
}

int HotkeyManager::AddContextualHotkey(const std::string &key,
                                       std::function<bool()> condition,
                                       std::function<void()> trueAction,
                                       std::function<void()> falseAction,
                                       int id) {
  return conditionalManager.AddConditionalHotkey(
      key, std::move(condition), std::move(trueAction),
      std::move(falseAction), id);
}

int HotkeyManager::AddGamingHotkey(const std::string &key,
                                   std::function<void()> trueAction,
                                   std::function<void()> falseAction, int id) {
  return AddContextualHotkey(key, "mode == 'gaming'", std::move(trueAction),
                             std::move(falseAction), id);
}

void HotkeyManager::LoadHotkeyConfigurations() {
  info("LoadHotkeyConfigurations: no static hotkey config loader implemented");
}

void HotkeyManager::ReloadConfigurations() { LoadHotkeyConfigurations(); }

void HotkeyManager::clearAllHotkeys() {
  std::lock_guard<std::mutex> ioLock(RegisteredHotkeysMutex());
  auto &hotkeys = RegisteredHotkeys();
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.enabled) {
      io.UngrabHotkey(id);
    }
  }
  hotkeys.clear();

  std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
  simpleHotkeys.clear();
  simpleHotkeyEnabled.clear();
}

std::vector<HotkeyManager::HotkeyInfo> HotkeyManager::getHotkeyList() const {
  std::vector<HotkeyInfo> list;
  std::lock_guard<std::mutex> lock(RegisteredHotkeysMutex());
  auto &hotkeys = RegisteredHotkeys();
  list.reserve(hotkeys.size());
  for (const auto &[id, hotkey] : hotkeys) {
    list.push_back(HotkeyInfo{id, hotkey.alias, hotkey.enabled});
  }
  return list;
}

std::vector<HotkeyManager::ConditionalHotkeyInfo>
HotkeyManager::getConditionalHotkeyList() const {
  std::vector<ConditionalHotkeyInfo> list;
  auto &manager = const_cast<ConditionalHotkeyManager &>(conditionalManager);
  std::lock_guard<std::mutex> lock(manager.GetMutex());
  list.reserve(activeConditionalHotkeys.size());
  for (const auto &hotkey : activeConditionalHotkeys) {
    std::string conditionString;
    if (std::holds_alternative<std::string>(hotkey.condition)) {
      conditionString = std::get<std::string>(hotkey.condition);
    } else {
      conditionString = "<function>";
    }
    list.push_back(ConditionalHotkeyInfo{hotkey.id, hotkey.key, conditionString,
                                         hotkey.monitoringEnabled,
                                         hotkey.currentlyGrabbed});
  }
  return list;
}

void HotkeyManager::printHotkeys() const {
  for (const auto &hotkey : getHotkeyList()) {
    info("Hotkey {}: '{}' enabled={}", hotkey.id, hotkey.alias,
         hotkey.enabled);
  }
}

void HotkeyManager::updateAllConditionalHotkeys() {
  conditionalManager.SetEnabled(conditionalHotkeysEnabled);
  conditionalManager.UpdateAllConditionalHotkeys();
}

void HotkeyManager::forceUpdateAllConditionalHotkeys() {
  conditionalManager.SetEnabled(conditionalHotkeysEnabled);
  conditionalManager.ForceUpdateAllConditionalHotkeys();
}

void HotkeyManager::reevaluateConditionalHotkeys(IO &) {
  conditionalManager.SetEnabled(conditionalHotkeysEnabled);
  conditionalManager.ReevaluateConditionalHotkeys();
}

void HotkeyManager::setConditionalHotkeysEnabled(bool enabled) {
  conditionalHotkeysEnabled = enabled;
  conditionalManager.SetEnabled(enabled);
  conditionalManager.UpdateAllConditionalHotkeys();
}

std::mutex &HotkeyManager::getHotkeyMutex() {
  return conditionalManager.GetMutex();
}

void HotkeyManager::loadDebugSettings() {}

void HotkeyManager::applyDebugSettings() {}

void HotkeyManager::cleanup() {
  conditionalHotkeysEnabled = false;
  conditionalManager.SetEnabled(false);
}

void HotkeyManager::toggleFakeDesktopOverlay() {
  info("toggleFakeDesktopOverlay: not implemented");
}

void HotkeyManager::showBlackOverlay() {
  info("showBlackOverlay: not implemented");
}

void HotkeyManager::printActiveWindowInfo() {
  info("printActiveWindowInfo: not implemented");
}

void HotkeyManager::toggleWindowFocusTracking() {
  focusTrackingEnabled = !focusTrackingEnabled;
  info("Window focus tracking {}", focusTrackingEnabled ? "enabled" : "disabled");
}

void HotkeyManager::initializeInputCallbacks() {
  if (inputCallbacksInitialized) {
    return;
  }

  io.SetAnyKeyPressCallback(
      [this](const std::string &key) { NotifyAnyKeyPressed(key); });
  io.SetInputEventCallback([this](const InputEvent &event) {
    if (event.down || event.kind == InputEventKind::MouseMove ||
        event.kind == InputEventKind::MouseWheel) {
      NotifyInputReceived();
    }
  });
  inputCallbacksInitialized = true;
}

void HotkeyManager::RegisterAnyKeyPressCallback(AnyKeyPressCallback callback) {
  initializeInputCallbacks();
  std::lock_guard<std::mutex> lock(anyKeyCallbacksMutex);
  anyKeyCallbacks.push_back(std::move(callback));
}

void HotkeyManager::NotifyAnyKeyPressed(const std::string &key) {
  std::vector<AnyKeyPressCallback> callbacks;
  {
    std::lock_guard<std::mutex> lock(anyKeyCallbacksMutex);
    callbacks = anyKeyCallbacks;
  }
  for (const auto &callback : callbacks) {
    if (callback) {
      callback(key);
    }
  }
}

void HotkeyManager::NotifyInputReceived() {}

} // namespace havel
