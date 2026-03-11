#include "HotkeyManager.hpp"
#include "IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/io/KeyMap.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <thread>

namespace havel {

static std::unordered_map<int, HotKey> g_registeredHotkeys;
static std::mutex g_registeredHotkeysMutex;

std::unordered_map<int, HotKey> &HotkeyManager::RegisteredHotkeys() {
  return g_registeredHotkeys;
}

std::mutex &HotkeyManager::RegisteredHotkeysMutex() {
  return g_registeredHotkeysMutex;
}

HotkeyManager::HotkeyManager(std::shared_ptr<IO> io)
    : activeConditionalHotkeys(conditionalManager.GetHotkeys()), io(io),
      conditionalManager(io) {
  conditionalManager.SetEnabled(conditionalHotkeysEnabled);
  initializeInputCallbacks();
}

HotkeyManager::HotkeyManager(std::shared_ptr<IO> io, WindowManager &, MPVController &,
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
  return io->Hotkey(key, std::move(callback));
}

bool HotkeyManager::AddHotkey(const std::string &key,
                              std::function<void()> callback, int id) {
  {
    std::lock_guard<std::mutex> lock(simpleHotkeysMutex);
    simpleHotkeys[key] = callback;
    simpleHotkeyEnabled[key] = true;
  }
  return io->Hotkey(key, std::move(callback), "", id);
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
      io->UngrabHotkey(it->first);
      hotkeys.erase(it);
      return true;
    }
  }
  return false;
}

bool HotkeyManager::RemoveHotkey(int id) {
  std::lock_guard<std::mutex> lock(RegisteredHotkeysMutex());
  auto &hotkeys = RegisteredHotkeys();
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    io->UngrabHotkey(id);
    hotkeys.erase(it);
    return true;
  }
  return false;
}

bool HotkeyManager::GrabHotkey(int id) {
  std::lock_guard<std::mutex> lock(RegisteredHotkeysMutex());
  auto &hotkeys = RegisteredHotkeys();
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    return io->GrabHotkey(id);
  }
  return false;
}

bool HotkeyManager::UngrabHotkey(int id) {
  std::lock_guard<std::mutex> lock(RegisteredHotkeysMutex());
  auto &hotkeys = RegisteredHotkeys();
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    io->UngrabHotkey(id);
    return true;
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
      io->UngrabHotkey(id);
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

  io->SetAnyKeyPressCallback(
      [this](const std::string &key) { NotifyAnyKeyPressed(key); });
  io->SetInputEventCallback([this](const InputEvent &event) {
    if (event.down || event.kind == InputEventKind::MouseMove ||
        event.kind == InputEventKind::MouseWheel) {
      NotifyInputReceived();
    }
  });
  io->SetInputBlockCallback(
      [this](const InputEvent &event) { return HandleInputEvent(event); });
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

void HotkeyManager::updateModifierState(int keyCode, bool down) {
  // Caller holds stateMutex.
  if (keyCode == KEY_LEFTCTRL) {
    modifierState.leftCtrl = down;
  } else if (keyCode == KEY_RIGHTCTRL) {
    modifierState.rightCtrl = down;
  } else if (keyCode == KEY_LEFTSHIFT) {
    modifierState.leftShift = down;
  } else if (keyCode == KEY_RIGHTSHIFT) {
    modifierState.rightShift = down;
  } else if (keyCode == KEY_LEFTALT) {
    modifierState.leftAlt = down;
  } else if (keyCode == KEY_RIGHTALT) {
    modifierState.rightAlt = down;
  } else if (keyCode == KEY_LEFTMETA) {
    modifierState.leftMeta = down;
  } else if (keyCode == KEY_RIGHTMETA) {
    modifierState.rightMeta = down;
  }
}

int HotkeyManager::currentModifierMask() const {
  int mask = 0;
  if (modifierState.leftCtrl || modifierState.rightCtrl) {
    mask |= 1 << 0;
  }
  if (modifierState.leftShift || modifierState.rightShift) {
    mask |= 1 << 1;
  }
  if (modifierState.leftAlt || modifierState.rightAlt) {
    mask |= 1 << 2;
  }
  if (modifierState.leftMeta || modifierState.rightMeta) {
    mask |= 1 << 3;
  }
  return mask;
}

bool HotkeyManager::checkModifierMatch(int required, bool wildcard) const {
  const bool ctrlRequired = (required & (1 << 0)) != 0;
  const bool shiftRequired = (required & (1 << 1)) != 0;
  const bool altRequired = (required & (1 << 2)) != 0;
  const bool metaRequired = (required & (1 << 3)) != 0;

  const bool ctrlPressed = modifierState.leftCtrl || modifierState.rightCtrl;
  const bool shiftPressed = modifierState.leftShift || modifierState.rightShift;
  const bool altPressed = modifierState.leftAlt || modifierState.rightAlt;
  const bool metaPressed = modifierState.leftMeta || modifierState.rightMeta;

  if (wildcard) {
    return (!ctrlRequired || ctrlPressed) && (!shiftRequired || shiftPressed) &&
           (!altRequired || altPressed) && (!metaRequired || metaPressed);
  }

  return (ctrlRequired == ctrlPressed) && (shiftRequired == shiftPressed) &&
         (altRequired == altPressed) && (metaRequired == metaPressed);
}

bool HotkeyManager::evaluateWheelCombo(const HotKey &hotkey,
                                       int wheelDirection) const {
  auto now = std::chrono::steady_clock::now();

  int comboTimeWindow = hotkey.comboTimeWindow;
  if (comboTimeWindow > 0) {
    auto wheelTime = (wheelDirection > 0) ? lastWheelUpTime : lastWheelDownTime;
    if (wheelTime.time_since_epoch().count() == 0) {
      return false;
    }

    auto wheelAge =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - wheelTime)
            .count();

    if (wheelAge > comboTimeWindow) {
      return false;
    }
  }

  for (const auto &comboKey : hotkey.comboSequence) {
    if (comboKey.type == HotkeyType::MouseWheel) {
      if (comboKey.wheelDirection != 0 &&
          comboKey.wheelDirection != wheelDirection) {
        return false;
      }
      continue;
    }

    int keyCode = 0;
    if (comboKey.type == HotkeyType::MouseButton) {
      keyCode = comboKey.mouseButton;
    } else if (comboKey.type == HotkeyType::Keyboard ||
               comboKey.type == HotkeyType::MouseMove) {
      keyCode = static_cast<int>(comboKey.key);
    } else {
      return false;
    }

    auto it = activeInputs.find(keyCode);
    if (it == activeInputs.end()) {
      return false;
    }

    if (comboTimeWindow > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - it->second.timestamp)
                         .count();
      if (elapsed > comboTimeWindow) {
        return false;
      }
    }

    if (comboKey.modifiers != 0) {
      if (!checkModifierMatch(comboKey.modifiers, comboKey.wildcard)) {
        return false;
      }
    }
  }

  return true;
}

bool HotkeyManager::evaluateCombo(const HotKey &hotkey) const {
  auto now = std::chrono::steady_clock::now();

  if (hotkey.requiresWheel && !isProcessingWheelEvent) {
    return false;
  }

  std::set<int> requiredKeys;
  int requiredModifiers = 0;

  for (const auto &comboKey : hotkey.comboSequence) {
    if (comboKey.type == HotkeyType::Keyboard) {
      int keyCode = static_cast<int>(comboKey.key);
      if (KeyMap::IsModifier(keyCode)) {
        if (keyCode == KEY_LEFTCTRL || keyCode == KEY_RIGHTCTRL) {
          requiredModifiers |= 1 << 0;
        } else if (keyCode == KEY_LEFTSHIFT || keyCode == KEY_RIGHTSHIFT) {
          requiredModifiers |= 1 << 1;
        } else if (keyCode == KEY_LEFTALT || keyCode == KEY_RIGHTALT) {
          requiredModifiers |= 1 << 2;
        } else if (keyCode == KEY_LEFTMETA || keyCode == KEY_RIGHTMETA) {
          requiredModifiers |= 1 << 3;
        }
      } else {
        requiredKeys.insert(keyCode);
      }
    } else if (comboKey.type == HotkeyType::MouseButton) {
      requiredKeys.insert(comboKey.mouseButton);
    } else if (comboKey.type == HotkeyType::MouseMove) {
      requiredKeys.insert(static_cast<int>(comboKey.key));
    } else if (comboKey.type == HotkeyType::MouseWheel) {
      continue;
    } else {
      return false;
    }
  }

  if (!hotkey.wildcard) {
    for (const auto &[code, input] : activeInputs) {
      if (KeyMap::IsModifier(code)) {
        continue;
      }
      if (requiredKeys.count(code)) {
        continue;
      }

      bool shadow = false;
      for (int reqKey : requiredKeys) {
        auto it = activeInputs.find(reqKey);
        if (it != activeInputs.end() &&
            it->second.timestamp == input.timestamp) {
          shadow = true;
          break;
        }
      }
      if (shadow) {
        continue;
      }

      return false;
    }
  }

  int comboTimeWindow = hotkey.comboTimeWindow;
  for (int requiredKey : requiredKeys) {
    auto it = activeInputs.find(requiredKey);
    if (it == activeInputs.end()) {
      return false;
    }

    if (comboTimeWindow > 0) {
      auto keyAge = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - it->second.timestamp);
      if (keyAge.count() > comboTimeWindow) {
        return false;
      }
    }
  }

  if (!hotkey.requiredPhysicalKeys.empty()) {
    for (int keyCode : hotkey.requiredPhysicalKeys) {
      auto it = physicalKeyStates.find(keyCode);
      if (it == physicalKeyStates.end() || !it->second) {
        return false;
      }
    }
  }

  if (requiredModifiers != 0) {
    int currentMods = currentModifierMask();
    if (!hotkey.wildcard) {
      if (currentMods != requiredModifiers) {
        return false;
      }
    } else {
      if ((currentMods & requiredModifiers) != requiredModifiers) {
        return false;
      }
    }
  }

  return true;
}

void HotkeyManager::executeHotkey(const HotKey &hotkey) const {
  if (!hotkey.callback) {
    return;
  }

  auto callback = hotkey.callback;
  auto alias = hotkey.alias;
  if (auto *executor = io->GetHotkeyExecutor()) {
    auto result = executor->submit([callback = std::move(callback),
                                    hotkeyAlias = alias]() {
      try {
        callback();
      } catch (const std::exception &e) {
        error("Hotkey '{}' threw: {}", hotkeyAlias, e.what());
      } catch (...) {
        error("Hotkey '{}' threw unknown exception", hotkeyAlias);
      }
    });
    if (!result.accepted) {
      warn("Hotkey task queue full, dropping callback: {}", alias);
    }
    return;
  }

  std::thread([callback = std::move(callback), hotkeyAlias = alias]() {
    try {
      callback();
    } catch (const std::exception &e) {
      error("Hotkey '{}' threw: {}", hotkeyAlias, e.what());
    } catch (...) {
      error("Hotkey '{}' threw unknown exception", hotkeyAlias);
    }
  }).detach();
}

bool HotkeyManager::HandleInputEvent(const InputEvent &event) {
  bool shouldBlock = false;
  auto now = std::chrono::steady_clock::now();

  if (event.kind == InputEventKind::Key) {
    const int effectiveCode =
        (event.mappedCode != 0) ? event.mappedCode : event.code;
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    updateModifierState(effectiveCode, event.down);

    if (event.down) {
      int mods = currentModifierMask();
      ActiveInput input;
      input.timestamp = now;
      input.modifiers = mods;

      activeInputs[effectiveCode] = input;
      if (event.originalCode != 0 && event.originalCode != effectiveCode) {
        activeInputs[event.originalCode] = input;
      }
      if (event.originalCode != 0) {
        physicalKeyStates[event.originalCode] = true;
      }
    } else {
      activeInputs.erase(effectiveCode);
      if (event.originalCode != 0 && event.originalCode != effectiveCode) {
        activeInputs.erase(event.originalCode);
      }
      if (event.originalCode != 0) {
        physicalKeyStates[event.originalCode] = false;
      }
    }
  } else if (event.kind == InputEventKind::MouseButton) {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    if (event.down) {
      int mods = currentModifierMask();
      ActiveInput input;
      input.timestamp = now;
      input.modifiers = mods;
      activeInputs[event.code] = input;
      mouseButtonStates[event.code] = true;
    } else {
      activeInputs.erase(event.code);
      mouseButtonStates[event.code] = false;
    }
  } else if (event.kind == InputEventKind::MouseWheel) {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    isProcessingWheelEvent = true;
    currentWheelDirection = (event.value > 0) ? 1 : -1;
    if (currentWheelDirection > 0) {
      lastWheelUpTime = now;
    } else {
      lastWheelDownTime = now;
    }
  }

  std::vector<int> matchedHotkeyIds;

  {
    std::shared_lock<std::shared_mutex> stateLock(stateMutex);
    std::lock_guard<std::mutex> hotkeyLock(RegisteredHotkeysMutex());

    for (auto &[id, hotkey] : RegisteredHotkeys()) {
      if (!hotkey.enabled || !hotkey.evdev) {
        continue;
      }

      if (hotkey.type == HotkeyType::Combo) {
        bool matched = false;
        if (event.kind == InputEventKind::MouseWheel && hotkey.requiresWheel) {
          int wheelDir = (event.value > 0) ? 1 : -1;
          matched = evaluateWheelCombo(hotkey, wheelDir);
        } else {
          matched = evaluateCombo(hotkey);
        }

        if (matched) {
          matchedHotkeyIds.push_back(id);
          if (hotkey.grab) {
            shouldBlock = true;
          }
        }
        continue;
      }

      if (event.kind == InputEventKind::Key ||
          event.kind == InputEventKind::MouseButton) {
        if (hotkey.type == HotkeyType::MouseButton &&
            event.kind != InputEventKind::MouseButton) {
          continue;
        }
        if (hotkey.type == HotkeyType::Keyboard &&
            event.kind != InputEventKind::Key) {
          continue;
        }

        if (hotkey.type == HotkeyType::MouseButton) {
          if (hotkey.mouseButton != event.code) {
            continue;
          }
        } else {
          if (hotkey.key != static_cast<Key>(event.code)) {
            continue;
          }
        }

        if (!hotkey.repeat && event.repeat) {
          continue;
        }

        if (hotkey.eventType == HotkeyEventType::Down && !event.down) {
          continue;
        }
        if (hotkey.eventType == HotkeyEventType::Up && event.down) {
          continue;
        }

        if (hotkey.modifiers != 0 &&
            !checkModifierMatch(hotkey.modifiers, hotkey.wildcard)) {
          continue;
        }

        if (!hotkey.contexts.empty()) {
          bool contextMatch =
              std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                          [](auto &ctx) { return ctx(); });
          if (!contextMatch) {
            continue;
          }
        }

        if (hotkey.repeatInterval > 0 && event.repeat) {
          auto elapsed =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - hotkey.lastTriggerTime)
                  .count();
          if (elapsed < hotkey.repeatInterval) {
            continue;
          }
          hotkey.lastTriggerTime = now;
        } else if (event.down && !event.repeat) {
          hotkey.lastTriggerTime = now;
        }

        matchedHotkeyIds.push_back(id);
        if (hotkey.grab) {
          shouldBlock = true;
        }
      } else if (event.kind == InputEventKind::MouseWheel &&
                 hotkey.type == HotkeyType::MouseWheel) {
        int wheelDir = (event.value > 0) ? 1 : -1;
        if (hotkey.wheelDirection != 0 &&
            hotkey.wheelDirection != wheelDir) {
          continue;
        }

        if (hotkey.modifiers != 0 &&
            !checkModifierMatch(hotkey.modifiers, hotkey.wildcard)) {
          continue;
        }

        if (!hotkey.contexts.empty()) {
          bool contextMatch =
              std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                          [](auto &ctx) { return ctx(); });
          if (!contextMatch) {
            continue;
          }
        }

        matchedHotkeyIds.push_back(id);
        if (hotkey.grab) {
          shouldBlock = true;
        }
      }
    }
  }

  for (int id : matchedHotkeyIds) {
    std::lock_guard<std::mutex> hotkeyLock(RegisteredHotkeysMutex());
    auto it = RegisteredHotkeys().find(id);
    if (it != RegisteredHotkeys().end() && it->second.enabled) {
      executeHotkey(it->second);
    }
  }

  if (event.kind == InputEventKind::MouseMove) {
    int dx = event.dx;
    int dy = event.dy;

    {
      std::lock_guard<std::mutex> hotkeyLock(RegisteredHotkeysMutex());
      std::unordered_set<int> currentGestureIds;
      for (const auto &[id, hotkey] : RegisteredHotkeys()) {
        if (hotkey.type != HotkeyType::MouseGesture) {
          continue;
        }
        currentGestureIds.insert(id);
        if (!registeredGestureHotkeys.count(id)) {
          auto directions = mouseGestureEngine.ParsePattern(hotkey.gesturePattern);
          if (!directions.empty()) {
            mouseGestureEngine.RegisterHotkey(id, directions);
          }
        }
      }
      for (int id : registeredGestureHotkeys) {
        if (!currentGestureIds.count(id)) {
          mouseGestureEngine.UnregisterHotkey(id);
        }
      }
      registeredGestureHotkeys = std::move(currentGestureIds);
    }

    if (mouseGestureEngine.HasRegisteredGestures()) {
      const auto matches = mouseGestureEngine.RecordMovement(dx, dy);
      for (int id : matches) {
        std::lock_guard<std::mutex> hotkeyLock(RegisteredHotkeysMutex());
        auto it = RegisteredHotkeys().find(id);
        if (it != RegisteredHotkeys().end() && it->second.enabled &&
            it->second.type == HotkeyType::MouseGesture) {
          executeHotkey(it->second);
          if (it->second.grab) {
            shouldBlock = true;
          }
          break;
        }
      }
    }

    const int threshold = 5;
    int virtualKey = 0;
    if (dx != 0 && std::abs(dx) >= threshold) {
      virtualKey = (dx > 0) ? 10002 : 10001;
    } else if (dy != 0 && std::abs(dy) >= threshold) {
      virtualKey = (dy > 0) ? 10004 : 10003;
    }

    bool processMovementHotkeys = (virtualKey != 0);
    if (processMovementHotkeys &&
        lastMovementHotkeyTime.time_since_epoch().count() != 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - lastMovementHotkeyTime)
                         .count();
      if (elapsed < 10) {
        processMovementHotkeys = false;
      }
    }
    if (processMovementHotkeys) {
      lastMovementHotkeyTime = now;

      std::vector<int> movementHotkeys;
      {
        std::shared_lock<std::shared_mutex> stateLock(stateMutex);
        std::lock_guard<std::mutex> hotkeyLock(RegisteredHotkeysMutex());
        for (auto &[id, hotkey] : RegisteredHotkeys()) {
          if (!hotkey.enabled) {
            continue;
          }
          if (hotkey.type != HotkeyType::Keyboard &&
              hotkey.type != HotkeyType::MouseMove) {
            continue;
          }
          if (static_cast<int>(hotkey.key) != virtualKey) {
            continue;
          }
          if (hotkey.modifiers != 0 &&
              !checkModifierMatch(hotkey.modifiers, hotkey.wildcard)) {
            continue;
          }
          if (!hotkey.contexts.empty()) {
            bool contextMatch =
                std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                            [](auto &ctx) { return ctx(); });
            if (!contextMatch) {
              continue;
            }
          }
          movementHotkeys.push_back(id);
          if (hotkey.grab) {
            shouldBlock = true;
          }
        }
      }

      for (int id : movementHotkeys) {
        std::lock_guard<std::mutex> hotkeyLock(RegisteredHotkeysMutex());
        auto it = RegisteredHotkeys().find(id);
        if (it != RegisteredHotkeys().end() && it->second.enabled) {
          executeHotkey(it->second);
        }
      }
    }
  }

  if (event.kind == InputEventKind::MouseWheel) {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    isProcessingWheelEvent = false;
    currentWheelDirection = 0;
  }

  return shouldBlock;
}

} // namespace havel
