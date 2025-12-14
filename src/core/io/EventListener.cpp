#include "EventListener.hpp"
#include "../IO.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <linux/uinput.h>
#include <shared_mutex>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

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
EventListener::EventListener() { shutdownFd = eventfd(0, EFD_NONBLOCK); }

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
      if (part.type == HotkeyType::Keyboard) {
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

  while (running.load() && !shutdown.load()) {
    fd_set readfds;
    FD_ZERO(&readfds);

    int maxFd = shutdownFd;
    FD_SET(shutdownFd, &readfds);

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

      debug("🔑 Key PRESS: original={} mapped={} | Modifiers: {}{}{}{}",
            originalCode, mappedCode,
            modifierState.IsCtrlPressed() ? "Ctrl+" : "",
            modifierState.IsShiftPressed() ? "Shift+" : "",
            modifierState.IsAltPressed() ? "Alt+" : "",
            modifierState.IsMetaPressed() ? "Meta+" : "");

      // CHECK ALL COMBOS THAT MIGHT INCLUDE THIS KEY
      // Loop through ALL hotkeys to find combos (more reliable than indexed lookup)
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
                  ? comboKey.mouseButton : -1;
          
          if (comboKeyCode == mappedCode || comboKeyCode == originalCode) {
            comboIncludesKey = true;
            break;
          }
        }
        
        // If this combo includes the key that was just pressed, evaluate it
        if (comboIncludesKey && EvaluateCombo(hotkey)) {
          debug("✅ Combo hotkey '{}' triggered on key press", hotkey.alias);
          ExecuteHotkeyCallback(hotkey);
        }
      }
    } else {
      // On key release, remove both original and mapped codes
      activeInputs.erase(mappedCode);
      if (mappedCode != originalCode) {
        activeInputs.erase(originalCode);
      }

      debug("🔑 Key RELEASE: original={} mapped={}", originalCode, mappedCode);
      
      // Note: comboPressedCount is removed in favor of activeInputs-based evaluation
      // EvaluateCombo() uses activeInputs map directly, which is more reliable
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

  // Create a copy of the callback and metadata BEFORE releasing locks
  // This prevents deadlock where callback tries to acquire locks we hold
  auto callback_copy = hotkey.callback;
  std::string alias_copy = hotkey.alias;

  // Increment pending count (must be done before thread spawns)
  pendingCallbacks++;

  // Spawn thread - it will have its own independent state
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

    // For keyboard/mouse button parts, check if pressed
    int keyCode;
    if (comboKey.type == HotkeyType::MouseButton) {
      keyCode = comboKey.mouseButton;
    } else if (comboKey.type == HotkeyType::Keyboard) {
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
        debug("🖱️  Mouse BUTTON DOWN: code={} | Active buttons: {}", ev.code,
              GetActiveInputsString());
      } else {
        activeInputs.erase(ev.code);
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
          if (EvaluateCombo(hotkey)) {
            matchedHotkeyIds.push_back(id);  // Just store ID
            if (hotkey.grab)
              shouldBlock = true;
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
        info("Mouse button hotkey: '{}' button={} down={}", hotkey.alias, ev.code,
             down);
        matchedHotkeyIds.push_back(id);  // Just store ID
        if (hotkey.grab)
          shouldBlock = true;
      }
    } // Both locks released here

    // Execute callbacks outside critical section
    for (int hotkeyId : matchedHotkeyIds) {
      std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
      auto it = IO::hotkeys.find(hotkeyId);
      
      if (it != IO::hotkeys.end() && it->second.enabled) {
        auto callback = it->second.callback;  // Copy just the callback
        lock.unlock();  // Release before executing
        
        std::thread([callback]() {
          callback();
        }).detach();
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

      if (!blockInput.load()) {
        SendUinputEvent(EV_REL, ev.code, scaledInt);
      }
    }
    // Mouse wheel
    else if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
      int wheelDirection = (ev.value > 0) ? 1 : -1;
      debug("🖱️  Mouse WHEEL: axis={}, direction={}, speed={}",
            ev.code == REL_WHEEL ? "VERT" : "HORZ",
            wheelDirection > 0 ? "UP/LEFT" : "DOWN/RIGHT", IO::scrollSpeed);

      // Update the wheel time tracking for combo evaluation and evaluate hotkeys
      std::vector<int> wheelHotkeyIds;
      bool wheelHotkeyShouldBlock = false;

      {
        std::unique_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
        std::unique_lock<std::shared_mutex> stateLock(stateMutex);

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

            if (hasWheel && EvaluateWheelCombo(hotkey, wheelDirection)) {
              info("Wheel combo: '{}'", hotkey.alias);
              wheelHotkeyIds.push_back(id);  // Store ID
              if (hotkey.grab)
                wheelHotkeyShouldBlock = true;
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
          wheelHotkeyIds.push_back(id);  // Store ID

          if (hotkey.grab)
            wheelHotkeyShouldBlock = true;
        }
      } // Both locks released here

      // Execute callbacks outside critical section
      for (int hotkeyId : wheelHotkeyIds) {
        HotKey hotkeyCopy;
        bool found = false;

        {
          std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
          auto it = IO::hotkeys.find(hotkeyId);

          if (it != IO::hotkeys.end() && it->second.enabled) {
            hotkeyCopy = it->second;  // Copy just for execution
            found = true;
          }
        }

        if (found) {
          ExecuteHotkeyCallback(hotkeyCopy);  // Use existing method
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
    if (!blockInput.load()) {
      SendUinputEvent(ev.type, ev.code, ev.value);
    }
  }
}
void EventListener::UpdateModifierState(int evdevCode, bool down) {
  // This is called with stateMutex already locked
  
  // Check if this key is remapped - if so, update modifier state based on target
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

  // Evaluate hotkeys with locks held, but collect matches to execute callbacks outside locks
  {
    std::shared_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
    std::shared_lock<std::shared_mutex> stateLock(stateMutex);

    for (auto &[id, hotkey] : IO::hotkeys) {
      if (!hotkey.enabled || !hotkey.evdev) {
        continue;
      }

      // Check if this is a combo hotkey
      if (hotkey.type == HotkeyType::Combo) {
        if (EvaluateCombo(hotkey)) {
          // Combo matched, collect for execution outside locks
          matchedHotkeyIds.push_back(id);
          if (hotkey.grab) {
            shouldBlock = true;
          }
        }
        continue;
      }

      // Match against key code
      if (hotkey.key != static_cast<Key>(evdevCode)) {
        continue;
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

      bool modifierMatch;

      // If the HOTKEY itself is a modifier key, skip modifier comparisons.
      // The pressed modifier should ALWAYS trigger its own hotkey.
      if (isModifierKey && hotkey.modifiers == 0) {
        modifierMatch = true;
      } else {
        modifierMatch = CheckModifierMatch(hotkey.modifiers, hotkey.wildcard);
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
        hotkeyCopy = it->second;  // Copy just for execution
        found = true;
      }
    }

    if (found) {
      ExecuteHotkeyCallback(hotkeyCopy);  // Use existing method
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
  auto now = std::chrono::steady_clock::now();

  // Always log combo evaluation
  debug("🔍 Evaluating combo '{}' | Active inputs: {}", hotkey.alias,
        GetActiveInputsString());

  for (const auto &comboKey : hotkey.comboSequence) {
    int keyCode;

    if (comboKey.type == HotkeyType::Keyboard) {
      keyCode = static_cast<int>(comboKey.key);
    } else if (comboKey.type == HotkeyType::MouseButton) {
      keyCode = comboKey.mouseButton;
    } else if (comboKey.type == HotkeyType::MouseWheel) {
      continue; // Handled separately
    } else {
      debug("❌ Combo '{}' failed: unsupported type {}", hotkey.alias,
            (int)comboKey.type);
      return false;
    }

    auto it = activeInputs.find(keyCode);
    if (it == activeInputs.end()) {
      debug("❌ Combo '{}' failed: key {} not in activeInputs", hotkey.alias,
            keyCode);
      return false;
    }

    // Time window (unless time window is 0 for infinite)
    if (comboTimeWindow > 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - it->second.timestamp)
                         .count();

      if (elapsed > comboTimeWindow) {
        debug("⏱️  Combo '{}' failed: key {} too old ({}ms > {}ms)",
              hotkey.alias, keyCode, elapsed, comboTimeWindow);
        return false;
      }
    }

    // Modifiers - check current state, not state when key was pressed
    if (comboKey.modifiers != 0) {
      int requiredMods = comboKey.modifiers;
      int currentMods = GetCurrentModifiersMask();

      if ((currentMods & requiredMods) != requiredMods) {
        debug("❌ Combo '{}' failed: key {} modifiers mismatch (have: 0x{:x}, "
              "need: 0x{:x})",
              hotkey.alias, keyCode, currentMods, requiredMods);
        return false;
      }
    }
  }

  debug("✅ Combo '{}' MATCHED!", hotkey.alias);
  return true;
}
} // namespace havel
