#include "EventListener.hpp"
#include "HotkeyExecutor.hpp"
#include "KeyMap.hpp"
#include "SignalHandler.hpp"
#include "core/ConfigManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/IO.hpp"
#include "utils/Logger.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/runtime/execution/ExecutionEngine.hpp"
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <shared_mutex>
#include <signal.h>
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
EventListener::EventListener() {
  signalHandler = std::make_unique<SignalHandler>(this);
  signalHandler->InstallAsyncHandlers();
  shutdownFd = eventfd(0, EFD_NONBLOCK);

  // Register atexit handler for emergency cleanup
  static bool atexitRegistered = false;
  if (!atexitRegistered) {
    std::atexit([]() {
      debug("atexit: emergency evdev ungrab");
      // Emergency cleanup - ForceUngrabAllDevices is async-signal-safe
      // but we can't call it here safely without knowing instance state
      // The IO destructor should handle normal cleanup
    });
    atexitRegistered = true;
  }
}

EventListener::~EventListener() {
  // Force ungrab all devices FIRST (critical for evdev cleanup)
  ForceUngrabAllDevices();

  // Stop the event loop and join thread
  Stop();

  if (shutdownFd >= 0) {
    close(shutdownFd);
  }

  debug("EventListener destructor completed - all devices ungrabbed");
}

bool EventListener::Start(const std::vector<std::string> &devicePaths,
                          bool grabDevices) {
  if (running.load()) {
    warn("EventListener already running");
    return false;
  }

  this->grabDevices = grabDevices;

  // Set up signal handling BEFORE spawning the event thread
  // This prevents race condition where SIGINT arrives before signalfd is ready
  SetupSignalHandling();

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
      debug("Successfully grabbed device: {} ({})", name, path);
      
      // Query and release any keys that were pressed before we grabbed
      // This prevents stuck modifiers and ghost keys
      uint8_t key_bits[(KEY_MAX + 7) / 8] = {};
      if (ioctl(fd, EVIOCGKEY(sizeof(key_bits)), key_bits) >= 0) {
        int released_count = 0;
        for (int key = 0; key < KEY_MAX; ++key) {
          if (key_bits[key / 8] & (1 << (key % 8))) {
            // Key is pressed - synthesize key-up event
            struct input_event ev = {};
            ev.type = EV_KEY;
            ev.code = key;
            ev.value = 0;  // key up
            if (write(fd, &ev, sizeof(ev)) < 0) {
              debug("Failed to release key {}", key);
            }
            released_count++;
          }
        }
        // Flush the events
        struct input_event sync_ev = {};
        sync_ev.type = EV_SYN;
        sync_ev.code = SYN_REPORT;
        sync_ev.value = 0;
        write(fd, &sync_ev, sizeof(sync_ev));
        
        if (released_count > 0) {
          debug("Released {} pre-existing pressed keys on {}", released_count, name);
        }
      } else {
        debug("Could not query key state for {} - continuing anyway", name);
      }
    }

    DrainDeviceEvents(fd);

    DeviceInfo device;
    device.path = path;
    device.fd = fd;
    device.name = name;
    devices.push_back(device);

    debug("Opened input device: {} ({})", name, path);
  }

  if (devices.empty()) {
    error("No input devices opened");
    return false;
  }

  ResetInputState();

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
  for (auto &device : devices) {
    DrainDeviceEvents(device.fd);
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

  if (signalHandler) {
    signalHandler->Shutdown();
  }
}

void EventListener::DrainDeviceEvents(int fd) {
  debug("Draining events from device fd={}", fd);
  struct input_event ev;
  ssize_t n;
  
  // Read and discard all pending events
  while (true) {
    n = read(fd, &ev, sizeof(ev));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;  // No more events
      }
      // Real error
      debug("DrainDeviceEvents: read error: {}", strerror(errno));
      break;
    }
    if (n == sizeof(ev)) {
      debug("Drained event: type={}, code={}, value={}", ev.type, ev.code, ev.value);
    }
  }
}

bool EventListener::SetupUinput() {
  uinputDevice = std::make_unique<UinputDevice>();
  if (!uinputDevice->Setup()) {
    error("Failed to initialize UinputDevice");
    return false;
  }
  info("UinputDevice initialized successfully");
  return true;
}

void EventListener::SendUinputEvent(int type, int code, int value) {
  if (!uinputDevice || !uinputDevice->IsInitialized()) {
    error("Cannot send event: uinput not initialized");
    return;
  }

  uinputDevice->SendEvent(type, code, value);
}

void EventListener::BeginUinputBatch() {
  if (!uinputDevice) {
    error("Cannot begin batch: uinput not initialized");
    return;
  }
  uinputDevice->BeginBatch();
}

void EventListener::QueueUinputEvent(int type, int code, int value) {
  if (!uinputDevice) {
    error("Cannot queue event: uinput not initialized");
    return;
  }
  uinputDevice->SendEvent(type, code, value);
}

void EventListener::EndUinputBatch() {
  if (!uinputDevice) {
    error("Cannot end batch: uinput not initialized");
    return;
  }
  uinputDevice->EndBatch();
}

void EventListener::EmergencyReleaseAllKeys() {
  if (!uinputDevice) {
    error("Cannot release keys: uinput not initialized");
    return;
  }
  uinputDevice->ReleaseAllKeys();
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

int EventListener::RemapKey(int evdevCode, bool down) {
  std::lock_guard<std::mutex> lock(remapMutex);
  if (down) {
    // On press: apply remapping and store the mapping
    auto remapIt = keyRemaps.find(evdevCode);
    if (remapIt != keyRemaps.end()) {
      activeRemaps[evdevCode] = remapIt->second;
      return remapIt->second;
    }
    activeRemaps[evdevCode] = evdevCode;
  } else {
    // On release: use the same mapping we stored on press
    auto it = activeRemaps.find(evdevCode);
    if (it != activeRemaps.end()) {
      int mappedCode = it->second;
      activeRemaps.erase(it);
      return mappedCode;
    }
  }
  return evdevCode;
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
    std::lock_guard<std::mutex> ioLock(HotkeyManager::RegisteredHotkeysMutex());
    for (const auto &[id, hotkey] : HotkeyManager::RegisteredHotkeys()) {
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

void EventListener::setHostBridge(havel::compiler::HostBridge *hb) {
  hostBridge = hb;
}

void EventListener::setExecutionEngine(havel::compiler::ExecutionEngine *ee) {
  executionEngine = ee;
}

void EventListener::EventLoop() {
  // Signal handling is now set up in Start() before the thread spawns
  // No need to call SetupSignalHandling() here

  while (running.load() && !shutdown.load()) {
    // Check for signal from atomic flag (set by traditional async handlers)
    int signalFlag = SignalHandler::GetSignalFlag();
    if (signalFlag == SIGINT || signalFlag == SIGTERM) {
      debug("Shutdown signal detected via atomic flag: {}", signalFlag);
      RequestShutdownFromSignal(signalFlag);
      break;
    } else if (signalFlag != 0) {
      // Non-shutdown signals (SIGCHLD, SIGALRM, etc.) — clear the flag and
      // continue. The signalfd path in HandleSignal handles these properly.
      SignalHandler::ClearSignalFlag();
    }

    // Check for expired timers (single-threaded VM timer queue)
    // This must be done in the main event loop thread to avoid VM reentrancy issues
    if (hostBridge) {
      hostBridge->checkTimers();
    }

    // Phase 3: Execute one bytecode instruction in next runnable goroutine
    // This is the core of the concurrent execution model
    // The ExecutionEngine coordinates with Scheduler and EventQueue
    bool workRemains = false;
    if (executionEngine) {
      workRemains = executionEngine->executeFrame();
    }

    fd_set readfds;
    FD_ZERO(&readfds);

    int maxFd = shutdownFd;
    FD_SET(shutdownFd, &readfds);

    // Add signal fd if available
    int signalFdToUse = signalHandler ? signalHandler->GetSignalFd() : -1;
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
    if (workRemains) {
      // Don't block if there is work to do in the VM
      timeout.tv_sec = 0;
      timeout.tv_usec = 0;
    } else {
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
    }

    int ret = select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);

    if (ret < 0) {
      if (errno == EINTR)
        continue;
      error("select() failed: {}", strerror(errno));
      break;
    }

    if (ret == 0)
      continue;

    if (FD_ISSET(shutdownFd, &readfds)) {
      if (asyncSignalRequested != 0) {
        SignalSafeShutdown(asyncSignalRequested, false);
        asyncSignalRequested = 0;
      }
      break;
    }

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

  debug("EventListener: Waiting for {} callbacks", pendingCallbacks.load());

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

  debug("EventListener: Stopped");
}
void EventListener::ProcessKeyboardEvent(const input_event &ev) {
  if (inputNotificationCallback) {
    inputNotificationCallback();
  }

  if (ev.type == EV_REL) {
    if (ev.code == REL_X || ev.code == REL_Y) {
      double scaledValue = ev.value * IO::mouseSensitivity;
      int32_t scaledInt = static_cast<int32_t>(scaledValue);

      InputEvent event;
      event.kind = InputEventKind::MouseMove;
      event.code = ev.code;
      event.value = scaledInt;
      event.dx = (ev.code == REL_X) ? scaledInt : 0;
      event.dy = (ev.code == REL_Y) ? scaledInt : 0;
      event.modifiers = GetCurrentModifiersMask();

      if (inputEventCallback) {
        inputEventCallback(event);
      }
      if (mouseMovementCallback) {
        mouseMovementCallback(event.dx, event.dy);
      }

      bool shouldBlock = false;
      if (inputBlockCallback) {
        shouldBlock = inputBlockCallback(event);
      }

      if (!shouldBlock && !blockInput.load()) {
        SendUinputEvent(EV_REL, ev.code, scaledInt);
      }

      if (ev.code == REL_X) {
        currentMouseX += scaledInt;
      } else if (ev.code == REL_Y) {
        currentMouseY += scaledInt;
      }
      return;
    }

    if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
      InputEvent event;
      event.kind = InputEventKind::MouseWheel;
      event.code = ev.code;
      event.value = ev.value;
      event.dy = (ev.code == REL_WHEEL) ? ((ev.value > 0) ? 1 : -1) : 0;
      event.dx = (ev.code == REL_HWHEEL) ? ((ev.value > 0) ? 1 : -1) : 0;
      event.modifiers = GetCurrentModifiersMask();

      if (inputEventCallback) {
        inputEventCallback(event);
      }

      bool shouldBlock = false;
      if (inputBlockCallback) {
        shouldBlock = inputBlockCallback(event);
      }

      if (!shouldBlock && !blockInput.load()) {
        double scaledValue = ev.value * IO::scrollSpeed;
        int32_t scaledInt = static_cast<int32_t>(scaledValue);
        if (scaledInt == 0 && ev.value != 0 && IO::scrollSpeed >= 1.0) {
          scaledInt = (ev.value > 0) ? 1 : -1;
        }
        SendUinputEvent(EV_REL, ev.code, scaledInt);
      }
      return;
    }

    if (!blockInput.load()) {
      SendUinputEvent(ev.type, ev.code, ev.value);
    }
    return;
  }

  if (ev.type == EV_ABS) {
    InputEvent event;
    event.kind = InputEventKind::Absolute;
    event.code = ev.code;
    event.value = ev.value;
    event.modifiers = GetCurrentModifiersMask();

    if (inputEventCallback) {
      inputEventCallback(event);
    }

    bool shouldBlock = false;
    if (inputBlockCallback) {
      shouldBlock = inputBlockCallback(event);
    }

    if (ev.code == ABS_X) {
      currentMouseX = ev.value;
    } else if (ev.code == ABS_Y) {
      currentMouseY = ev.value;
    }

    if (!shouldBlock && !blockInput.load()) {
      SendUinputEvent(ev.type, ev.code, ev.value);
    }
    return;
  }

  auto now = std::chrono::steady_clock::now();
  int originalCode = ev.code;
  bool repeat = (ev.value == 2);
  bool down = (ev.value == 1 || repeat);
  int mappedCode = RemapKey(originalCode, down);

  {
    std::unique_lock<std::shared_mutex> lock(stateMutex);
    evdevKeyState[originalCode] = down;
    UpdateModifierState(mappedCode, down);
    if (down) {
      keyDownTime[originalCode] = now;
      activeInputs[originalCode] = ActiveInput(GetCurrentModifiersMask(), now);
      physicalKeyStates[originalCode] = true;
    } else {
      keyDownTime.erase(originalCode);
      activeInputs.erase(originalCode);
      physicalKeyStates[originalCode] = false;
    }
  }

  // Call raw key callbacks if registered
  if (down && keyDownCallback) {
    keyDownCallback(originalCode);
  } else if (!down && keyUpCallback) {
    keyUpCallback(originalCode);
  }

  if (down && emergencyShutdownKey != 0 &&
      originalCode == emergencyShutdownKey) {
    error("🚨 EMERGENCY HOTKEY TRIGGERED! Shutting down...");
    running.store(false);
    shutdown.store(true);
    ForceUngrabAllDevices();
    if (shutdownFd >= 0) {
      uint64_t val = 1;
      write(shutdownFd, &val, sizeof(val));
    }
    return;
  }

  InputEvent event;
  event.kind = InputEventKind::Key;
  event.code = originalCode;
  event.value = ev.value;
  event.down = down;
  event.repeat = repeat;
  event.originalCode = originalCode;
  event.mappedCode = mappedCode;
  event.keyName = KeyMap::EvdevToString(mappedCode);
  event.modifiers = GetCurrentModifiersMask();

  if (down && anyKeyPressCallback) {
    anyKeyPressCallback(event.keyName);
  }

  if (inputEventCallback) {
    inputEventCallback(event);
  }

  bool shouldBlock = false;
  if (inputBlockCallback) {
    shouldBlock = inputBlockCallback(event);
  }

  if (shouldBlock) {
    if (!down) {
      SendUinputEvent(EV_KEY, mappedCode, 0);
    }
    return;
  }

  SendUinputEvent(EV_KEY, mappedCode, ev.value);
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

  switch (executorMode_) {
  case ExecutorMode::Executor: {
    if (hotkeyExecutor) {
      auto result = hotkeyExecutor->submit(
        [callback = callback_copy, hotkeyAlias = alias_copy, this]() {
          try {
            callback();
          } catch (const std::exception &e) {
            error("Hotkey '{}' threw: {}", hotkeyAlias, e.what());
          } catch (...) {
            error("Hotkey '{}' threw unknown exception", hotkeyAlias);
          }
          {
            std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
            executingHotkeys.erase(hotkeyAlias);
          }
        });
      if (!result.accepted) {
        warn("Hotkey task queue full, dropping callback: {}", alias_copy);
        {
          std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
          executingHotkeys.erase(alias_copy);
        }
      }
      return;
    }
    break;
  }
  case ExecutorMode::Scheduler: {
    // Phase 3: Single-threaded cooperative execution via EventQueue
    if (executionEngine && executionEngine->getEventQueue()) {
      executionEngine->getEventQueue()->push([callback = callback_copy, hotkeyAlias = alias_copy, this]() {
        try {
          callback();
        } catch (const std::exception &e) {
          error("Hotkey '{}' threw: {}", hotkeyAlias, e.what());
        } catch (...) {
          error("Hotkey '{}' threw unknown exception", hotkeyAlias);
        }
        {
          std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
          executingHotkeys.erase(hotkeyAlias);
        }
      });
      return;
    }
    
    // Fallback if engine not available
    if (hotkeyExecutor) {
      auto result = hotkeyExecutor->submit(
        [callback = callback_copy, hotkeyAlias = alias_copy, this]() {
          try {
            callback();
          } catch (const std::exception &e) {
            error("Hotkey '{}' threw: {}", hotkeyAlias, e.what());
          } catch (...) {
            error("Hotkey '{}' threw unknown exception", hotkeyAlias);
          }
          {
            std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
            executingHotkeys.erase(hotkeyAlias);
          }
        });
      if (!result.accepted) {
        warn("Hotkey task queue full, dropping callback: {}", alias_copy);
        {
          std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
          executingHotkeys.erase(alias_copy);
        }
      }
      return;
    }
    break;
  }
  case ExecutorMode::Sync: {
    try {
      callback_copy();
    } catch (const std::exception &e) {
      error("Hotkey '{}' threw: {}", alias_copy, e.what());
    } catch (...) {
      error("Hotkey '{}' threw unknown exception", alias_copy);
    }
    {
      std::lock_guard<std::mutex> execLock(hotkeyExecMutex);
      executingHotkeys.erase(alias_copy);
    }
    return;
  }
  case ExecutorMode::Thread:
    break;
  }

  // Thread mode (or fallback when no executor available)
  pendingCallbacks++;

  std::thread([callback = callback_copy, alias = alias_copy, this]() {
    try {
      if (running.load() && !shutdown.load()) {
        callback();
      }
    } catch (const std::exception &e) {
      error("Hotkey '{}' exception: {}", alias, e.what());
    }
    pendingCallbacks--;

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

    // Skip if wheelTime is zero-initialized (not yet set)
    if (wheelTime.time_since_epoch().count() == 0) {
      debug("❌ Wheel combo '{}' failed: wheel time not initialized",
            hotkey.alias);
      return false;
    }

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

    if (inputEventCallback) {
      InputEvent event;
      event.kind = InputEventKind::MouseButton;
      event.code = ev.code;
      event.value = ev.value;
      event.down = down;
      event.repeat = (ev.value == 2);
      event.modifiers = GetCurrentModifiersMask();
      inputEventCallback(event);
    }

    // Collect matched hotkeys to avoid holding locks during callback execution
    std::vector<int> matchedHotkeyIds;
    bool shouldBlock = false;

    // Evaluate hotkeys - lock both mutexes
    {
      std::unique_lock<std::shared_mutex> hotkeyLock(hotkeyMutex);
      std::unique_lock<std::shared_mutex> stateLock(stateMutex);

      std::lock_guard<std::mutex> ioLock(
          HotkeyManager::RegisteredHotkeysMutex());
      for (auto &[id, hotkey] : HotkeyManager::RegisteredHotkeys()) {
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
        if (Conf().GetVerboseKeyLogging()) {
          info("Mouse button hotkey: '{}' button={} down={}", hotkey.alias,
               ev.code, down);
        }
        matchedHotkeyIds.push_back(id); // Just store ID
        if (hotkey.grab)
          shouldBlock = true;
      }
    } // Both locks released here

    // Execute callbacks outside critical section
    for (int hotkeyId : matchedHotkeyIds) {
      std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
      auto it = HotkeyManager::RegisteredHotkeys().find(hotkeyId);

      std::lock_guard<std::mutex> ioLock(
          HotkeyManager::RegisteredHotkeysMutex());
      if (it != HotkeyManager::RegisteredHotkeys().end() &&
          it->second.enabled) {
        auto callback = it->second.callback; // Copy just the callback
        lock.unlock();                       // Release before executing

        // Use HotkeyExecutor instead of spawning threads
        if (hotkeyExecutor) {
          hotkeyExecutor->submit([callback]() {
            try {
              callback();
            } catch (const std::exception &e) {
              error("Callback exception: {}", e.what());
            } catch (...) {
              error("Callback unknown exception");
            }
          });
        } else {
          std::thread([callback]() { callback(); }).detach();
        }
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
      if (inputEventCallback) {
        InputEvent event;
        event.kind = InputEventKind::MouseMove;
        event.code = ev.code;
        event.value = scaledInt;
        event.dx = (ev.code == REL_X) ? scaledInt : 0;
        event.dy = (ev.code == REL_Y) ? scaledInt : 0;
        event.modifiers = GetCurrentModifiersMask();
        inputEventCallback(event);
      }
      if (mouseMovementCallback) {
        if (ev.code == REL_X) {
          mouseMovementCallback(scaledInt, 0); // dx, dy=0
        } else if (ev.code == REL_Y) {
          mouseMovementCallback(0, scaledInt); // dx=0, dy
        }
      }
      // Process mouse gesture if we have gesture hotkeys registered
      if (mouseGestureEngine.HasRegisteredGestures()) {
        int dx = 0;
        int dy = 0;
        if (ev.code == REL_X) {
          dx = scaledInt;
        } else if (ev.code == REL_Y) {
          dy = scaledInt;
        }

        const auto matches = mouseGestureEngine.RecordMovement(dx, dy);
        for (int hotkeyId : matches) {
          std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
          auto hotkeyIt = HotkeyManager::RegisteredHotkeys().find(hotkeyId);
          if (hotkeyIt == HotkeyManager::RegisteredHotkeys().end()) {
            continue;
          }

          const auto &hotkey = hotkeyIt->second;
          if (hotkey.enabled && hotkey.type == HotkeyType::MouseGesture) {
            ExecuteHotkeyCallback(hotkey);
            break;
          }
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
      if (inputEventCallback) {
        InputEvent event;
        event.kind = InputEventKind::MouseWheel;
        event.code = ev.code;
        event.value = ev.value;
        event.dy = (ev.code == REL_WHEEL) ? wheelDirection : 0;
        event.dx = (ev.code == REL_HWHEEL) ? wheelDirection : 0;
        event.modifiers = GetCurrentModifiersMask();
        inputEventCallback(event);
      }
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

        for (auto &[id, hotkey] : HotkeyManager::RegisteredHotkeys()) {
          if (!hotkey.enabled)
            continue;
          if (hotkey.type == HotkeyType::Combo) {
            std::lock_guard<std::mutex> ioLock(
                HotkeyManager::RegisteredHotkeysMutex());
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
              if (Conf().GetVerboseKeyLogging()) {
                info("Wheel combo: '{}'", hotkey.alias);
              }
              wheelHotkeyIds.push_back(id); // Store ID
              if (hotkey.grab)
                wheelHotkeyShouldBlock = true;
            } else if (!hasWheel && hotkey.requiresWheel) {
              // This shouldn't happen, but just in case - evaluate normally if
              // it has the flag
              if (EvaluateCombo(hotkey)) {
                if (Conf().GetVerboseKeyLogging()) {
                  info("Non-wheel combo with requiresWheel flag: '{}'",
                       hotkey.alias);
                }
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
          if (Conf().GetVerboseKeyLogging()) {
            info("Wheel hotkey: '{}' dir={}", hotkey.alias, wheelDirection);
          }
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
          auto it = HotkeyManager::RegisteredHotkeys().find(hotkeyId);

          if (it != HotkeyManager::RegisteredHotkeys().end() &&
              it->second.enabled) {
            hotkeyCopy = it->second; // Copy just for execution
            found = true;
            std::lock_guard<std::mutex> ioLock(
                HotkeyManager::RegisteredHotkeysMutex());
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
    if (inputEventCallback) {
      InputEvent event;
      event.kind = InputEventKind::Absolute;
      event.code = ev.code;
      event.value = ev.value;
      event.modifiers = GetCurrentModifiersMask();
      inputEventCallback(event);
    }
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

    for (auto &[id, hotkey] : HotkeyManager::RegisteredHotkeys()) {
      if (!hotkey.enabled)
        continue;

      // Only match keyboard and mouse movement hotkeys with our virtual
      // movement keys
      std::lock_guard<std::mutex> ioLock(
          HotkeyManager::RegisteredHotkeysMutex());
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
    auto it = HotkeyManager::RegisteredHotkeys().find(hotkeyId);

    if (it != HotkeyManager::RegisteredHotkeys().end() && it->second.enabled) {
      auto callback = it->second.callback;
      lock.unlock();

      // Use HotkeyExecutor instead of spawning threads
      if (hotkeyExecutor) {
        hotkeyExecutor->submit([callback]() {
          try {
            callback();
          } catch (const std::exception &e) {
            error("Callback exception: {}", e.what());
          } catch (...) {
            error("Callback unknown exception");
          }
        });
      } else {
        std::lock_guard<std::mutex> ioLock(
            HotkeyManager::RegisteredHotkeysMutex());
        std::thread([callback]() { callback(); }).detach();
      }
    }
  }
}

void EventListener::QueueMouseMovementHotkey(int virtualKey) {
  // Check rate limiting to avoid flooding the queue
  auto now = std::chrono::steady_clock::now();

  // Skip if lastMovementHotkeyTime is zero-initialized (not yet set)
  if (lastMovementHotkeyTime.time_since_epoch().count() == 0) {
    lastMovementHotkeyTime = now;
    return;
  }

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
    // Use HotkeyExecutor instead of spawning threads
    if (hotkeyExecutor) {
      hotkeyExecutor->submit([this]() {
        try {
          ProcessQueuedMouseMovementHotkeys();
        } catch (const std::exception &e) {
          error("Mouse movement callback exception: {}", e.what());
        } catch (...) {
          error("Mouse movement callback unknown exception");
        }
        movementHotkeyProcessing.store(false);
      });
    } else {
      std::thread([this]() {
        ProcessQueuedMouseMovementHotkeys();
        movementHotkeyProcessing.store(false);
      }).detach();
    }
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

    for (auto &[id, hotkey] : HotkeyManager::RegisteredHotkeys()) {
      if (!hotkey.enabled || !hotkey.evdev) {
        continue;
      }

      // Check if this is a combo hotkey
      if (hotkey.type == HotkeyType::Combo) {
        try {
          if (EvaluateCombo(hotkey)) {
            // Combo matched, collect for execution outside locks
            std::lock_guard<std::mutex> ioLock(
                HotkeyManager::RegisteredHotkeysMutex());
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
      auto it = HotkeyManager::RegisteredHotkeys().find(hotkeyId);

      if (it != HotkeyManager::RegisteredHotkeys().end() &&
          it->second.enabled) {
        hotkeyCopy = it->second; // Copy just for execution
        found = true;
      }
    }

    if (found) {
      ExecuteHotkeyCallback(hotkeyCopy); // Use existing method
    }
  }

  std::lock_guard<std::mutex> ioLock(HotkeyManager::RegisteredHotkeysMutex());
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
    std::vector<int> orderedRequiredKeys;
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
          orderedRequiredKeys.push_back(keyCode);
        }
      } else if (comboKey.type == HotkeyType::MouseButton) {
        requiredKeys.insert(comboKey.mouseButton);
        orderedRequiredKeys.push_back(comboKey.mouseButton);
      } else if (comboKey.type == HotkeyType::MouseMove) {
        requiredKeys.insert(static_cast<int>(comboKey.key));
        orderedRequiredKeys.push_back(static_cast<int>(comboKey.key));
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

    // Check each required key is actually active in the defined order
    std::chrono::steady_clock::time_point lastTimestamp{};
    bool hasLastTimestamp = false;
    for (int requiredKey : orderedRequiredKeys) {
      auto it = activeInputs.find(requiredKey);

      if (it == activeInputs.end()) {
        debug("❌ Combo '{}' rejected: required key {} not active",
              hotkey.alias, requiredKey);
        return false;
      }

      if (hasLastTimestamp && it->second.timestamp < lastTimestamp) {
        debug("❌ Combo '{}' rejected: key {} order mismatch", hotkey.alias,
              requiredKey);
        return false;
      }

      lastTimestamp = it->second.timestamp;
      hasLastTimestamp = true;

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
    // modifier handling - BUT NOT if we have specific physical key requirements
    // (e.g., @RShift requires Right Shift specifically)
    bool isPureModifierWheelCombo =
        hotkey.requiresWheel && requiredKeys.empty() &&
        requiredModifiers != 0 && hotkey.requiredPhysicalKeys.empty();

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
  const auto matches = mouseGestureEngine.RecordMovement(dx, dy);
  for (int hotkeyId : matches) {
    std::shared_lock<std::shared_mutex> lock(hotkeyMutex);
    auto hotkeyIt = HotkeyManager::RegisteredHotkeys().find(hotkeyId);
    if (hotkeyIt == HotkeyManager::RegisteredHotkeys().end()) {
      continue;
    }

    const auto &hotkey = hotkeyIt->second;
    if (hotkey.enabled && hotkey.type == HotkeyType::MouseGesture) {
      ExecuteHotkeyCallback(hotkey);
      break;
    }
  }
}

MouseGestureDirection EventListener::GetGestureDirection(int dx, int dy) const {
  return mouseGestureEngine.GetDirection(dx, dy);
}

bool EventListener::MatchGesturePattern(
    const std::vector<MouseGestureDirection> &expected,
    const std::vector<MouseGestureDirection> &actual) const {
  return mouseGestureEngine.MatchPattern(expected, actual);
}

// Release all pressed virtual keys (for clean shutdown)
void EventListener::ReleaseAllVirtualKeys() {
  if (pressedVirtualKeys.empty())
    return;

  debug("Releasing {} pressed virtual keys", pressedVirtualKeys.size());

  // Copy the set to avoid use-after-free if SendUinputEvent modifies it
  std::unordered_set<int> keysToRelease = pressedVirtualKeys;

  for (int code : keysToRelease) {
    SendUinputEvent(EV_KEY, code, 0);
  }

  // Send final SYN_REPORT
  SendUinputEvent(EV_SYN, SYN_REPORT, 0);

  pressedVirtualKeys.clear();
}

// Force ungrab all devices - ASYNC-SIGNAL-SAFE (Layer 2)
// This is called from signal handlers and MUST complete successfully
// to prevent stuck grabs. Uses only async-signal-safe operations.
void EventListener::ForceUngrabAllDevices() {
  // CRITICAL: This method is called from signal handlers
  // Do NOT use: malloc, free, printf, cerr, logging, mutexes, etc.
  // Only use: ioctl, close, _exit, and simple memory operations

  // Ungrab all input devices - this is the MOST IMPORTANT operation
  for (auto &device : devices) {
    if (device.fd >= 0) {
      // EVIOCGRAB with 0 releases the grab
      // This is async-signal-safe according to POSIX
      ioctl(device.fd, EVIOCGRAB, 0);
    }
  }

  // Also destroy uinput device if active
  if (uinputDevice) {
    const int uinputFd = uinputDevice->GetFd();
    if (uinputFd >= 0) {
      ioctl(uinputFd, UI_DEV_DESTROY);
    }
  }
}

// Helper function to convert gesture pattern string to directions
std::vector<MouseGestureDirection>
EventListener::ParseGesturePattern(const std::string &patternStr) const {
  return mouseGestureEngine.ParsePattern(patternStr);
}

// Overloaded method that takes a HotKey and gets its pattern
std::vector<MouseGestureDirection>
EventListener::ParseGesturePattern(const HotKey &hotkey) const {
  return ParseGesturePattern(hotkey.gesturePattern);
}

// Helper function to validate gesture with tolerance
bool EventListener::IsGestureValid(
    const std::vector<MouseGestureDirection> &pattern, int minDistance) const {
  return mouseGestureEngine.IsGestureValid(pattern, minDistance);
}

void EventListener::ResetMouseGesture() { mouseGestureEngine.Reset(); }

void EventListener::ResetInputState() {
  std::unique_lock<std::shared_mutex> lock(stateMutex);
  activeInputs.clear();
  physicalKeyStates.clear();
  keyDownTime.clear();
  modifierState = ModifierState();
}

void EventListener::RegisterGestureHotkey(
    int id, const std::vector<MouseGestureDirection> &directions) {
  mouseGestureEngine.RegisterHotkey(id, directions);
}

void EventListener::SetupSignalHandling() {
  if (signalHandler) {
    signalHandler->SetupSignalfd();
  }
}

void EventListener::ProcessSignal() {
  if (signalHandler) {
    signalHandler->ProcessSignal();
  }
}

void EventListener::HandleSignal(int sig) {
  debug("EventListener received signal: {}", sig);

  switch (sig) {
  case SIGTERM:
  case SIGINT:
    if (!shutdown.load()) {
      RequestShutdownFromSignal(sig);
    }
    break;
  case SIGHUP:
  case SIGQUIT:
    if (!shutdown.load()) {
      SignalSafeShutdown(sig, true);
    }
    break;
  case SIGSEGV:
    error("Segmentation fault - attempting graceful shutdown");
    SignalSafeShutdown(sig, true);
    break;
  default:
    debug("Unknown signal received: {}", sig);
    break;
  }
}

void EventListener::RequestShutdownFromSignal(int sig) {
  ForceUngrabAllDevices();
  asyncSignalRequested = sig;
  if (shutdownFd >= 0) {
    uint64_t val = 1;
    write(shutdownFd, &val, sizeof(val));
  }
}

void EventListener::SignalSafeShutdown(int sig, bool exitAfter) {
  ForceUngrabAllDevices();
  running.store(false);
  shutdown.store(true);
  if (shutdownFd >= 0) {
    uint64_t val = 1;
    write(shutdownFd, &val, sizeof(val));
  }
  if (exitAfter) {
    // Use proper exit codes: 0 for graceful shutdown, signal number + 128 for
    // signal termination
    int exitCode =
        (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) ? 0 : sig + 128;
    _exit(exitCode);
  }
}

} // namespace havel
