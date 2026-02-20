#include "EventListener.hpp"
#include "../IO.hpp"
#include "../core/MouseGestureTypes.hpp" // Include mouse gesture types
#include "../io/HotkeyExecutor.hpp"
#include "../utils/Logger.hpp"
#include "KeyMap.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <linux/uinput.h>
#include <shared_mutex>
#include <signal.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace havel {
std::string EventListener::GetActiveInputsString() const {
  if (activeInputs.empty())
    return "[none]";

  std::string result;
  for (const auto &[code, input] : activeInputs) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - input.timestamp)
                       .count();

    result += std::to_string(code) + "(mods:0x" +
              std::to_string(input.modifiers) + ", " + std::to_string(elapsed) +
              "ms) ";
  }
  return result;
}

/**
 * EventListener - Unified input event listener for keyboard, mouse, and
 * joystick
 *
 * This class consolidates all input device handling into a single event loop.
 * It replaces the old separate evdev listeners (keyboard, mouse, gamepad) with
 * a unified approach that:
 *
 * 1. Monitors multiple input devices simultaneously using select()
 * 2. Processes keyboard, mouse button, mouse movement, wheel, and joystick
 * events
 * 3. Evaluates hotkeys with full support for:
 *    - Single keys with modifiers (Ctrl+W, Alt+Tab, etc.)
 *    - Mouse buttons (LButton, RButton, MButton, XButton1/2)
 *    - Mouse wheel (WheelUp, WheelDown)
 *    - Combos with & operator (LButton & RButton, CapsLock & W, etc.)
 *    - Joystick buttons (JoyA, JoyB, JoyX, JoyY, etc.)
 *    - Repeat intervals (@LAlt:850 for custom repeat timing)
 * 4. Applies mouse and scroll sensitivity scaling
 * 5. Supports key remapping (e.g., CapsLock -> Ctrl)
 * 6. Forwards events through uinput (with optional blocking)
 */
EventListener::EventListener() {
  shutdownFd = eventfd(0, EFD_NONBLOCK);
  gestureLastTime = std::chrono::steady_clock::now();
}

EventListener::~EventListener() {
  Stop();
  if (shutdownFd >= 0) {
    close(shutdownFd);
  }
  if (uinputFd >= 0) {
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
  }
}

bool EventListener::Start(const std::vector<std::string> &devicePaths,
                          bool grabDevices) {
  if (running.load()) {
    info("EventListener already running");
    return false;
  }

  this->grabDevices = grabDevices;

  // Create eventfd for shutdown signaling
  shutdownFd = eventfd(0, EFD_NONBLOCK);
  if (shutdownFd < 0) {
    error("Failed to create eventfd");
    return false;
  }

  // Open all devices
  for (const auto &path : devicePaths) {
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      error("Failed to open device: {}", path);
      continue;
    }

    char name[256] = "Unknown";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);

    // Try to grab device if requested
    if (grabDevices) {
      if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        error("Failed to grab device {} ({}): already grabbed elsewhere. "
              "Closing device.",
              name, path);
        close(fd);
        continue;
      }
      info("Successfully grabbed device: {} ({})", name, path);
    }

    DeviceInfo device;
    device.path = path;
    device.fd = fd;
    device.name = name;
    devices.push_back(device);

    info("Opened input device: {} ({})", name, path);
  }

  if (devices.empty()) {
    error("No input devices opened");
    return false;
  }

  running.store(true);
  shutdown.store(false);
  eventThread = std::thread(&EventListener::EventLoop, this);

  return true;
}

void EventListener::Stop() {
  if (!running.load()) {
    return;
  }

  running.store(false);
  shutdown.store(true);

  // Signal shutdown
  if (shutdownFd >= 0) {
    uint64_t val = 1;
    write(shutdownFd, &val, sizeof(val));
  }

  if (eventThread.joinable()) {
    eventThread.join();
  }

  // Release all pressed virtual keys before ungrabbing devices
  ReleaseAllVirtualKeys();

  // Ungrab and close all devices
  for (auto &device : devices) {
    if (device.fd >= 0) {
      if (grabDevices) {
        ioctl(device.fd, EVIOCGRAB, 0); // Ungrab device
      }
      close(device.fd);
    }
  }
  devices.clear();

  // Close the signal file descriptor if it was created
  if (signalFd >= 0) {
    close(signalFd);
    signalFd = -1;
  }
}

bool EventListener::SetupUinput() {
  uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinputFd < 0) {
    error("Failed to open /dev/uinput");
    return false;
  }

  // Enable all key events
  ioctl(uinputFd, UI_SET_EVBIT, EV_KEY);
  ioctl(uinputFd, UI_SET_EVBIT, EV_SYN);
  ioctl(uinputFd, UI_SET_EVBIT, EV_REL);
  // ioctl(uinputFd, UI_SET_EVBIT, EV_ABS);

  // Enable all keys
  for (int i = 0; i < KEY_MAX; i++) {
    ioctl(uinputFd, UI_SET_KEYBIT, i);
  }

  // Enable mouse buttons
  for (int i = BTN_MOUSE; i < BTN_JOYSTICK; i++) {
    ioctl(uinputFd, UI_SET_KEYBIT, i);
  }

  // Enable relative axes (mouse)
  ioctl(uinputFd, UI_SET_RELBIT, REL_X);
  ioctl(uinputFd, UI_SET_RELBIT, REL_Y);
  ioctl(uinputFd, UI_SET_RELBIT, REL_WHEEL);
  ioctl(uinputFd, UI_SET_RELBIT, REL_HWHEEL);

  // Setup device
  struct uinput_setup usetup = {};
  strcpy(usetup.name, "Havel Virtual Input");
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5678;
  usetup.id.version = 1;

  if (ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0) {
    error("Failed to setup uinput device");
    close(uinputFd);
    uinputFd = -1;
    return false;
  }

  if (ioctl(uinputFd, UI_DEV_CREATE) < 0) {
    error("Failed to create uinput device");
    close(uinputFd);
    uinputFd = -1;
    return false;
  }

  info("Uinput device created successfully");
  return true;
}

void EventListener::SendUinputEvent(int type, int code, int value) {
  if (uinputFd < 0) {
    error("Cannot send event: uinput not initialized (fd={})", uinputFd);
    return;
  }

  // Track pressed virtual keys for cleanup on shutdown
  if (type == EV_KEY) {
    if (value == 1 || value == 2) {
      pressedVirtualKeys.insert(code);
    } else if (value == 0) {
      pressedVirtualKeys.erase(code);
    }
  }

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  input_event ev = {};
  ev.time.tv_sec = ts.tv_sec;
  ev.time.tv_usec = ts.tv_nsec / 1000;
  ev.type = static_cast<__u16>(type);
  ev.code = static_cast<__u16>(code);
  ev.value = value;

  ssize_t written = write(uinputFd, &ev, sizeof(ev));
  if (written != sizeof(ev)) {
    error("Failed to write to uinput: {} (fd={})", strerror(errno), uinputFd);
    return;
  }

  // Send SYN event - critical for uinput to work properly
  input_event syn = {};
  syn.time.tv_sec = ts.tv_sec;
  syn.time.tv_usec = ts.tv_nsec / 1000;
  syn.type = EV_SYN;
  syn.code = SYN_REPORT;
  syn.value = 0;

  ssize_t syn_written = write(uinputFd, &syn, sizeof(syn));
  if (syn_written != sizeof(syn)) {
    error("Failed to write SYN event to uinput: {} (fd={})", strerror(errno),
          uinputFd);
  }

  debug("Forwarded uinput: type={} code={} value={} fd={}", type, code, value,
        uinputFd);
}

void EventListener::RegisterHotkey(int id, const HotKey &hotkey) {
  std::unique_lock<std::shared_mutex> lock(hotkeyMutex);

  // If it's a combo hotkey, build the key index
  if (hotkey.type == HotkeyType::Combo) {
    for (const auto &part : hotkey.comboSequence) {
      int keyCode = 0;
      if (part.type == HotkeyType::Keyboard ||
          part.type == HotkeyType::MouseMove) {
        keyCode = static_cast<int>(part.key);
      } else if (part.type == HotkeyType::MouseButton) {
        keyCode = part.mouseButton;
      } else if (part.type == HotkeyType::MouseWheel) {
        // Skip wheel parts for now
        continue;
      }

      if (keyCode != 0) {
        combosByKey[keyCode].push_back(id);
      }
    }

    // Initialize pressed count for this combo
    comboPressedCount[id] = 0;
  }
  // Handle mouse gesture hotkeys
  else if (hotkey.type == HotkeyType::MouseGesture) {
    // Parse the gesture pattern and register it
    std::vector<MouseGestureDirection> directions;

    // Parse directions from the gesture pattern string
    directions = ParseGesturePattern(hotkey);

    if (!directions.empty()) {
      RegisterGestureHotkey(id, directions);
    }
  }
}

void EventListener::UnregisterHotkey(int id) {
  std::unique_lock<std::shared_mutex> lock(hotkeyMutex);

  // Remove from hotkeys map
  auto it = IO::hotkeys.find(id);
  if (it == IO::hotkeys.end())
    return;

  // If it's a combo, clean up the key index
  if (it->second.type == HotkeyType::Combo) {
    // Remove from combosByKey
    for (auto &[keyCode, ids] : combosByKey) {
      auto idIt = std::find(ids.begin(), ids.end(), id);
      if (idIt != ids.end()) {
        ids.erase(idIt);

        // If no more combos use this key, remove the entry
        if (ids.empty()) {
          combosByKey.erase(keyCode);
        }
      }
    }

    // Remove from comboPressedCount
    comboPressedCount.erase(id);
  }
  // Handle mouse gesture hotkeys
  else if (it->second.type == HotkeyType::MouseGesture) {
    // Remove from gesture hotkeys
    gestureHotkeys.erase(id);
  }

  // Remove the hotkey
  IO::hotkeys.erase(it);
}

bool EventListener::GetKeyState(int evdevCode) const {
  std::shared_lock<std::shared_mutex> lock(stateMutex);
  auto it = evdevKeyState.find(evdevCode);
  return it != evdevKeyState.end() && it->second;
}

const EventListener::ModifierState &EventListener::GetModifierState() const {
  std::shared_lock<std::shared_mutex> lock(stateMutex);
  return modifierState;
}

void EventListener::SetBlockInput(bool block) { blockInput.store(block); }

void EventListener::AddKeyRemap(int fromCode, int toCode) {
  std::lock_guard<std::mutex> lock(remapMutex);
  keyRemaps[fromCode] = toCode;
}

void EventListener::RemoveKeyRemap(int fromCode) {
  std::lock_guard<std::mutex> lock(remapMutex);
  keyRemaps.erase(fromCode);
}

void EventListener::SetEmergencyShutdownKey(int evdevCode) {
  emergencyShutdownKey = evdevCode;
}

void EventListener::SetMouseSensitivity(double sensitivity) {
  IO::mouseSensitivity = sensitivity;
  mouseSensitivity = sensitivity;
}

void EventListener::SetScrollSpeed(double speed) {
  scrollSpeed = speed;
  IO::scrollSpeed = speed;
}

void EventListener::SetAnyKeyPressCallback(AnyKeyPressCallback callback) {
  this->anyKeyPressCallback = callback;
}

#ifdef __linux__
bool EventListener::StartX11Monitor(Display *display) {
  if (!display) {
    error("Cannot start X11 monitor: display is null");
    return false;
  }

  if (!x11Monitor) {
    x11Monitor = std::make_unique<X11HotkeyMonitor>();
  }

  // Register all current hotkeys with X11 monitor
  {
    std::unique_lock<std::shared_mutex> lock(hotkeyMutex);
    for (const auto &[id, hotkey] : IO::hotkeys) {
      if (!hotkey.evdev) { // Only X11 hotkeys
        x11Monitor->RegisterHotkey(id, hotkey);
      }
    }
  }

  return x11Monitor->Start(display);
}

void EventListener::StopX11Monitor() {
  if (x11Monitor) {
    x11Monitor->Stop();
  }
}

bool EventListener::IsX11MonitorRunning() const {
  return x11Monitor && x11Monitor->IsRunning();
}
#endif
int EventListener::GetCurrentModifiersMask() const {
  // This is called with stateMutex already locked
  int mask = 0;
  if (modifierState.IsCtrlPressed())
    mask |= Modifier::Ctrl;
  if (modifierState.IsShiftPressed())
    mask |= Modifier::Shift;
  if (modifierState.IsAltPressed())
    mask |= Modifier::Alt;
  if (modifierState.IsMetaPressed())
    mask |= Modifier::Meta;
  return mask;
}
void EventListener::EventLoop() {
  info("EventListener: Starting event loop");

  // Setup signal handling
  SetupSignalHandling();

  while (running.load() && !shutdown.load()) {
    fd_set readfds;
    FD_ZERO(&readfds);

    int maxFd = shutdownFd;
    FD_SET(shutdownFd, &readfds);

    // Add signal fd if available
    int signalFdToUse = signalFd;
    if (signalFdToUse >= 0) {
      FD_SET(signalFdToUse, &readfds);
      if (signalFdToUse > maxFd) {
        maxFd = signalFdToUse;
      }
    }

    for (const auto &device : devices) {
      FD_SET(device.fd, &readfds);
      if (device.fd > maxFd) {
        maxFd = device.fd;
      }
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int ret = select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);

    if (ret < 0) {
      if (errno == EINTR)
        continue;
      error("select() failed: {}", strerror(errno));
      break;
    }

    if (ret == 0)
      continue;

    if (FD_ISSET(shutdownFd, &readfds))
      break;

    // Check for signal
    if (signalFdToUse >= 0 && FD_ISSET(signalFdToUse, &readfds)) {
      ProcessSignal();
      continue;
    }

    // Process events from all devices
    for (const auto &device : devices) {
      if (!FD_ISSET(device.fd, &readfds))
        continue;

      struct input_event ev;
      ssize_t n = read(device.fd, &ev, sizeof(ev));

      if (n != sizeof(ev))
        continue;

      // Route based on event type AND code
      if (ev.type == EV_KEY) {
        // Check if it's a mouse button
        if (ev.code >= BTN_MOUSE && ev.code < BTN_JOYSTICK) {
          ProcessMouseEvent(ev);
        } else {
          ProcessKeyboardEvent(ev);
        }
      } else if (ev.type == EV_REL || ev.type == EV_ABS) {
        ProcessMouseEvent(ev);
      }
    }
  }

  info("EventListener: Waiting for {} callbacks", pendingCallbacks.load());

  auto shutdownStart = std::chrono::steady_clock::now();
  const auto maxShutdownTime = std::chrono::seconds(5);

  while (pendingCallbacks.load() > 0) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - shutdownStart);

    if (elapsed > maxShutdownTime) {
      error("Shutdown timeout: {} callbacks still pending",
            pendingCallbacks.load());
      break; // Force quit
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  info("EventListener: Stopped");
}
void EventListener::ProcessKeyboardEvent(const input_event &ev) {
  // Notify that input was received (for watchdog)
  if (inputNotificationCallback) {
    inputNotificationCallback();
  }

  int originalCode = ev.code;
  int mappedCode = originalCode;
  bool repeat = (ev.value == 2);
  bool down = (ev.value == 1 || repeat);

  // Get key name for callback notification
  std::string keyName = KeyMap::EvdevToString(originalCode);
  if (keyName.empty()) {
    // If no name found, use a generic format
    keyName = "evdev_" + std::to_string(originalCode);
  }

  // Send any key press notification
  if (anyKeyPressCallback && down) {
    anyKeyPressCallback(keyName);
  }

  // Handle key remapping
  {
    std::lock_guard<std::mutex> lock(remapMutex);
    if (down && !repeat) {
      // On press: apply remapping and store the mapping
      auto remapIt = keyRemaps.find(originalCode);
      if (remapIt != keyRemaps.end()) {
        mappedCode = remapIt->second;
      }
      // Store the mapping for this key press
      activeRemaps[originalCode] = mappedCode;
    } else if (!down) {
      // On release: use the same mapping we stored on press
      auto it = activeRemaps.find(originalCode);
      if (it != activeRemaps.end()) {
        mappedCode = it->second;
        // Remove from active remaps after we've used it for release
        activeRemaps.erase(it);
      } else {
        // Fallback: try to get remapping from keyRemaps
        auto remapIt = keyRemaps.find(originalCode);
        if (remapIt != keyRemaps.end()) {
          mappedCode = remapIt->second;
        }
      }
    }
  }

  // Update key state
  {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    evdevKeyState[originalCode] = down;

    // Update modifier state FIRST (before calculating currentModifiers)
    UpdateModifierState(originalCode, down);

    // NOW calculate modifiers (ModifierState is already updated)
    int currentModifiers = 0;
    if (modifierState.IsCtrlPressed())
      currentModifiers |= Modifier::Ctrl;
    if (modifierState.IsShiftPressed())
      currentModifiers |= Modifier::Shift;
    if (modifierState.IsAltPressed())
      currentModifiers |= Modifier::Alt;
    if (modifierState.IsMetaPressed())
      currentModifiers |= Modifier::Meta;

    // Track active inputs for combos - store BOTH original and mapped codes for
    // remapping support
    if (down) {
      // Store mapped code (what the system sees)
      activeInputs[mappedCode] = ActiveInput(currentModifiers);

      // Store original code too (what combo might be registered as)
      if (mappedCode != originalCode) {
        activeInputs[originalCode] = ActiveInput(currentModifiers);
      }

      // Track physical key state separately for precise combo matching
      physicalKeyStates[originalCode] = true;

      debug("🔑 Key PRESS: original={} mapped={} | Modifiers: {}{}{}{}",
            originalCode, mappedCode,
            modifierState.IsCtrlPressed() ? "Ctrl+" : "",
            modifierState.IsShiftPressed() ? "Shift+" : "",
            modifierState.IsAltPressed() ? "Alt+" : "",
            modifierState.IsMetaPressed() ? "Meta+" : "");

      // CHECK ALL COMBOS THAT MIGHT INCLUDE THIS KEY
      // Loop through ALL hotkeys to find combos (more reliable than indexed
      // lookup)
      for (auto &[hotkeyId, hotkey] : IO::hotkeys) {
        if (!hotkey.enabled || hotkey.type != HotkeyType::Combo) {
          continue;
        }

        // Check if this combo includes the pressed key (mapped or original)
        bool comboIncludesKey = false;
        for (const auto &comboKey : hotkey.comboSequence) {
          int comboKeyCode = (comboKey.type == HotkeyType::Keyboard)
                                 ? static_cast<int>(comboKey.key)
                             : (comboKey.type == HotkeyType::MouseButton)
                                 ? comboKey.mouseButton
                                 : -1;

          if (comboKeyCode == mappedCode || comboKeyCode == originalCode) {
            comboIncludesKey = true;
            break;
          }
        }

        // If this combo includes the key that was just pressed, evaluate it
        try {
          if (comboIncludesKey && EvaluateCombo(hotkey)) {
            debug("✅ Combo hotkey '{}' triggered on key press", hotkey.alias);
            ExecuteHotkeyCallback(hotkey);
          }
        } catch (const std::system_error &e) {
          error("System error evaluating combo '{}': {}", hotkey.alias,
                e.what());
          // Continue with other hotkeys instead of crashing
          continue;
        } catch (const std::exception &e) {
          error("Exception evaluating combo '{}': {}", hotkey.alias, e.what());
          // Continue with other hotkeys instead of crashing
          continue;
        }
      }
    } else {
      // KEY UP - ALWAYS clear from activeInputs
      activeInputs.erase(mappedCode);
      if (mappedCode != originalCode) {
        activeInputs.erase(originalCode);
      }

      // Clear physical key state as well
      physicalKeyStates[originalCode] = false;

      debug("🔼 Key UP: {} ({})", mappedCode,
            KeyMap::EvdevToString(mappedCode));
    }
    // Modifier state was already updated earlier
  }

  // Evaluate hotkeys
  bool shouldBlock = EvaluateHotkeys(originalCode, down, repeat);

  if (shouldBlock) {
    if (!down) {
      // Always release grabbed keys to prevent sticking
      SendUinputEvent(EV_KEY, mappedCode, 0);
    } else {
      debug("Blocking key {} down (mapped from {})", mappedCode, originalCode);
      // Don't send the down/repeat event
    }
  } else {
    // Not blocked, forward as-is
    SendUinputEvent(EV_KEY, mappedCode, ev.value);
  }
}
void EventListener::ExecuteHotkeyCallback(const HotKey &hotkey) {
  if (!hotkey.callback)
    return;

  // Prevent double execution of the same hotkey
  {
    std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
    if (executingHotkeys.find(hotkey.alias) != executingHotkeys.end()) {
      debug("Hotkey '{}' already executing, skipping", hotkey.alias);
      return;
    }
    executingHotkeys.insert(hotkey.alias);
  }

  // Create a copy of the callback and metadata BEFORE releasing locks
  // This prevents deadlock where callback tries to acquire locks we hold
  auto callback_copy = hotkey.callback;
  std::string alias_copy = hotkey.alias;

  // Use HotkeyExecutor if available for thread-safe execution
  if (hotkeyExecutor) {
    auto result = hotkeyExecutor->submit([callback = callback_copy,
                                          hotkeyAlias = alias_copy, this]() {
      try {
        info("Executing hotkey callback via HotkeyExecutor: {}", hotkeyAlias);
        callback();
      } catch (const std::exception &e) {
        error("Hotkey '{}' threw: {}", hotkeyAlias, e.what());
      } catch (...) {
        error("Hotkey '{}' threw unknown exception", hotkeyAlias);
      }

      // Remove from executing set when done
      {
        std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
        executingHotkeys.erase(hotkeyAlias);
      }
    });

    if (!result.accepted) {
      warn("Hotkey task queue full, dropping callback: {}", alias_copy);
      // Remove from executing set since we won't execute
      {
        std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
        executingHotkeys.erase(alias_copy);
      }
    }
    return;
  }

  // Fallback: Use detached thread if HotkeyExecutor not available
  pendingCallbacks++;

  std::thread([callback = callback_copy, alias = alias_copy, this]() {
    try {
      if (running.load() && !shutdown.load()) {
        // Thread runs OUTSIDE of mutex locks
        callback();
      }
    } catch (const std::exception &e) {
      error("Hotkey '{}' exception: {}", alias, e.what());
    }
    pendingCallbacks--;

    // Remove from executing set when done
    {
      std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
      executingHotkeys.erase(alias);
    }
  }).detach();
}
bool EventListener::EvaluateWheelCombo(const HotKey &hotkey,
                                       int wheelDirection) {
  auto now = std::chrono::steady_clock::now();

  debug("🔍 Evaluating wheel combo '{}'", hotkey.alias);

  // Check if the wheel event occurred recently enough (unless time window is 0
  // for infinite)
  if (comboTimeWindow > 0) {
    auto wheelTime = (wheelDirection > 0) ? lastWheelUpTime : lastWheelDownTime;
    auto wheelAge =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - wheelTime)
            .count();

    if (wheelAge > comboTimeWindow) {
      debug("❌ Wheel combo '{}' failed: wheel event too old ({}ms)",
            hotkey.alias, wheelAge);
      return false;
    }
  }

  for (const auto &comboKey : hotkey.comboSequence) {
    if (comboKey.type == HotkeyType::MouseWheel) {
      // Check wheel direction matches
      if (comboKey.wheelDirection != 0 &&
          comboKey.wheelDirection != wheelDirection) {
        debug("❌ Wheel combo '{}' failed: wrong direction", hotkey.alias);
        return false;
      }
      // Wheel part matched, continue
      continue;
    }

    // For keyboard/mouse button/mouse move parts, check if pressed
    int keyCode;
    if (comboKey.type == HotkeyType::MouseButton) {
      keyCode = comboKey.mouseButton;
    } else if (comboKey.type == HotkeyType::Keyboard ||
               comboKey.type == HotkeyType::MouseMove) {
      keyCode = static_cast<int>(comboKey.key);
    } else {
      return false;
    }

    // Check if key is currently pressed
    auto it = activeInputs.find(keyCode);
    if (it == activeInputs.end()) {
      debug("❌ Wheel combo '{}' failed: key {} not pressed", hotkey.alias,
            keyCode);
      return false;
    }

    // Check time window (unless time window is 0 for infinite)
    if (comboTimeWindow > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - it->second.timestamp)
                         .count();

      if (elapsed > comboTimeWindow) {
        debug("⏱️  Wheel combo '{}' failed: key {} too old ({}ms)", hotkey.alias,
              keyCode, elapsed);
        return false;
      }
    }

    // Check modifiers
    if (comboKey.modifiers != 0) {
      if (!CheckModifierMatch(comboKey.modifiers, comboKey.wildcard)) {
        debug("❌ Wheel combo '{}' failed: modifiers don't match",
              hotkey.alias);
        return false;
      }
    }
  }

  debug("✅ Wheel combo '{}' MATCHED", hotkey.alias);
  return true;
}
/**
 * Process mouse event - Handles mouse buttons, movement, and wheel
 *
 * This function processes all mouse-related events:
 * - EV_KEY: Mouse button presses/releases (BTN_LEFT, BTN_RIGHT, etc.)
 * - EV_REL: Relative movement (REL_X, REL_Y) and wheel (REL_WHEEL, REL_HWHEEL)
 * - EV_ABS: Absolute positioning (touchpads, joystick axes)
 *
 * For each event type, it:
 * 1. Updates internal state (button states, active inputs for combos)
 * 2. Evaluates registered hotkeys (button hotkeys, wheel hotkeys, combos)
 * 3. Applies sensitivity scaling (mouse movement, scroll speed)
 * 4. Forwards events to uinput (unless blocked by a grabbed hotkey)
 */
void EventListener::ProcessMouseEvent(const input_event &ev) {
  // Notify that input was received (for watchdog)
  if (inputNotificationCallback) {
    inputNotificationCallback();
  }

  bool shouldBlock = false;
  auto now = std::chrono::steady_clock::now();

  if (ev.type == EV_KEY) {
    // Mouse button event
    bool down = (ev.value == 1 || ev.value == 2);

    // Update button state and active inputs for combos
    {
      std::unique_lock<std::shared_mutex> lock(stateMutex);
      mouseButtonState[ev.code] = down;

      if (down) {
        int currentMods = GetCurrentModifiersMask();
        activeInputs[ev.code] = ActiveInput(currentMods, now);
        // Track physical mouse button state as well
        physicalKeyStates[ev.code] = true;
        debug("🖱️  Mouse BUTTON DOWN: code={} | Active buttons: {}", ev.code,
              GetActiveInputsString());
      } else {
        activeInputs.erase(ev.code);
        // Clear physical mouse button state as well
        physicalKeyStates[ev.code] = false;
        debug("🖱️  Mouse BUTTON UP: code={} | Active buttons: {}", ev.code,
              GetActiveInputsString());
      }
    } // stateMutex unlocked here

    // Collect matched hotkeys to avoid holding locks during callback execution
    std::vector<int> matchedHotkeyIds;
    bool shouldBlock = false;

    // Evaluate hotkeys - lock both mutexes
    {
      std::unique_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
      std::unique_lock<std::shared_mutex> stateLock(stateMutex);

      for (auto &[id, hotkey] : IO::hotkeys) {
        if (!hotkey.enabled)
          continue;

        // Handle combo hotkeys
        if (hotkey.type == HotkeyType::Combo) {
          try {
            if (EvaluateCombo(hotkey)) {
              matchedHotkeyIds.push_back(id); // Just store ID
              if (hotkey.grab)
                shouldBlock = true;
            }
          } catch (const std::system_error &e) {
            error("System error evaluating mouse combo '{}': {}", hotkey.alias,
                  e.what());
            // Continue with other hotkeys instead of crashing
            continue;
          } catch (const std::exception &e) {
            error("Exception evaluating mouse combo '{}': {}", hotkey.alias,
                  e.what());
            // Continue with other hotkeys instead of crashing
            continue;
          }
          continue;
        }

        // Handle mouse button hotkeys
        if (hotkey.type != HotkeyType::MouseButton)
          continue;
        if (hotkey.mouseButton != ev.code)
          continue;

        // Event type check
        if (hotkey.eventType == HotkeyEventType::Down && !down)
          continue;
        if (hotkey.eventType == HotkeyEventType::Up && down)
          continue;

        // Modifier matching: ignore modifiers unless explicitly set
        bool modifierMatch =
            (hotkey.modifiers == 0)
                ? true
                : CheckModifierMatch(hotkey.modifiers, hotkey.wildcard);

        if (!modifierMatch)
          continue;

        // Context checks (all must pass)
        if (!hotkey.contexts.empty()) {
          bool contextMatch =
              std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                          [](auto &ctx) { return ctx(); });
          if (!contextMatch)
            continue;
        }

        // Hotkey matched!
        info("Mouse button hotkey: '{}' button={} down={}", hotkey.alias,
             ev.code, down);
        matchedHotkeyIds.push_back(id); // Just store ID
        if (hotkey.grab)
          shouldBlock = true;
      }
    } // Both locks released here

    // Execute callbacks outside critical section
    for (int hotkeyId : matchedHotkeyIds) {
      std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
      auto it = IO::hotkeys.find(hotkeyId);

      if (it != IO::hotkeys.end() && it->second.enabled) {
        auto callback = it->second.callback; // Copy just the callback
        lock.unlock();                       // Release before executing

        std::thread([callback]() { callback(); }).detach();
      }
    }

    // Forward if not blocked
    if (!shouldBlock && !blockInput.load()) {
      SendUinputEvent(EV_KEY, ev.code, ev.value);
    } else if (!down) {
      // Always release to prevent stuck buttons
      SendUinputEvent(EV_KEY, ev.code, 0);
    }

  } else if (ev.type == EV_REL) {
    // Mouse movement
    if (ev.code == REL_X || ev.code == REL_Y) {
      double scaledValue = ev.value * IO::mouseSensitivity;
      debug("🖱️  Mouse MOVE: axis={}, value={}, scaled={}, sensitivity={}",
            ev.code == REL_X ? "X" : "Y", ev.value, scaledValue,
            IO::mouseSensitivity);
      int32_t scaledInt = static_cast<int32_t>(scaledValue);
      if (mouseMovementCallback) {
        if (ev.code == REL_X) {
          mouseMovementCallback(scaledInt, 0); // dx, dy=0
        } else if (ev.code == REL_Y) {
          mouseMovementCallback(0, scaledInt); // dx=0, dy
        }
      }
      // Process mouse gesture if we have gesture hotkeys registered
      if (!gestureHotkeys.empty()) {
        auto now = std::chrono::steady_clock::now();

        // Add movement to the gesture buffer
        MouseMovement movement;
        movement.time = now;

        if (ev.code == REL_X) {
          movement.dx = scaledInt;
          movement.dy = 0;
        } else if (ev.code == REL_Y) {
          movement.dx = 0;
          movement.dy = scaledInt;
        }

        // Add movement to buffer
        gestureBuffer.push_back(movement);

        // Keep only recent movements (in the last 50ms)
        auto cutoffTime = now - std::chrono::milliseconds(50);
        gestureBuffer.erase(
            std::remove_if(gestureBuffer.begin(), gestureBuffer.end(),
                           [cutoffTime](const MouseMovement &m) {
                             return m.time < cutoffTime;
                           }),
            gestureBuffer.end());

        // If we have a recent X and Y movement, combine them for gesture
        // processing
        int combinedX = 0, combinedY = 0;
        for (const auto &m : gestureBuffer) {
          combinedX += m.dx;
          combinedY += m.dy;
        }

        // Process the combined movement if it's significant enough
        if (std::abs(combinedX) > 5 || std::abs(combinedY) > 5) {
          ProcessMouseGesture(combinedX, combinedY);

          // Clear the buffer after processing
          gestureBuffer.clear();
        }
      }

      // ✅ ADD THIS: Queue movement as hotkey triggers (async processing)
      const int threshold = 5; // Minimum movement to trigger

      if (ev.code == REL_X && std::abs(scaledInt) >= threshold) {
        int virtualKey =
            (scaledInt > 0) ? 10002 : 10001; // mouseright : mouseleft
        QueueMouseMovementHotkey(virtualKey);
      } else if (ev.code == REL_Y && std::abs(scaledInt) >= threshold) {
        int virtualKey = (scaledInt > 0) ? 10004 : 10003; // mousedown : mouseup
        QueueMouseMovementHotkey(virtualKey);
      }

      if (!blockInput.load()) {
        SendUinputEvent(EV_REL, ev.code, scaledInt);
      }

      // Track mouse position from relative movements for Wayland support
      if (ev.code == REL_X) {
        currentMouseX += scaledInt;
      } else if (ev.code == REL_Y) {
        currentMouseY += scaledInt;
      }
    }
    // Mouse wheel
    else if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
      int wheelDirection = (ev.value > 0) ? 1 : -1;
      debug("🖱️  Mouse WHEEL: axis={}, direction={}, speed={}",
            ev.code == REL_WHEEL ? "VERT" : "HORZ",
            wheelDirection > 0 ? "UP/LEFT" : "DOWN/RIGHT", IO::scrollSpeed);

      // Update the wheel time tracking for combo evaluation and evaluate
      // hotkeys
      std::vector<int> wheelHotkeyIds;
      bool wheelHotkeyShouldBlock = false;

      {
        std::unique_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
        std::unique_lock<std::shared_mutex> stateLock(stateMutex);

        // Set wheel event context for combo evaluation
        isProcessingWheelEvent = true;
        currentWheelDirection = wheelDirection;

        // Update the wheel time tracking for combo evaluation
        auto now = std::chrono::steady_clock::now();
        if (wheelDirection > 0) {
          lastWheelUpTime = now;
        } else {
          lastWheelDownTime = now;
        }

        for (auto &[id, hotkey] : IO::hotkeys) {
          if (!hotkey.enabled)
            continue;
          if (hotkey.type == HotkeyType::Combo) {
            // Check if this combo involves a wheel
            bool hasWheel =
                std::any_of(hotkey.comboSequence.begin(),
                            hotkey.comboSequence.end(), [](const HotKey &k) {
                              return k.type == HotkeyType::MouseWheel;
                            });

            // For wheel combos, use the existing EvaluateWheelCombo function
            // For other combos that don't require wheel, they won't be
            // evaluated anyway due to the gate in EvaluateCombo
            if (hasWheel && EvaluateWheelCombo(hotkey, wheelDirection)) {
              info("Wheel combo: '{}'", hotkey.alias);
              wheelHotkeyIds.push_back(id); // Store ID
              if (hotkey.grab)
                wheelHotkeyShouldBlock = true;
            } else if (!hasWheel && hotkey.requiresWheel) {
              // This shouldn't happen, but just in case - evaluate normally if
              // it has the flag
              if (EvaluateCombo(hotkey)) {
                info("Non-wheel combo with requiresWheel flag: '{}'",
                     hotkey.alias);
                wheelHotkeyIds.push_back(id); // Store ID
                if (hotkey.grab)
                  wheelHotkeyShouldBlock = true;
              }
            }
            continue;
          }
          if (hotkey.type != HotkeyType::MouseWheel)
            continue;

          // Match wheel direction (0 = wildcard, matches any)
          if (hotkey.wheelDirection != 0 &&
              hotkey.wheelDirection != wheelDirection)
            continue;

          // Modifier matching
          if (!CheckModifierMatch(hotkey.modifiers, hotkey.wildcard))
            continue;

          // Context checks (any must pass - OR logic)
          if (!hotkey.contexts.empty()) {
            bool contextMatch =
                std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                            [](auto &ctx) { return ctx(); });
            if (!contextMatch)
              continue;
          }

          // Hotkey matched!
          info("Wheel hotkey: '{}' dir={}", hotkey.alias, wheelDirection);
          wheelHotkeyIds.push_back(id); // Store ID

          if (hotkey.grab)
            wheelHotkeyShouldBlock = true;
        }
      } // Both locks released here

      // Reset wheel event context after processing
      isProcessingWheelEvent = false;
      currentWheelDirection = 0;

      // Execute callbacks outside critical section
      for (int hotkeyId : wheelHotkeyIds) {
        HotKey hotkeyCopy;
        bool found = false;

        {
          std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
          auto it = IO::hotkeys.find(hotkeyId);

          if (it != IO::hotkeys.end() && it->second.enabled) {
            hotkeyCopy = it->second; // Copy just for execution
            found = true;
          }
        }

        if (found) {
          ExecuteHotkeyCallback(hotkeyCopy); // Use existing method
        }
      }

      // Update shouldBlock if any hotkey requested to grab
      if (wheelHotkeyShouldBlock) {
        shouldBlock = true;
      }

      // Apply scroll speed and forward if not blocked
      if (!shouldBlock && !blockInput.load()) {
        double scaledValue = ev.value * IO::scrollSpeed;
        int32_t scaledInt = static_cast<int32_t>(std::round(scaledValue));

        if (scaledInt == 0 && ev.value != 0 && IO::scrollSpeed >= 1.0) {
          scaledInt = (ev.value > 0) ? 1 : -1;
        }

        debug("Forwarding wheel: raw={} scaled={} blocked={} scrollSpeed={}",
              ev.value, scaledInt, shouldBlock, IO::scrollSpeed);
        SendUinputEvent(EV_REL, ev.code, scaledInt);
      } else {
        debug("Wheel BLOCKED");
      }
    }
    // Other relative events
    else {
      if (!blockInput.load()) {
        SendUinputEvent(ev.type, ev.code, ev.value);
      }
    }

  } else if (ev.type == EV_ABS) {
    // Absolute positioning or joystick axes
    // Track mouse position for Wayland support
    if (ev.code == ABS_X) {
      currentMouseX = ev.value;
    } else if (ev.code == ABS_Y) {
      currentMouseY = ev.value;
    }
    if (!blockInput.load()) {
      SendUinputEvent(ev.type, ev.code, ev.value);
    }
  }
}

void EventListener::EvaluateMouseMovementHotkeys(int virtualKey) {
  std::vector<int> matchedHotkeyIds;

  {
    std::shared_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
    std::shared_lock<std::shared_mutex> stateLock(stateMutex);

    for (auto &[id, hotkey] : IO::hotkeys) {
      if (!hotkey.enabled)
        continue;

      // Only match keyboard and mouse movement hotkeys with our virtual
      // movement keys
      if (hotkey.type != HotkeyType::Keyboard &&
          hotkey.type != HotkeyType::MouseMove)
        continue;

      // Check if the key matches
      if (static_cast<int>(hotkey.key) != virtualKey)
        continue;

      // Modifier matching
      if (hotkey.modifiers != 0) {
        if (!CheckModifierMatch(hotkey.modifiers, hotkey.wildcard))
          continue;
      }

      // Context checks
      if (!hotkey.contexts.empty()) {
        bool contextMatch =
            std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                        [](auto &ctx) { return ctx(); });
        if (!contextMatch)
          continue;
      }

      // Match!
      matchedHotkeyIds.push_back(id);
    }
  }

  // Execute callbacks asynchronously to avoid blocking input thread
  for (int hotkeyId : matchedHotkeyIds) {
    std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
    auto it = IO::hotkeys.find(hotkeyId);

    if (it != IO::hotkeys.end() && it->second.enabled) {
      auto callback = it->second.callback;
      lock.unlock();

      // Execute callback in a separate thread to avoid blocking input
      std::thread([callback]() { callback(); }).detach();
    }
  }
}

void EventListener::QueueMouseMovementHotkey(int virtualKey) {
  // Check rate limiting to avoid flooding the queue
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - lastMovementHotkeyTime)
                     .count();

  // Only process at most every 10ms to avoid overwhelming the system
  if (elapsed < 10) {
    return;
  }
  lastMovementHotkeyTime = now;

  // Add to queue if not already processing and queue isn't too large
  {
    std::unique_lock<std::shared_mutex> lock(movementHotkeyMutex);
    if (queuedMovementHotkeys.size() < 10) { // Prevent excessive queuing
      queuedMovementHotkeys.push(virtualKey);
    }
  }

  // Process queued hotkeys asynchronously if not already processing
  if (!movementHotkeyProcessing.exchange(true)) {
    // Process in a separate thread to avoid blocking the input thread
    std::thread([this]() {
      ProcessQueuedMouseMovementHotkeys();
      movementHotkeyProcessing.store(false);
    }).detach();
  }
}

void EventListener::ProcessQueuedMouseMovementHotkeys() {
  std::vector<int> hotkeysToProcess;

  // Extract all queued hotkeys under lock
  {
    std::unique_lock<std::shared_mutex> lock(movementHotkeyMutex);
    while (!queuedMovementHotkeys.empty()) {
      hotkeysToProcess.push_back(queuedMovementHotkeys.front());
      queuedMovementHotkeys.pop();
    }
  }

  // Process unique hotkeys only (avoid duplicate processing)
  std::sort(hotkeysToProcess.begin(), hotkeysToProcess.end());
  hotkeysToProcess.erase(
      std::unique(hotkeysToProcess.begin(), hotkeysToProcess.end()),
      hotkeysToProcess.end());

  // Process each unique hotkey
  for (int virtualKey : hotkeysToProcess) {
    EvaluateMouseMovementHotkeys(virtualKey);
  }
}

void EventListener::UpdateModifierState(int evdevCode, bool down) {
  // This is called with stateMutex already locked

  // Check if this key is remapped - if so, update modifier state based on
  // target
  int effectiveCode = evdevCode;
  auto remapIt = keyRemaps.find(evdevCode);
  if (remapIt != keyRemaps.end()) {
    effectiveCode = remapIt->second;
  }

  if (effectiveCode == KEY_LEFTCTRL) {
    modifierState.leftCtrl = down;
  } else if (effectiveCode == KEY_RIGHTCTRL) {
    modifierState.rightCtrl = down;
  } else if (effectiveCode == KEY_LEFTSHIFT) {
    modifierState.leftShift = down;
  } else if (effectiveCode == KEY_RIGHTSHIFT) {
    modifierState.rightShift = down;
  } else if (effectiveCode == KEY_LEFTALT) {
    modifierState.leftAlt = down;
  } else if (effectiveCode == KEY_RIGHTALT) {
    modifierState.rightAlt = down;
  } else if (effectiveCode == KEY_LEFTMETA) {
    modifierState.leftMeta = down;
  } else if (effectiveCode == KEY_RIGHTMETA) {
    modifierState.rightMeta = down;
  }
}

bool EventListener::CheckModifierMatch(int requiredModifiers,
                                       bool wildcard) const {
  // This is called with stateMutex already locked

  bool ctrlRequired = (requiredModifiers & (1 << 0)) != 0;
  bool shiftRequired = (requiredModifiers & (1 << 1)) != 0;
  bool altRequired = (requiredModifiers & (1 << 2)) != 0;
  bool metaRequired = (requiredModifiers & (1 << 3)) != 0;

  bool ctrlPressed = modifierState.IsCtrlPressed();
  bool shiftPressed = modifierState.IsShiftPressed();
  bool altPressed = modifierState.IsAltPressed();
  bool metaPressed = modifierState.IsMetaPressed();

  if (wildcard) {
    // Wildcard: only check that REQUIRED modifiers are pressed
    return (!ctrlRequired || ctrlPressed) && (!shiftRequired || shiftPressed) &&
           (!altRequired || altPressed) && (!metaRequired || metaPressed);
  } else {
    // Normal: exact modifier match
    return (ctrlRequired == ctrlPressed) && (shiftRequired == shiftPressed) &&
           (altRequired == altPressed) && (metaRequired == metaPressed);
  }
}

bool EventListener::CheckModifierMatchExcludingModifier(
    int requiredModifiers, bool wildcard, int excludeModifier) const {
  // This is used when a key is remapped to a modifier (e.g., CapsLock -> LAlt)
  // We need to check modifiers while excluding the remapped-to modifier
  // This is called with stateMutex already locked

  bool ctrlRequired = (requiredModifiers & (1 << 0)) != 0;
  bool shiftRequired = (requiredModifiers & (1 << 1)) != 0;
  bool altRequired = (requiredModifiers & (1 << 2)) != 0;
  bool metaRequired = (requiredModifiers & (1 << 3)) != 0;

  // Get the current modifier state, but exclude the remapped-to modifier
  bool ctrlPressed = modifierState.IsCtrlPressed();
  bool shiftPressed = modifierState.IsShiftPressed();
  bool altPressed = modifierState.IsAltPressed();
  bool metaPressed = modifierState.IsMetaPressed();

  // Remove the contribution of the remapped modifier
  if (excludeModifier == KEY_LEFTCTRL || excludeModifier == KEY_RIGHTCTRL) {
    ctrlPressed = false; // Assume only this key contributes to Ctrl
  } else if (excludeModifier == KEY_LEFTSHIFT ||
             excludeModifier == KEY_RIGHTSHIFT) {
    shiftPressed = false; // Assume only this key contributes to Shift
  } else if (excludeModifier == KEY_LEFTALT ||
             excludeModifier == KEY_RIGHTALT) {
    altPressed = false; // Assume only this key contributes to Alt
  } else if (excludeModifier == KEY_LEFTMETA ||
             excludeModifier == KEY_RIGHTMETA) {
    metaPressed = false; // Assume only this key contributes to Meta
  }

  if (wildcard) {
    // Wildcard: only check that REQUIRED modifiers are pressed (excluding the
    // remapped one)
    return (!ctrlRequired || ctrlPressed) && (!shiftRequired || shiftPressed) &&
           (!altRequired || altPressed) && (!metaRequired || metaPressed);
  } else {
    // Normal: exact modifier match (excluding the remapped one)
    return (ctrlRequired == ctrlPressed) && (shiftRequired == shiftPressed) &&
           (altRequired == altPressed) && (metaRequired == metaPressed);
  }
}

bool EventListener::EvaluateHotkeys(int evdevCode, bool down, bool repeat) {
  std::vector<int> matchedHotkeyIds;
  bool shouldBlock = false;

  // Check emergency shutdown key
  if (down && emergencyShutdownKey != 0 && evdevCode == emergencyShutdownKey) {
    error("🚨 EMERGENCY HOTKEY TRIGGERED! Shutting down...");
    running.store(false);
    shutdown.store(true);
    return true;
  }

  // Evaluate hotkeys with locks held, but collect matches to execute callbacks
  // outside locks
  {
    std::shared_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
    std::shared_lock<std::shared_mutex> stateLock(stateMutex);

    for (auto &[id, hotkey] : IO::hotkeys) {
      if (!hotkey.enabled || !hotkey.evdev) {
        continue;
      }

      // Check if this is a combo hotkey
      if (hotkey.type == HotkeyType::Combo) {
        try {
          if (EvaluateCombo(hotkey)) {
            // Combo matched, collect for execution outside locks
            matchedHotkeyIds.push_back(id);
            if (hotkey.grab) {
              shouldBlock = true;
            }
          }
        } catch (const std::system_error &e) {
          error("System error evaluating hotkey combo '{}': {}", hotkey.alias,
                e.what());
          // Continue with other hotkeys instead of crashing
          continue;
        } catch (const std::exception &e) {
          error("Exception evaluating hotkey combo '{}': {}", hotkey.alias,
                e.what());
          // Continue with other hotkeys instead of crashing
          continue;
        }
        continue;
      }

      // Match against key code
      if (hotkey.key != static_cast<Key>(evdevCode)) {
        continue;
      }

      // Guard: Modifiers should not trigger standalone hotkeys (unless in combo
      // context) This prevents Shift/RShift from triggering zoom when combined
      // with wheel events However, we need to allow single modifier hotkeys to
      // work, so we'll check more carefully
      if (KeyMap::IsModifier(static_cast<int>(hotkey.key)) &&
          hotkey.type != HotkeyType::Combo) {
        // Allow single modifier hotkeys to work - if the hotkey is for the
        // modifier itself and no additional modifiers are required, then it
        // should be allowed
        if (static_cast<int>(hotkey.key) != evdevCode ||
            hotkey.modifiers != 0) {
          continue;
        }
      }

      // Event type check
      if (!hotkey.repeat && repeat) {
        continue;
      }

      if (hotkey.eventType == HotkeyEventType::Down && !down) {
        continue;
      }
      if (hotkey.eventType == HotkeyEventType::Up && down) {
        continue;
      }
      // Modifier matching
      bool isModifierKey =
          (evdevCode == KEY_LEFTALT || evdevCode == KEY_RIGHTALT ||
           evdevCode == KEY_LEFTCTRL || evdevCode == KEY_RIGHTCTRL ||
           evdevCode == KEY_LEFTSHIFT || evdevCode == KEY_RIGHTSHIFT ||
           evdevCode == KEY_LEFTMETA || evdevCode == KEY_RIGHTMETA);

      // Check if this key is remapped to a modifier (e.g., CapsLock -> LAlt)
      // If so, we should treat it like a modifier key for matching purposes
      bool keyRemappedToModifier = false;
      int remappedTarget = evdevCode;
      {
        std::lock_guard<std::mutex> remapLock(remapMutex);
        auto it = keyRemaps.find(evdevCode);
        if (it != keyRemaps.end()) {
          remappedTarget = it->second;
          keyRemappedToModifier = (remappedTarget == KEY_LEFTALT ||
                                   remappedTarget == KEY_RIGHTALT ||
                                   remappedTarget == KEY_LEFTCTRL ||
                                   remappedTarget == KEY_RIGHTCTRL ||
                                   remappedTarget == KEY_LEFTSHIFT ||
                                   remappedTarget == KEY_RIGHTSHIFT ||
                                   remappedTarget == KEY_LEFTMETA ||
                                   remappedTarget == KEY_RIGHTMETA);
        }
      }

      bool modifierMatch;

      // If the HOTKEY itself is a modifier key, skip modifier comparisons.
      // The pressed modifier should ALWAYS trigger its own hotkey.
      // This also applies if the key is remapped to a modifier.
      if ((isModifierKey || keyRemappedToModifier) && hotkey.modifiers == 0) {
        modifierMatch = true;
      } else {
        // For keys remapped to modifiers, ignore that modifier in the match
        // check by using the original modifiers before the remap was applied
        if (keyRemappedToModifier) {
          // Check modifiers against the state BEFORE this key's remap was
          // applied We need to exclude the remapped modifier from the check
          modifierMatch = CheckModifierMatchExcludingModifier(
              hotkey.modifiers, hotkey.wildcard, remappedTarget);
        } else {
          modifierMatch = CheckModifierMatch(hotkey.modifiers, hotkey.wildcard);
        }
      }

      if (!modifierMatch)
        continue;

      // Context checks
      if (!hotkey.contexts.empty()) {
        bool contextMatch =
            std::any_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                        [](auto &ctx) { return ctx(); });
        if (!contextMatch) {
          continue;
        }
      }

      // Check repeat interval
      if (hotkey.repeatInterval > 0 && repeat) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - hotkey.lastTriggerTime)
                           .count();

        if (elapsed < hotkey.repeatInterval) {
          continue;
        }
        hotkey.lastTriggerTime = now;
      } else if (down && !repeat) {
        hotkey.lastTriggerTime = std::chrono::steady_clock::now();
      }

      // Hotkey matched! Collect for execution outside locks
      hotkey.success = true; // Update the actual hotkey's success status
      debug("Hotkey {} triggered, key: {}, modifiers: {}, down: {}, repeat: {}",
            hotkey.alias, hotkey.key, hotkey.modifiers, down, repeat);

      matchedHotkeyIds.push_back(id);

      if (hotkey.grab) {
        shouldBlock = true;
      }
    }
  } // Locks released here

  // Execute callbacks outside critical section
  for (int hotkeyId : matchedHotkeyIds) {
    HotKey hotkeyCopy;
    bool found = false;

    {
      std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
      auto it = IO::hotkeys.find(hotkeyId);

      if (it != IO::hotkeys.end() && it->second.enabled) {
        hotkeyCopy = it->second; // Copy just for execution
        found = true;
      }
    }

    if (found) {
      ExecuteHotkeyCallback(hotkeyCopy); // Use existing method
    }
  }

  return shouldBlock;
}

/**
 * Evaluate combo hotkey - Checks if all keys in a combo are currently pressed
 *
 * Combos use the & operator to require multiple keys/buttons pressed
 * simultaneously:
 * - "LButton & RButton" - Both mouse buttons pressed together
 * - "CapsLock & W" - CapsLock and W pressed together
 * - "JoyA & JoyB" - Gamepad buttons A and B pressed together
 *
 * The combo is considered active if:
 * 1. All keys/buttons in the combo are currently pressed
 * 2. All were pressed within the time window (default 500ms)
 *
 * This allows for natural combo input where keys don't need to be pressed
 * at exactly the same instant, but within a short time window.
 */
bool EventListener::EvaluateCombo(const HotKey &hotkey) {
  try {
    // stateMutex is already locked by the caller (ProcessMouseEvent or
    // ProcessKeyboardEvent) std::shared_lock<std::shared_mutex>
    // stateLock(stateMutex);

    auto now = std::chrono::steady_clock::now();

    // Gate: If this combo requires a wheel event, only evaluate during wheel
    // events
    if (hotkey.requiresWheel && !isProcessingWheelEvent) {
      debug("⏭️  Skipping combo '{}' - requires wheel but not processing wheel "
            "event",
            hotkey.alias);
      return false;
    }

    debug("🔍 Evaluating combo '{}' | Active inputs: {}", hotkey.alias,
          GetActiveInputsString());

    // Build a set of required keys for this combo
    std::set<int> requiredKeys;
    int requiredModifiers = 0;

    for (const auto &comboKey : hotkey.comboSequence) {
      if (comboKey.type == HotkeyType::Keyboard) {
        int keyCode = static_cast<int>(comboKey.key);

        // Check if this is a modifier
        if (KeyMap::IsModifier(keyCode)) {
          // Track as modifier using the EventListener's Modifier enum
          if (keyCode == KEY_LEFTCTRL || keyCode == KEY_RIGHTCTRL) {
            requiredModifiers |= Modifier::Ctrl;
          } else if (keyCode == KEY_LEFTSHIFT || keyCode == KEY_RIGHTSHIFT) {
            requiredModifiers |= Modifier::Shift;
          } else if (keyCode == KEY_LEFTALT || keyCode == KEY_RIGHTALT) {
            requiredModifiers |= Modifier::Alt;
          } else if (keyCode == KEY_LEFTMETA || keyCode == KEY_RIGHTMETA) {
            requiredModifiers |= Modifier::Meta;
          }
        } else {
          // Regular key
          requiredKeys.insert(keyCode);
        }
      } else if (comboKey.type == HotkeyType::MouseButton) {
        requiredKeys.insert(comboKey.mouseButton);
      } else if (comboKey.type == HotkeyType::MouseMove) {
        requiredKeys.insert(static_cast<int>(comboKey.key));
      } else if (comboKey.type == HotkeyType::MouseWheel) {
        // ← ADD THIS CASE
        // Wheel events are TRANSIENT - they don't stay in activeInputs
        // They only trigger DURING the wheel event itself
        // So we just skip them in the activeInputs check
        // The wheel direction matching happens in ProcessMouseEvent
        debug("Combo includes wheel event, skipping activeInputs check for it");
        continue;
      } else {
        debug("❌ Combo '{}' has unsupported type {}", hotkey.alias,
              static_cast<int>(comboKey.type));
        return false;
      }
    }

    // Check if EXACTLY the required keys are active (no more, no less)
    // unless wildcard is enabled
    int activeCount = activeInputs.size();
    int requiredCount = requiredKeys.size();

    // Count how many modifier keys are active
    int activeModifierKeys = 0;
    for (const auto &[code, input] : activeInputs) {
      if (KeyMap::IsModifier(code)) {
        activeModifierKeys++;
      }
    }

    if (!hotkey.wildcard) {
      // Strict matching: ensure no unauthorized keys are pressed
      // We iterate active inputs and verify each one is allowed
      // (either required, a modifier, or a shadow/phantom of a required key)

      std::unique_lock<std::mutex> remapLock(
          remapMutex); // Lock for keyRemaps access

      for (const auto &[code, input] : activeInputs) {
        // 1. Modifiers are handled by modifier matching logic, so we ignore
        // them here (Assuming modifier check logic handles strictness if
        // needed)
        if (KeyMap::IsModifier(code))
          continue;

        // 2. If it's explicitly required, it's allowed
        if (requiredKeys.count(code))
          continue;

        // 3. Check for phantom/shadow keys due to remapping
        // Case A: 'code' is a mapped version of a required key (original ->
        // code)
        bool isShadow = false;
        for (int reqKey : requiredKeys) {
          auto it = keyRemaps.find(reqKey);
          if (it != keyRemaps.end() && it->second == code) {
            isShadow = true;
            break;
          }
        }
        if (isShadow)
          continue;

        // Case B: 'code' is the original version of a required key (code ->
        // mapped) (i.e. we matched on the mapped key, but the original is also
        // in activeInputs)
        auto it = keyRemaps.find(code);
        if (it != keyRemaps.end()) {
          if (requiredKeys.count(it->second)) {
            continue; // It's the original source of a required key
          }
        }

        // If we get here, 'code' is an extra key that is not allowed
        debug("❌ Combo '{}' rejected: unauthorized key {} active",
              hotkey.alias, code);
        return false;
      }
      // If we passed the loop, all active keys are allowed
    }

    // Check each required key is actually active
    for (int requiredKey : requiredKeys) {
      auto it = activeInputs.find(requiredKey);

      if (it == activeInputs.end()) {
        debug("❌ Combo '{}' rejected: required key {} not active",
              hotkey.alias, requiredKey);
        return false;
      }

      // Check timing if time window is set
      if (comboTimeWindow > 0) {
        auto keyAge = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.timestamp);

        if (keyAge.count() > comboTimeWindow) {
          debug("❌ Combo '{}' rejected: key {} too old ({}ms > {}ms)",
                hotkey.alias, requiredKey, keyAge.count(), comboTimeWindow);
          return false;
        }
      }
    }

    // Check specific physical keys are pressed (for precise modifier matching)
    // Skip this check for pure modifier+wheel combos to avoid conflicts with
    // modifier handling
    bool isPureModifierWheelCombo =
        hotkey.requiresWheel && requiredKeys.empty() && requiredModifiers != 0;

    if (!isPureModifierWheelCombo && !hotkey.requiredPhysicalKeys.empty()) {
      if (!ArePhysicalKeysPressed(hotkey.requiredPhysicalKeys)) {
        debug("❌ Combo '{}' rejected: required physical keys not pressed",
              hotkey.alias);
        return false;
      }
    }

    // Check modifiers if required
    if (requiredModifiers != 0) {
      int currentMods = GetCurrentModifiersMask();

      if (!hotkey.wildcard) {
        // Strict: must match exactly
        if (currentMods != requiredModifiers) {
          debug("❌ Combo '{}' rejected: wrong modifiers "
                "(have {:#x}, need {:#x})",
                hotkey.alias, currentMods, requiredModifiers);
          return false;
        }
      } else {
        // Wildcard: required mods must be present (can have extras)
        if ((currentMods & requiredModifiers) != requiredModifiers) {
          debug("❌ Combo '{}' rejected: missing required modifiers "
                "(have {:#x}, need {:#x})",
                hotkey.alias, currentMods, requiredModifiers);
          return false;
        }
      }
    }

    debug("✅ Combo '{}' matched!", hotkey.alias);
    return true;
  } catch (const std::system_error &e) {
    error("System error in EvaluateCombo for hotkey '{}': {}", hotkey.alias,
          e.what());
    return false; // Return false instead of crashing
  } catch (const std::exception &e) {
    error("Exception in EvaluateCombo for hotkey '{}': {}", hotkey.alias,
          e.what());
    return false; // Return false instead of crashing
  }
}

bool EventListener::ArePhysicalKeysPressed(
    const std::vector<int> &requiredKeys) const {
  for (int key : requiredKeys) {
    auto it = physicalKeyStates.find(key);
    if (it == physicalKeyStates.end() || !it->second) {
      return false;
    }
  }
  return true;
}

void EventListener::ProcessMouseGesture(int dx, int dy) {
  auto now = std::chrono::steady_clock::now();

  // If we don't have an active gesture, start one if movement is significant
  // enough
  if (!currentMouseGesture.isActive) {
    // Check if the movement is large enough to start a gesture
    double distance = std::sqrt(dx * dx + dy * dy);
    if (distance >= currentMouseGesture.minDistance) {
      currentMouseGesture.isActive = true;
      currentMouseGesture.startTime = now;
      currentMouseGesture.lastMoveTime = now;
      currentMouseGesture.totalDistance = 0;

      // Calculate the direction of this movement
      MouseGestureDirection direction = GetGestureDirection(dx, dy);
      currentMouseGesture.directions.push_back(direction);

      // Update the total distance
      currentMouseGesture.totalDistance += static_cast<int>(distance);
    }
  } else {
    // We have an active gesture, check for timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - currentMouseGesture.startTime)
                       .count();

    if (elapsed > currentMouseGesture.timeout) {
      // Reset gesture due to timeout
      ResetMouseGesture();
      return;
    }

    // Calculate the direction of this movement
    MouseGestureDirection direction = GetGestureDirection(dx, dy);

    // Check if this direction is different from the last one
    if (!currentMouseGesture.directions.empty() &&
        currentMouseGesture.directions.back() != direction) {
      currentMouseGesture.directions.push_back(direction);
    }

    // Update the total distance and last move time
    double distance = std::sqrt(dx * dx + dy * dy);
    currentMouseGesture.totalDistance += static_cast<int>(distance);
    currentMouseGesture.lastMoveTime = now;

    // Check for matching gestures
    for (const auto &[id, expectedDirections] : gestureHotkeys) {
      // Check if the current gesture matches any registered gesture
      if (MatchGesturePattern(expectedDirections,
                              currentMouseGesture.directions)) {
        // Find the hotkey and execute its callback
        std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
        auto hotkeyIt = IO::hotkeys.find(id);
        if (hotkeyIt != IO::hotkeys.end()) {
          const auto &hotkey = hotkeyIt->second;
          if (hotkey.enabled && hotkey.type == HotkeyType::MouseGesture) {
            ExecuteHotkeyCallback(hotkey);

            // Reset the gesture after successful match
            ResetMouseGesture();
            break;
          }
        }
      }
    }
  }
}

MouseGestureDirection EventListener::GetGestureDirection(int dx, int dy) const {
  // Calculate angle in degrees
  double angle = std::atan2(dy, dx) * (180.0 / M_PI);
  if (angle < 0)
    angle += 360.0;

  // Determine direction based on angle
  if (angle >= 337.5 || angle < 22.5)
    return MouseGestureDirection::Right; // 0°: Right
  if (angle >= 22.5 && angle < 67.5)
    return MouseGestureDirection::DownRight; // 45°: Down-Right
  if (angle >= 67.5 && angle < 112.5)
    return MouseGestureDirection::Down; // 90°: Down
  if (angle >= 112.5 && angle < 157.5)
    return MouseGestureDirection::DownLeft; // 135°: Down-Left
  if (angle >= 157.5 && angle < 202.5)
    return MouseGestureDirection::Left; // 180°: Left
  if (angle >= 202.5 && angle < 247.5)
    return MouseGestureDirection::UpLeft; // 225°: Up-Left
  if (angle >= 247.5 && angle < 292.5)
    return MouseGestureDirection::Up; // 270°: Up
  if (angle >= 292.5 && angle < 337.5)
    return MouseGestureDirection::UpRight; // 315°: Up-Right

  return MouseGestureDirection::Right; // Default fallback
}

bool EventListener::MatchGesturePattern(
    const std::vector<MouseGestureDirection> &expected,
    const std::vector<MouseGestureDirection> &actual) const {
  if (actual.size() < expected.size()) {
    return false; // Not enough directions yet
  }

  // Check if the end of actual matches the expected pattern
  size_t startIdx = actual.size() - expected.size();
  for (size_t i = 0; i < expected.size(); ++i) {
    if (actual[startIdx + i] != expected[i]) {
      return false;
    }
  }
  return true;
}

// Release all pressed virtual keys (for clean shutdown)
void EventListener::ReleaseAllVirtualKeys() {
  if (pressedVirtualKeys.empty())
    return;

  info("Releasing {} pressed virtual keys", pressedVirtualKeys.size());

  for (int code : pressedVirtualKeys) {
    SendUinputEvent(EV_KEY, code, 0);
  }

  // Send final SYN_REPORT
  SendUinputEvent(EV_SYN, SYN_REPORT, 0);

  pressedVirtualKeys.clear();
}

// Helper function to convert gesture pattern string to directions
std::vector<MouseGestureDirection>
EventListener::ParseGesturePattern(const std::string &patternStr) const {
  std::vector<MouseGestureDirection> directions;

  // Check if it's a predefined shape
  if (patternStr == "circle") {
    // A circle could be represented as: right, down, left, up (clockwise)
    directions = {MouseGestureDirection::Right, MouseGestureDirection::Down,
                  MouseGestureDirection::Left, MouseGestureDirection::Up};
  } else if (patternStr == "square") {
    // A square: right, down, left, up
    directions = {MouseGestureDirection::Right, MouseGestureDirection::Down,
                  MouseGestureDirection::Left, MouseGestureDirection::Up};
  } else if (patternStr == "triangle") {
    // A triangle: up-right, down-left, down
    directions = {MouseGestureDirection::UpRight,
                  MouseGestureDirection::DownLeft, MouseGestureDirection::Down};
  } else if (patternStr == "zigzag") {
    // A zigzag: right, down-left, right, up-left
    directions = {MouseGestureDirection::Right, MouseGestureDirection::DownLeft,
                  MouseGestureDirection::Right, MouseGestureDirection::UpLeft};
  } else if (patternStr == "check") {
    // A check mark: down-right, up-left
    directions = {MouseGestureDirection::DownRight,
                  MouseGestureDirection::UpRight};
  } else {
    // Parse comma-separated direction pattern
    std::string dir;
    std::istringstream iss(patternStr);

    while (std::getline(iss, dir, ',')) {
      // Remove leading/trailing whitespace
      dir.erase(0, dir.find_first_not_of(" \t\n\r"));
      dir.erase(dir.find_last_not_of(" \t\n\r") + 1);

      // Mouse-specific directions (to avoid conflicts with arrow keys)
      if (dir == "mouseup") {
        directions.push_back(MouseGestureDirection::Up);
      } else if (dir == "mousedown") {
        directions.push_back(MouseGestureDirection::Down);
      } else if (dir == "mouseleft") {
        directions.push_back(MouseGestureDirection::Left);
      } else if (dir == "mouseright") {
        directions.push_back(MouseGestureDirection::Right);
      }
      // Corner directions with mouse prefix
      else if (dir == "mouseupleft") {
        directions.push_back(MouseGestureDirection::UpLeft);
      } else if (dir == "mouseupright") {
        directions.push_back(MouseGestureDirection::UpRight);
      } else if (dir == "mousedownleft") {
        directions.push_back(MouseGestureDirection::DownLeft);
      } else if (dir == "mousedownright") {
        directions.push_back(MouseGestureDirection::DownRight);
      }
      // For backward compatibility, also support generic directions but only in
      // gesture contexts
      else if (dir == "up") {
        directions.push_back(MouseGestureDirection::Up);
      } else if (dir == "down") {
        directions.push_back(MouseGestureDirection::Down);
      } else if (dir == "left") {
        directions.push_back(MouseGestureDirection::Left);
      } else if (dir == "right") {
        directions.push_back(MouseGestureDirection::Right);
      }
      // Generic corner directions (for backward compatibility)
      else if (dir == "up-left" || dir == "upleft") {
        directions.push_back(MouseGestureDirection::UpLeft);
      } else if (dir == "up-right" || dir == "upright") {
        directions.push_back(MouseGestureDirection::UpRight);
      } else if (dir == "down-left" || dir == "downleft") {
        directions.push_back(MouseGestureDirection::DownLeft);
      } else if (dir == "down-right" || dir == "downright") {
        directions.push_back(MouseGestureDirection::DownRight);
      }
    }
  }

  return directions;
}

// Overloaded method that takes a HotKey and gets its pattern
std::vector<MouseGestureDirection>
EventListener::ParseGesturePattern(const HotKey &hotkey) const {
  return ParseGesturePattern(hotkey.gesturePattern);
}

// Helper function to validate gesture with tolerance
bool EventListener::IsGestureValid(
    const std::vector<MouseGestureDirection> &pattern, int minDistance) const {
  // For now, just check if we have any movement that meets the minimum distance
  return currentMouseGesture.totalDistance >= minDistance;
}

void EventListener::ResetMouseGesture() {
  currentMouseGesture.isActive = false;
  currentMouseGesture.directions.clear();
  currentMouseGesture.xPositions.clear();
  currentMouseGesture.yPositions.clear();
  currentMouseGesture.totalDistance = 0;
}

void EventListener::RegisterGestureHotkey(
    int id, const std::vector<MouseGestureDirection> &directions) {
  gestureHotkeys[id] = directions;
}

void EventListener::SetupSignalHandling() {
  // Initialize signal mask to block the signals we want to handle via signalfd
  sigemptyset(&signalMask);
  sigaddset(&signalMask, SIGTERM);
  sigaddset(&signalMask, SIGINT);
  sigaddset(&signalMask, SIGHUP);
  sigaddset(&signalMask, SIGQUIT);

  // Block these signals for all threads in the process
  if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) != 0) {
    error("Failed to block signals for signalfd");
    return;
  }

  // Create the signalfd
  signalFd = signalfd(-1, &signalMask, SFD_CLOEXEC);
  if (signalFd == -1) {
    error("Failed to create signalfd: {}", strerror(errno));
    return;
  }

  info("Signal handling set up with signalfd");
}

void EventListener::ProcessSignal() {
  if (signalFd < 0)
    return;

  struct signalfd_siginfo si;
  ssize_t s = read(signalFd, &si, sizeof(si));
  if (s != sizeof(si)) {
    error("Failed to read from signalfd: {}", strerror(errno));
    return;
  }

  int sig = si.ssi_signo;
  info("EventListener received signal: {}", sig);

  // Handle emergency cleanup immediately in the same thread
  switch (sig) {
  case SIGTERM:
  case SIGINT:
  case SIGHUP:
  case SIGQUIT:
    info("Emergency shutdown: Ungrabbing all devices immediately in "
         "EventListener thread");

    // Release all pressed virtual keys before ungrabbing devices
    ReleaseAllVirtualKeys();

    // Ungrab all input devices to release grabs
    for (auto &device : devices) {
      if (grabDevices && device.fd >= 0) {
        ioctl(device.fd, EVIOCGRAB, 0); // Ungrab device
      }
    }

    // Stop the event listener
    running.store(false);
    shutdown.store(true);

    // Signal shutdown
    if (shutdownFd >= 0) {
      uint64_t val = 1;
      write(shutdownFd, &val, sizeof(val));
    }

    info("Emergency shutdown complete in EventListener thread");
    break;
  default:
    // Other signals, just log
    info("Received unhandled signal: {}", sig);
    break;
  }
}

} // namespace havel
