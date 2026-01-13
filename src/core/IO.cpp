#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/HotkeyManager.hpp"
#include "io/EventListener.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "utils/Utils.hpp"
#include "x11.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <linux/input.h>
#include <linux/uinput.h>
#include <mutex>
#include <qapplication.h>
#include <qtmetamacros.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <unistd.h>

// X11 includes
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XI2proto.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace havel {
#if defined(WINDOWS)
HHOOK IO::keyboardHook = NULL;
#endif
std::unordered_map<int, HotKey> IO::hotkeys;
bool IO::hotkeyEnabled = true;
int IO::hotkeyCount = 0;
bool IO::globalEvdev = true;
double IO::mouseSensitivity = 1.0;
double IO::scrollSpeed = 1.0;
std::atomic<int> IO::syntheticEventsExpected{0};
// Static keycode cache - eliminates repeated lookups for the same keys
static std::unordered_map<std::string, int> g_keycodeCache;
static std::mutex g_keycodeCacheMutex;

int IO::GetKeyCacheLookup(const std::string& keyName) {
  std::string lower = toLower(keyName);
  
  // Fast path: check cache first
  {
    std::lock_guard<std::mutex> lock(g_keycodeCacheMutex);
    auto it = g_keycodeCache.find(lower);
    if (it != g_keycodeCache.end()) {
      return it->second;
    }
  }
  
  // Cache miss: do the expensive lookup
  int code = EvdevNameToKeyCode(lower);
  
  // Store in cache for next time
  if (code != -1) {
    std::lock_guard<std::mutex> lock(g_keycodeCacheMutex);
    g_keycodeCache[lower] = code;
  }
  
  return code;
}

// Parse key string into tokens once instead of scanning repeatedly
std::vector<KeyToken> IO::ParseKeyString(const std::string& keys) {
  std::vector<KeyToken> tokens;
  size_t i = 0;
  
  static std::unordered_map<char, std::string> shorthandModifiers = {
      {'^', "ctrl"}, {'!', "alt"}, {'+', "shift"},
      {'#', "meta"}, {'@', "toggle_uinput"}, {'%', "toggle_x11"}
  };
  
  while (i < keys.length()) {
    // Handle special sequences like {key_name}
    if (keys[i] == '{') {
      size_t end = keys.find('}', i);
      if (end != std::string::npos) {
        std::string seq = keys.substr(i + 1, end - i - 1);
        std::transform(seq.begin(), seq.end(), seq.begin(), ::tolower);
        
        KeyToken token;
        
        if (seq == "emergency_release" || seq == "panic") {
          token.type = KeyToken::Special;
          token.value = seq;
        } else if (seq.ends_with(" down") || seq.ends_with(":down")) {
          token.type = KeyToken::ModifierDown;
          token.value = seq.substr(0, seq.size() - 5);
          token.down = true;
        } else if (seq.ends_with(" up") || seq.ends_with(":up")) {
          token.type = KeyToken::ModifierUp;
          token.value = seq.substr(0, seq.size() - 3);
          token.down = false;
        } else {
          token.type = KeyToken::Key;
          token.value = seq;
          token.down = true;
        }
        
        tokens.push_back(token);
        i = end + 1;
        continue;
      }
    }
    
    // Handle shorthand modifiers (^, +, !, #)
    if (shorthandModifiers.count(keys[i])) {
      std::string mod = shorthandModifiers[keys[i]];
      KeyToken token;
      token.type = KeyToken::Modifier;
      token.value = mod;
      token.down = true;
      tokens.push_back(token);
      ++i;
      continue;
    }
    
    // Skip whitespace
    if (isspace(keys[i])) {
      ++i;
      continue;
    }
    
    // Single character keys
    KeyToken token;
    token.type = KeyToken::Key;
    token.value = std::string(1, keys[i]);
    token.down = true;
    tokens.push_back(token);
    ++i;
  }
  
  return tokens;
}

// Batch write events with a single syscall
void IO::SendBatchedKeyEvents(const std::vector<input_event>& events) {
  if (events.empty()) return;
  
  // Use EventListener's batched write if available
  if (eventListener && useNewEventListener) {
    for (const auto& ev : events) {
      eventListener->SendUinputEvent(ev.type, ev.code, ev.value);
    }
  }
  // Otherwise batch write directly (requires access to uinput fd)
  // This is a fallback - EventListener should handle the batching
}

int IO::XErrorHandler(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == x11::XBadWindow ||
      (ee->request_code == X_GrabButton && ee->error_code == x11::XBadAccess) ||
      (ee->request_code == X_GrabKey && ee->error_code == x11::XBadAccess)) {
    return 0;
  }
  error("X11 error: request_code={}, error_code={}", ee->request_code,
        ee->error_code);
  return 0; // Don't crash
}

std::string IO::findEvdevDevice(const std::string &deviceName) {
  debug("=== DEBUGGING findEvdevDevice for '{}' ===", deviceName);

  std::ifstream proc("/proc/bus/input/devices");
  if (!proc.is_open()) {
    error("Cannot open /proc/bus/input/devices");
    return "";
  }

  std::string line;
  std::string currentName;
  int deviceCount = 0;

  while (std::getline(proc, line)) {
    debug("Line: {}", line);

    if (line.starts_with("N: Name=")) {
      deviceCount++;
      currentName = line.substr(8);
      if (!currentName.empty() && currentName[0] == '"') {
        currentName = currentName.substr(1, currentName.length() - 2);
      }
      debug("  Device #{}: '{}'", deviceCount, currentName);

      if (currentName == deviceName) {
        debug("  🎯 EXACT MATCH FOUND!");
      }
    } else if (line.starts_with("H: Handlers=")) {
      debug("  Handlers: {}", line);

      if (currentName == deviceName) {
        debug("  🔥 This is our target device!");

        size_t eventPos = line.find("event");
        if (eventPos != std::string::npos) {
          std::string eventStr = line.substr(eventPos + 5);
          size_t spacePos = eventStr.find(' ');
          if (spacePos != std::string::npos) {
            eventStr = eventStr.substr(0, spacePos);
          }
          std::string result = "/dev/input/event" + eventStr;
          debug("  ✅ SUCCESS: {}", result);
          return result;
        }
      }
    }
  }

  debug("=== END DEBUG - Device not found ===");
  return "";
}
std::string IO::getKeyboardDevice() {
  std::string id = Configs::Get().Get<std::string>("Device.KeyboardID", "");
  if (!id.empty()) {
    debug("Using keyboard device ID from config: '{}'", id);
    return findEvdevDevice(id);
  }

  auto keyboards = Device::findKeyboards();

  debug("=== Keyboard Detection Results ===");
  for (const auto &kb : keyboards) {
    debug("Found: '{}' confidence={:.1f}% reason='{}'", kb.name,
          kb.confidence * 100, kb.reason);
  }

  if (!keyboards.empty()) {
    info("✅ Selected keyboard: '{}' -> {} (confidence: {:.1f}%)",
         keyboards[0].name, keyboards[0].eventPath,
         keyboards[0].confidence * 100);
    return keyboards[0].eventPath;
  }

  error("❌ No suitable keyboard devices found");
  return "";
}

std::string IO::getMouseDevice() {
  std::string id = Configs::Get().Get<std::string>("Device.MouseID", "");
  if (!id.empty()) {
    debug("Using mouse device ID from config: '{}'", id);
    return findEvdevDevice(id);
  }

  auto mice = Device::findMice();

  debug("=== Mouse Detection Results ===");
  for (const auto &mouse : mice) {
    debug("Found: '{}' confidence={:.1f}% reason='{}'", mouse.name,
          mouse.confidence * 100, mouse.reason);
  }

  if (!mice.empty()) {
    info("✅ Selected mouse: '{}' -> {} (confidence: {:.1f}%)", mice[0].name,
         mice[0].eventPath, mice[0].confidence * 100);
    return mice[0].eventPath;
  }

  warning("❌ No suitable mouse devices found");
  return "";
}

std::string IO::getGamepadDevice() {
  auto gamepads = Device::findGamepads();

  debug("=== Gamepad Detection Results ===");
  for (const auto &gamepad : gamepads) {
    debug("Found: '{}' confidence={:.1f}% reason='{}'", gamepad.name,
          gamepad.confidence * 100, gamepad.reason);
  }

  if (!gamepads.empty()) {
    info("✅ Found gamepad: '{}' -> {} (confidence: {:.1f}%)", gamepads[0].name,
         gamepads[0].eventPath, gamepads[0].confidence * 100);
    return gamepads[0].eventPath;
  }

  return "";
}

std::vector<std::string> IO::GetInputDevices() {
  std::vector<std::string> devices;
  std::string keyboardDevice = getKeyboardDevice();
  std::string mouseDevice = getMouseDevice();
  std::string gamepadDevice;

  if (!keyboardDevice.empty()) {
    devices.push_back(keyboardDevice);
  }

  if (!mouseDevice.empty() && mouseDevice != keyboardDevice &&
      !Configs::Get().Get<bool>("Device.IgnoreMouse", false)) {
    devices.push_back(mouseDevice);
  }

  bool enableGamepad = Configs::Get().Get<bool>("Device.EnableGamepad", false);
  if (enableGamepad) {
    gamepadDevice = getGamepadDevice();
    if (!gamepadDevice.empty()) {
      devices.push_back(gamepadDevice);
    }
  }

  return devices;
}

void IO::listInputDevices() {
  auto devices = Device::getAllDevices();

  std::cout << "=== Input Device Detection Results ===" << std::endl;

  for (const auto &device : devices) {
    std::cout << device.toString() << std::endl;
  }

  std::cout << "\n=== Summary ===" << std::endl;

  auto keyboards = Device::findKeyboards();
  std::cout << "Keyboards found: " << keyboards.size() << std::endl;
  for (const auto &kb : keyboards) {
    std::cout << "  - " << kb.name << " (" << (kb.confidence * 100) << "%)"
              << std::endl;
  }

  auto mice = Device::findMice();
  std::cout << "Mice found: " << mice.size() << std::endl;
  for (const auto &mouse : mice) {
    std::cout << "  - " << mouse.name << " (" << (mouse.confidence * 100)
              << "%)" << std::endl;
  }

  auto gamepads = Device::findGamepads();
  std::cout << "Gamepads/Joysticks found: " << gamepads.size() << std::endl;
  for (const auto &gamepad : gamepads) {
    std::cout << "  - " << gamepad.name << " (" << (gamepad.confidence * 100)
              << "%)" << std::endl;
  }
}

// Updated constructor
IO::IO() {
  info("IO constructor called");

  XSetErrorHandler(IO::XErrorHandler);
  DisplayManager::Initialize();
  display = DisplayManager::GetDisplay();

  // Initialize KeyMap
  KeyMap::Initialize();

  InitKeyMap();
  mouseSensitivity = Configs::Get().Get<double>("Mouse.Sensitivity", 1.0);

  // Check if we should use the new EventListener
  useNewEventListener =
      Configs::Get().Get<bool>("IO.UseNewEventListener", false);

#ifdef __linux__
  if (display) {
    UpdateNumLockMask();

    if (useNewEventListener) {
      // Use new unified EventListener
      info("Using new unified EventListener");

      std::vector<std::string> devices;

      // Get all keyboard devices (including auxiliary keyboards)
      auto keyboardDevices = Device::findKeyboards();
      for (const auto& kb : keyboardDevices) {
        devices.push_back(kb.eventPath);
        info("Adding keyboard device: '{}' -> {} (confidence: {:.1f}%)",
             kb.name, kb.eventPath, kb.confidence * 100);
      }

      std::string mouseDevice = getMouseDevice();
      std::string gamepadDevice;

      if (!mouseDevice.empty() &&
          !Configs::Get().Get<bool>("Device.IgnoreMouse", false)) {
        // Only add mouse device if it's not already in the keyboard devices list
        bool alreadyAdded = false;
        for (const auto& kb_device : keyboardDevices) {
          if (kb_device.eventPath == mouseDevice) {
            alreadyAdded = true;
            break;
          }
        }
        if (!alreadyAdded) {
          devices.push_back(mouseDevice);
          info("Adding mouse device: {}", mouseDevice);
        }
      }

      bool enableGamepad =
          Configs::Get().Get<bool>("Device.EnableGamepad", false);
      if (enableGamepad) {
        gamepadDevice = getGamepadDevice();
        if (!gamepadDevice.empty()) {
          devices.push_back(gamepadDevice);
          info("Adding gamepad device: {}", gamepadDevice);
        }
      }

      if (!devices.empty()) {
        try {
          eventListener = std::make_unique<EventListener>();
          eventListener->SetupUinput();

          // Set mouse and scroll sensitivity
          eventListener->SetMouseSensitivity(mouseSensitivity);
          eventListener->SetScrollSpeed(
              Configs::Get().Get<double>("Mouse.ScrollSpeed", 1.0));

          globalEvdev = true;
          eventListener->Start(devices, true);
          info("Successfully started unified EventListener with {} devices",
               devices.size());
          uinputFd = eventListener->uinputFd;
        } catch (const std::exception &e) {
          error("Failed to start unified EventListener: {}", e.what());
          globalEvdev = false;
        }
      } else {
        globalEvdev = false;
        error("No input devices found for EventListener");
      }
    } else {
      // Use old separate listeners
      // Initialize keyboard device
      std::string keyboardDevice = getKeyboardDevice();
      if (!keyboardDevice.empty()) {
        try {
          info("Using keyboard device: {}", keyboardDevice);
          SetupUinputDevice();
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          StartEvdevHotkeyListener(keyboardDevice);
          info("Successfully started evdev hotkey listener for keyboard");
        } catch (const std::exception &e) {
          error("Failed to start evdev keyboard listener: {}", e.what());
          globalEvdev = false;
        }
      } else {
        globalEvdev = false;
        error("Failed to find a suitable keyboard device");
      }

      // Initialize mouse device
      std::string mouseDevice = getMouseDevice();
      if (!mouseDevice.empty() && mouseDevice != keyboardDevice &&
          !Configs::Get().Get<bool>("Device.IgnoreMouse", false)) {
        try {
          info("Using mouse device: {}", mouseDevice);
          StartEvdevMouseListener(mouseDevice);
          info("Successfully started evdev mouse listener");
        } catch (const std::exception &e) {
          error("Failed to start evdev mouse listener: {}", e.what());
        }
      } else if (mouseDevice.empty()) {
        warning("No suitable mouse device found");
      }

      // Initialize gamepad device if requested
      bool enableGamepad =
          Configs::Get().Get<bool>("Device.EnableGamepad", false);
      if (enableGamepad) {
        std::string gamepadDevice = getGamepadDevice();
        if (!gamepadDevice.empty()) {
          try {
            info("Using gamepad device: {}", gamepadDevice);
            StartEvdevGamepadListener(gamepadDevice);
            info("Successfully started evdev gamepad listener");
          } catch (const std::exception &e) {
            error("Failed to start evdev gamepad listener: {}", e.what());
          }
        } else {
          warning(
              "Gamepad support enabled but no suitable gamepad device found");
        }
      }
    }

    // Fall back to X11 hotkeys if evdev initialization failed
    if (!globalEvdev) {
      timerRunning = true;
      try {
        timerThread = std::thread(&IO::MonitorHotkeys, this);
        info("Started X11 hotkey monitoring thread");
      } catch (const std::exception &e) {
        error("Failed to start X11 hotkey monitoring thread: {}", e.what());
        timerRunning = false;
      }
    }

    // Debug output - show what we detected
    if (Configs::Get().Get<bool>("Device.ShowDetectionResults", false)) {
      listInputDevices();
    }
  }
#endif
}
IO::~IO() {
  cleanup();
}

void IO::cleanup() {
  // Stop the hotkey monitoring thread
  if (timerRunning && timerThread.joinable()) {
    timerRunning = false;
    timerThread.join();
  }

  // Stop the evdev listener if it's running
  StopEvdevHotkeyListener();
  StopEvdevMouseListener();

  // Stop EventListener if using new event system
  if (eventListener) {
    eventListener->Stop();
  }

  // Ungrab all hotkeys before closing
#ifdef __linux__
  if (display && !globalEvdev) {
    Window root = DefaultRootWindow(display);

    // First, ungrab all hotkeys from the instance
    for (const auto &[id, hotkey] : instanceHotkeys) {
      if (hotkey.key != 0 && !hotkey.evdev) {
        KeyCode keycode = XKeysymToKeycode(display, hotkey.key);
        if (keycode != 0) {
          Ungrab(keycode, hotkey.modifiers, root);
        }
      }
    }

    // Then, ungrab all static hotkeys
    for (const auto &[id, hotkey] : hotkeys) {
      if (hotkey.key != 0 && !hotkey.evdev) {
        KeyCode keycode = XKeysymToKeycode(display, hotkey.key);
        if (keycode != 0) {
          Ungrab(keycode, hotkey.modifiers, root);
        }
      }
    }

    // Sync to ensure all ungrabs are processed
    XSync(display, x11::XFalse);
  }
#endif
  CleanupUinputDevice();
  // Don't close the display here, it's managed by DisplayManager
  display = nullptr;

  // Additional safety: Force ungrab any remaining evdev devices
#ifdef __linux__
  // Force cleanup of any remaining evdev resources
  info("Final cleanup completed - all devices should be ungrabbed");
#endif
  
  std::cout << "IO cleanup completed" << std::endl;
}

bool IO::SetupUinputDevice() {
  uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinputFd < 0) {
    std::cerr << "uinput: failed to open /dev/uinput: " << strerror(errno)
              << "\n";
    return false;
  }

  struct uinput_setup usetup = {};
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5678;
  strcpy(usetup.name, "havel-uinput-kb");
  // Enable key event support
  if (ioctl(uinputFd, UI_SET_EVBIT, EV_KEY) < 0)
    goto error;
  if (ioctl(uinputFd, UI_SET_EVBIT, EV_SYN) < 0)
    goto error;
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_SIDE);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_EXTRA);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_FORWARD);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_BACK);
  ioctl(uinputFd, UI_SET_EVBIT, EV_REL);
  ioctl(uinputFd, UI_SET_RELBIT, REL_WHEEL);
  ioctl(uinputFd, UI_SET_RELBIT, REL_HWHEEL);

  // Enable all possible key codes you might emit
  for (int i = 0; i < 256; ++i)
    ioctl(uinputFd, UI_SET_KEYBIT, i);

  if (ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0)
    goto error;
  if (ioctl(uinputFd, UI_DEV_CREATE) < 0)
    goto error;

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Allow it to init

  return true;

error:
  std::cerr << "uinput: device setup failed: " << strerror(errno) << "\n";
  close(uinputFd);
  uinputFd = -1;
  return false;
}
bool IO::GrabKeyboard() {
  if (!display)
    return false;

  struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000}; // 1ms

  // Try to grab keyboard, may need to wait for other processes to ungrab
  for (int i = 0; i < 1000; i++) {
    int result = XGrabKeyboard(display, DefaultRootWindow(display), x11::XTrue,
                               GrabModeAsync, GrabModeAsync, CurrentTime);
    if (result == x11::XSuccess) {
      info("Successfully grabbed entire keyboard after {} attempts", i + 1);
      return true;
    }

    nanosleep(&ts, nullptr);
  }

  error("Cannot grab keyboard after 1000 attempts");
  return false;
}
bool IO::Grab(Key input, unsigned int modifiers, Window root, bool grab,
              bool isMouse) {
  if (!display)
    return false;

  bool success = true;
  bool isButton = isMouse || (input >= Button1 && input <= 7);

  unsigned int modVariants[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};

  // Single loop - just grab the 4 variants of THIS key
  for (unsigned int variant : modVariants) {
    unsigned int finalMods = modifiers | variant;
    int result;

    if (isButton) {
      result = XGrabButton(display, input, finalMods, root, x11::XTrue,
                           ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
                           GrabModeAsync, x11::XNone, x11::XNone);
    } else {
      result = XGrabKey(display, input, finalMods, root, x11::XTrue,
                        GrabModeAsync, GrabModeAsync);
    }

    if (result != x11::XSuccess) {
      success = false;
    }
  }

  XSync(display, x11::XFalse);
  return success;
}

bool IO::GrabAllHotkeys() {
  if (!display)
    return false;

  UpdateNumLockMask(); // Once at start

  unsigned int modVariants[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};

  bool success = true;

  // Single pass through your configured hotkeys
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.evdev || !hotkey.grab)
      continue; // Skip evdev hotkeys

    // Grab 4 variants of each configured hotkey
    for (unsigned int variant : modVariants) {
      unsigned int finalMods = hotkey.modifiers | variant;

      int result =
          XGrabKey(display, hotkey.key, finalMods, DefaultRootWindow(display),
                   x11::XTrue, GrabModeAsync, GrabModeAsync);
      if (result != x11::XSuccess) {
        success = false;
      }
    }
  }

  XSync(display, x11::XFalse);
  return success;
}

void IO::Ungrab(Key input, unsigned int modifiers, Window root, bool isMouse) {
  if (!display)
    return;

  bool isButton = isMouse || (input >= Button1 && input <= 7);

  unsigned int modVariants[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};

  for (unsigned int variant : modVariants) {
    unsigned int finalMods = modifiers | variant;

    if (isButton) {
      XUngrabButton(display, input, finalMods, root);
    } else {
      XUngrabKey(display, input, finalMods, root);
    }
  }

  XSync(display, x11::XFalse);
}

void IO::UngrabAll() {
  if (!display)
    return;
  XUngrabKey(display, AnyKey, AnyModifier, DefaultRootWindow(display));
  XUngrabButton(display, AnyButton, AnyModifier, DefaultRootWindow(display));
  XSync(display, x11::XFalse);
}

// Fast version - no retries
bool IO::FastGrab(Key input, unsigned int modifiers, Window root) {
  if (!display)
    return false;

  unsigned int variants[] = {modifiers, modifiers | LockMask,
                             modifiers | numlockmask,
                             modifiers | numlockmask | LockMask};

  // Just try once, no retries
  for (unsigned int mods : variants) {
    XGrabKey(display, input, mods, root, x11::XTrue, GrabModeAsync,
             GrabModeAsync);
  }

  return true;
}
bool IO::ModifierMatch(unsigned int expected, unsigned int actual) {
  return CLEANMASK(expected) == CLEANMASK(actual);
}
void IO::MonitorHotkeys() {
#ifdef __linux__
  info("Starting X11 hotkey monitoring thread");

  // Lock scope for initialization
  {
    std::lock_guard<std::mutex> lock(x11Mutex);
    if (!display) {
      error("Display is null, cannot monitor hotkeys");
      return;
    }
  }

  if (!XInitThreads()) {
    error("Failed to initialize X11 threading support");
    return;
  }

  XEvent event;
  Window root;

  // Initialize root window with lock
  {
    std::lock_guard<std::mutex> lock(x11Mutex);
    root = DefaultRootWindow(display);
    XSelectInput(display, root, KeyPressMask | KeyReleaseMask);
  }

  // Helper function to check if a keysym is a modifier
  constexpr auto IsModifierKey = [](KeySym ks) -> bool {
    return ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L ||
           ks == XK_Control_R || ks == XK_Alt_L || ks == XK_Alt_R ||
           ks == XK_Meta_L || ks == XK_Meta_R || ks == XK_Super_L ||
           ks == XK_Super_R || ks == XK_Hyper_L || ks == XK_Hyper_R ||
           ks == XK_Caps_Lock || ks == XK_Shift_Lock || ks == XK_Num_Lock ||
           ks == XK_Scroll_Lock;
  };

  // Pre-allocate containers
  std::vector<std::function<void()>> callbacks;
  callbacks.reserve(16);

  // Error message constants
  static const std::string callback_error_prefix = "Error in hotkey callback: ";
  static const std::string event_error_prefix = "Error processing X11 event: ";

  constexpr unsigned int relevantModifiers =
      ShiftMask | LockMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask;

  try {
    while (timerRunning) {
      int pendingEvents = 0;

      // Check for events with minimal lock
      {
        std::lock_guard<std::mutex> lock(x11Mutex);
        if (!display) {
          error("Display connection lost");
          break;
        }
        pendingEvents = XPending(display);
      }

      if (pendingEvents == 0) {
        // No events, sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      // Process all pending events in batch
      for (int i = 0; i < pendingEvents && timerRunning; ++i) {
        // Get next event with lock
        {
          std::lock_guard<std::mutex> lock(x11Mutex);
          if (!display || XNextEvent(display, &event) != 0) {
            error("XNextEvent failed - X11 connection error");
            timerRunning = false;
            break;
          }
        }

        try {
          // Process event without lock (no X11 calls here)
          if (event.type != x11::XKeyPress && event.type != x11::XKeyRelease) {
            continue;
          }

          const bool isDown = (event.type == x11::XKeyPress);
          const XKeyEvent *keyEvent = &event.xkey;

          // Process hotkeys first
          bool shouldForwardKey = true;
          const unsigned int cleanedState = keyEvent->state & relevantModifiers;
          callbacks.clear();

          // Find matching hotkeys
          {
            std::scoped_lock<std::timed_mutex> lock(hotkeyMutex);
            for (const auto &[id, hotkey] : hotkeys) {
              if (!hotkey.enabled || hotkey.evdev) // Skip evdev hotkeys in X11
                continue;

              if (hotkey.key == static_cast<Key>(keyEvent->keycode) &&
                  static_cast<int>(cleanedState) == hotkey.modifiers) {

                // Check event type
                if ((hotkey.eventType == HotkeyEventType::Down && !isDown) ||
                    (hotkey.eventType == HotkeyEventType::Up && isDown)) {
                  continue;
                }

                if (hotkey.callback) {
                  info("Executing hotkey callback: {} key: {} modifiers: {}",
                       hotkey.alias, hotkey.key, hotkey.modifiers);
                  callbacks.emplace_back(hotkey.callback);
                }

                if (hotkey.grab) {
                  shouldForwardKey = false;
                }
              }
            }
          }

          // Execute callbacks outside all locks
          for (const auto &callback : callbacks) {
            try {
              callback();
            } catch (const std::exception &e) {
              error(callback_error_prefix + e.what());
            } catch (...) {
              error("Unknown error in hotkey callback");
            }
          }

          // Forward the key event if no hotkey grabbed it
          if (shouldForwardKey) {
            SendUInput(keyEvent->keycode, isDown);
          }

        } catch (const std::exception &e) {
          error(event_error_prefix + e.what());
        } catch (...) {
          error("Unknown error processing X11 event");
        }
      }
    }
  } catch (const std::exception &e) {
    error(std::string("Fatal error in hotkey monitoring: ") + e.what());
    timerRunning = false;
  } catch (...) {
    error("Unknown fatal error in hotkey monitoring");
    timerRunning = false;
  }

  info("Hotkey monitoring thread stopped");
#endif // __linux__
}
// X11 hotkey monitoring thread
void IO::InitKeyMap() {
  // Basic implementation of key map
  std::cout << "Initializing key map" << std::endl;

  // Initialize key mappings for common keys
#ifdef __linux__
  keyMap["esc"] = XK_Escape;
  keyMap["enter"] = XK_Return;
  keyMap["space"] = XK_space;
  keyMap["tab"] = XK_Tab;
  keyMap["backspace"] = XK_BackSpace;
  keyMap["ctrl"] = XK_Control_L;
  keyMap["alt"] = XK_Alt_L;
  keyMap["shift"] = XK_Shift_L;
  keyMap["win"] = XK_Super_L;
  keyMap["lwin"] = XK_Super_L; // Add alias for Left Win
  keyMap["rwin"] = XK_Super_R; // Add Right Win
  keyMap["up"] = XK_Up;
  keyMap["down"] = XK_Down;
  keyMap["left"] = XK_Left;
  keyMap["right"] = XK_Right;
  keyMap["delete"] = XK_Delete;
  keyMap["insert"] = XK_Insert;
  keyMap["home"] = XK_Home;
  keyMap["end"] = XK_End;
  keyMap["pageup"] = XK_Page_Up;
  keyMap["pagedown"] = XK_Page_Down;
  keyMap["printscreen"] = XK_Print;
  keyMap["scrolllock"] = XK_Scroll_Lock;
  keyMap["pause"] = XK_Pause;
  keyMap["capslock"] = XK_Caps_Lock;
  keyMap["numlock"] = XK_Num_Lock;
  keyMap["menu"] = XK_Menu; // Add Menu key

  // Numpad keys
  keyMap["kp_0"] = XK_KP_0;
  keyMap["kp_1"] = XK_KP_1;
  keyMap["kp_2"] = XK_KP_2;
  keyMap["kp_3"] = XK_KP_3;
  keyMap["kp_4"] = XK_KP_4;
  keyMap["kp_5"] = XK_KP_5;
  keyMap["kp_6"] = XK_KP_6;
  keyMap["kp_7"] = XK_KP_7;
  keyMap["kp_8"] = XK_KP_8;
  keyMap["kp_9"] = XK_KP_9;
  keyMap["kp_insert"] = XK_KP_Insert;      // KP 0
  keyMap["kp_end"] = XK_KP_End;            // KP 1
  keyMap["kp_down"] = XK_KP_Down;          // KP 2
  keyMap["kp_pagedown"] = XK_KP_Page_Down; // KP 3
  keyMap["kp_left"] = XK_KP_Left;          // KP 4
  keyMap["kp_begin"] = XK_KP_Begin;        // KP 5
  keyMap["kp_right"] = XK_KP_Right;        // KP 6
  keyMap["kp_home"] = XK_KP_Home;          // KP 7
  keyMap["kp_up"] = XK_KP_Up;              // KP 8
  keyMap["kp_pageup"] = XK_KP_Page_Up;     // KP 9
  keyMap["kp_delete"] = XK_KP_Delete;      // KP Decimal
  keyMap["kp_decimal"] = XK_KP_Decimal;
  keyMap["kp_add"] = XK_KP_Add;
  keyMap["kp_subtract"] = XK_KP_Subtract;
  keyMap["kp_multiply"] = XK_KP_Multiply;
  keyMap["kp_divide"] = XK_KP_Divide;
  keyMap["kp_enter"] = XK_KP_Enter;

  // Function keys
  keyMap["f1"] = XK_F1;
  keyMap["f2"] = XK_F2;
  keyMap["f3"] = XK_F3;
  keyMap["f4"] = XK_F4;
  keyMap["f5"] = XK_F5;
  keyMap["f6"] = XK_F6;
  keyMap["f7"] = XK_F7;
  keyMap["f8"] = XK_F8;
  keyMap["f9"] = XK_F9;
  keyMap["f10"] = XK_F10;
  keyMap["f11"] = XK_F11;
  keyMap["f12"] = XK_F12;

  // Media keys (using XF86keysym.h - ensure it's included)
  keyMap["volumeup"] = XF86XK_AudioRaiseVolume;
  keyMap["volumedown"] = XF86XK_AudioLowerVolume;
  keyMap["mute"] = XF86XK_AudioMute;
  keyMap["play"] = XF86XK_AudioPlay;
  keyMap["pause"] = XF86XK_AudioPause;
  keyMap["playpause"] = XF86XK_AudioPlay; // Often mapped to the same key
  keyMap["stop"] = XF86XK_AudioStop;
  keyMap["prev"] = XF86XK_AudioPrev;
  keyMap["next"] = XF86XK_AudioNext;

  // Punctuation and symbols
  keyMap["comma"] = XK_comma; // Add comma
  keyMap["period"] = XK_period;
  keyMap["semicolon"] = XK_semicolon;
  keyMap["slash"] = XK_slash;
  keyMap["backslash"] = XK_backslash;
  keyMap["bracketleft"] = XK_bracketleft;
  keyMap["bracketright"] = XK_bracketright;
  keyMap["minus"] = XK_minus; // Add minus
  keyMap["equal"] = XK_equal; // Add equal
  keyMap["grave"] = XK_grave; // Tilde key (~)
  keyMap["apostrophe"] = XK_apostrophe;

  // Letter keys (a-z)
  for (char c = 'a'; c <= 'z'; ++c) {
    keyMap[std::string(1, c)] = XStringToKeysym(std::string(1, c).c_str());
  }

  // Number keys (0-9)
  for (char c = '0'; c <= '9'; ++c) {
    keyMap[std::string(1, c)] = XStringToKeysym(std::string(1, c).c_str());
  }

  // Button names (for mouse events)
  keyMap["button1"] = Button1;
  keyMap["button2"] = Button2;
  keyMap["button3"] = Button3;
  keyMap["button4"] = Button4; // Wheel up
  keyMap["button5"] = Button5; // Wheel down

#endif
}

void IO::removeSpecialCharacters(str &keyName) {
  // Define the characters to remove
  const std::string charsToRemove = "^+!#*&";

  // Remove characters
  keyName.erase(std::remove_if(keyName.begin(), keyName.end(),
                               [&charsToRemove](char c) {
                                 return charsToRemove.find(c) !=
                                        std::string::npos;
                               }),
                keyName.end());
}
bool IO::EmitClick(int btnCode, int action) {
  // Use EventListener's uinput if available, otherwise use old method
  if (eventListener && useNewEventListener) {
    // Send events through EventListener
    switch (action) {
    case 0: // Release
      eventListener->SendUinputEvent(EV_KEY, btnCode, 0);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      return true;

    case 1: // Hold
      eventListener->SendUinputEvent(EV_KEY, btnCode, 1);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      return true;

    case 2: // Click (FAST)
      eventListener->SendUinputEvent(EV_KEY, btnCode, 1);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      eventListener->SendUinputEvent(EV_KEY, btnCode, 0);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      return true;

    default:
      error("Invalid mouse action: {}", action);
      return false;
    }
  }

  // Fallback to old method using mouseUinputFd
  if (mouseUinputFd < 0) {
    return false;
  }

  static std::mutex uinputMutex;
  std::lock_guard<std::mutex> lock(uinputMutex);

  auto writeEvent = [&](uint16_t type, uint16_t code, int32_t value) -> bool {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    input_event ev = {
        .time = {.tv_sec = ts.tv_sec, .tv_usec = ts.tv_nsec / 1000},
        .type = type,
        .code = code,
        .value = value};
    return write(mouseUinputFd, &ev, sizeof(ev)) == sizeof(ev);
  };

  auto sync = [&]() -> bool { return writeEvent(EV_SYN, SYN_REPORT, 0); };

  switch (action) {
  case 0: // Release
    return writeEvent(EV_KEY, btnCode, 0) && sync();

  case 1: // Hold
    return writeEvent(EV_KEY, btnCode, 1) && sync();

  case 2: // Click (FAST)
    return writeEvent(EV_KEY, btnCode, 1) && sync() &&
           writeEvent(EV_KEY, btnCode, 0) && sync();
    // No delay = instant click ⚡

  default:
    error("Invalid mouse action: {}", action);
    return false;
  }
}
bool IO::MouseMoveTo(int targetX, int targetY, int speed, float accel) {
  if (mouseUinputFd < 0)
    return false;

  // Get screen dimensions for absolute coordinates
  auto monitors = DisplayManager::GetMonitors();
  if (monitors.empty())
    return false;

  // Assume primary monitor for coordinate system
  int screenWidth = monitors[0].width;
  int screenHeight = monitors[0].height;

  // Get current position
  Display *display = DisplayManager::GetDisplay();
  int currentX = 0, currentY = 0;
  if (display) {
    ::Window root, child;
    int rootX, rootY;
    unsigned int mask;
    XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rootX,
                  &rootY, &currentX, &currentY, &mask);
  }

  // Animate to target with steps
  int steps = std::max(10, static_cast<int>(std::abs(targetX - currentX) +
                                            std::abs(targetY - currentY)) /
                               speed);

  for (int i = 0; i <= steps; i++) {
    double progress = static_cast<double>(i) / steps;

    // Ease in-out curve
    double easedProgress;
    if (progress < 0.5) {
      easedProgress = 2 * progress * progress;
    } else {
      easedProgress = -1 + (4 - 2 * progress) * progress;
    }

    int currentTargetX =
        currentX + static_cast<int>((targetX - currentX) * easedProgress);
    int currentTargetY =
        currentY + static_cast<int>((targetY - currentY) * easedProgress);

    // Send absolute position events
    input_event ev = {};

    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = (currentTargetX * 65535) / screenWidth; // Scale to device range
    write(mouseUinputFd, &ev, sizeof(ev));

    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = (currentTargetY * 65535) / screenHeight; // Scale to device range
    write(mouseUinputFd, &ev, sizeof(ev));

    // Sync
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(mouseUinputFd, &ev, sizeof(ev));

    // Sleep based on acceleration
    double currentSpeed =
        speed * (1.0 + (accel - 1.0) * std::min(progress * 2.0, 1.0));
    int sleepMs = std::max(1, static_cast<int>(20 / currentSpeed));
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
  }

  return true;
}
bool IO::MouseMove(int dx, int dy, int speed, float accel) {
  // Use EventListener's uinput if available, otherwise use old method
  if (eventListener && useNewEventListener) {
    // Apply speed and acceleration (but NO sensitivity scaling)
    if (speed <= 0)
      speed = 1;
    if (accel <= 0.0f)
      accel = 1.0f;

    // Apply acceleration curve
    float acceleratedSpeed = speed * std::pow(accel, 1.5f);

    // Calculate final movement
    int actualDx = static_cast<int>(dx * acceleratedSpeed);
    int actualDy = static_cast<int>(dy * acceleratedSpeed);

    // Preserve direction for tiny movements
    if (actualDx == 0 && dx != 0)
      actualDx = (dx > 0) ? 1 : -1;
    if (actualDy == 0 && dy != 0)
      actualDy = (dy > 0) ? 1 : -1;

    debug("Mouse move: {} {}", actualDx, actualDy);

    // Send using EventListener
    if (actualDx != 0) {
      eventListener->SendUinputEvent(EV_REL, REL_X, actualDx);
    }
    if (actualDy != 0) {
      eventListener->SendUinputEvent(EV_REL, REL_Y, actualDy);
    }
    // Send sync event
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    return true;
  }

  // Fallback to old method using mouseUinputFd
  if (mouseUinputFd < 0) {
    error("mouseUinputFd is borked: {}", mouseUinputFd);
    return false;
  }

  static std::mutex uinputMutex;
  std::lock_guard<std::mutex> ioLock(uinputMutex);

  // Apply speed and acceleration (but NO sensitivity scaling)
  if (speed <= 0)
    speed = 1;
  if (accel <= 0.0f)
    accel = 1.0f;

  // Apply acceleration curve
  float acceleratedSpeed = speed * std::pow(accel, 1.5f);

  // Calculate final movement
  int actualDx = static_cast<int>(dx * acceleratedSpeed);
  int actualDy = static_cast<int>(dy * acceleratedSpeed);

  // Preserve direction for tiny movements
  if (actualDx == 0 && dx != 0)
    actualDx = (dx > 0) ? 1 : -1;
  if (actualDy == 0 && dy != 0)
    actualDy = (dy > 0) ? 1 : -1;

  debug("Mouse move: {} {}", actualDx, actualDy);

  // Send X movement
  if (actualDx != 0) {
    input_event ev = {};
    ev.type = EV_REL;
    ev.code = REL_X;
    ev.value = actualDx;
    if (write(mouseUinputFd, &ev, sizeof(ev)) < 0) {
      error("Failed to write X movement: {}", strerror(errno));
      return false;
    }
  }

  // Send Y movement
  if (actualDy != 0) {
    input_event ev = {};
    ev.type = EV_REL;
    ev.code = REL_Y;
    ev.value = actualDy;
    if (write(mouseUinputFd, &ev, sizeof(ev)) < 0) {
      error("Failed to write Y movement: {}", strerror(errno));
      return false;
    }
  }

  // Send sync event
  input_event ev = {};
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  if (write(mouseUinputFd, &ev, sizeof(ev)) < 0) {
    error("Failed to write sync event: {}", strerror(errno));
    return false;
  }

  return true;
}
// Set mouse sensitivity (0.1 - 10.0)
void IO::SetMouseSensitivity(double sensitivity) {
  std::lock_guard<std::mutex> lock(mouseMutex);
  // Clamp sensitivity between 0.1 and 10.0
  sensitivity = std::max(0.1, std::min(10.0, sensitivity));

  // Try to set hardware sensitivity first
  if (!globalEvdev) {
    if (xinput2Available) {
      SetHardwareMouseSensitivity(sensitivity);
    }
    mouseSensitivity = sensitivity;
  } else {
    // Keep them in sync in case we need to fall back later
    mouseSensitivity = sensitivity;
  }
}

bool IO::InitializeXInput2() {
  if (!display) {
    error("Cannot initialize XInput2: No display");
    return false;
  }

  int xi_opcode, event, xi_error;
  if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event,
                       &xi_error)) {
    error("X Input extension not available");
    return false;
  }

  // Check XInput2 version
  int major = 2, minor = 0;
  if (XIQueryVersion(display, &major, &minor) != x11::XSuccess) {
    error("XInput2 not supported by server");
    return false;
  }

  // Find the first pointer device that supports XInput2
  XIDeviceInfo *devices;
  int ndevices;
  devices = XIQueryDevice(display, XIAllDevices, &ndevices);

  for (int i = 0; i < ndevices; i++) {
    XIDeviceInfo *dev = &devices[i];
    if (dev->use == XISlavePointer || dev->use == XIFloatingSlave) {
      xinput2DeviceId = dev->deviceid;
      break;
    }
  }

  XIFreeDeviceInfo(devices);

  if (xinput2DeviceId == -1) {
    error("No suitable XInput2 pointer device found");
    return false;
  }

  return true;
}

bool IO::SetHardwareMouseSensitivity(double sensitivity) {
  if (!display || xinput2DeviceId == -1) {
    return false;
  }

  // Clamp sensitivity to a reasonable range for hardware
  sensitivity = std::max(0.1, std::min(10.0, sensitivity));

  // Convert sensitivity to X's acceleration (sensitivity^2 for better curve)
  double accel_numerator = sensitivity * sensitivity * 10.0;
  double accel_denominator = 10.0;
  double threshold = 0.0; // No threshold for continuous acceleration

  // Set the device's acceleration
  XDevice *dev = XOpenDevice(display, xinput2DeviceId);
  if (!dev) {
    error("Failed to open XInput2 device");
    return false;
  }

  XDeviceControl *control =
      (XDeviceControl *)XGetDeviceControl(display, dev, DEVICE_RESOLUTION);
  if (!control) {
    XCloseDevice(display, dev);
    return false;
  }

  XChangePointerControl(display, x11::XTrue, x11::XTrue,
                        (int)(accel_numerator * 10),
                        (int)(accel_denominator * 10), (int)threshold);

  XFree(control);
  XCloseDevice(display, dev);

  info("Set hardware mouse sensitivity to: {}", sensitivity);
  return true;
}

// Get current mouse sensitivity
double IO::GetMouseSensitivity() const {
  std::lock_guard<std::mutex> lock(mouseMutex);
  return mouseSensitivity;
}

// Set scroll speed (0.1 - 10.0)
void IO::SetScrollSpeed(double speed) {
  std::lock_guard<std::mutex> lock(mouseMutex);
  // Clamp scroll speed between 0.1 and 10.0
  scrollSpeed = std::max(0.1, std::min(10.0, speed));
  info("Scroll speed set to: {}", scrollSpeed);
}

// Get current scroll speed
double IO::GetScrollSpeed() const {
  std::lock_guard<std::mutex> lock(mouseMutex);
  return scrollSpeed;
}

// Enhanced mouse movement with custom sensitivity
bool IO::MouseMoveSensitive(int dx, int dy, int baseSpeed, float accel) {
  std::lock_guard<std::mutex> lock(mouseMutex);

  // Apply sensitivity and acceleration
  double adjustedDx = dx * mouseSensitivity;
  double adjustedDy = dy * mouseSensitivity;

  // Apply acceleration if enabled (accel > 1.0)
  if (accel > 1.0f) {
    double distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 1.0) {
      double factor = 1.0 + (accel - 1.0) * (distance / 100.0);
      adjustedDx *= factor;
      adjustedDy *= factor;
    }
  }

  // Call the original MouseMove with adjusted values
  return MouseMove(static_cast<int>(adjustedDx), static_cast<int>(adjustedDy),
                   baseSpeed, 1.0f);
}

bool IO::Scroll(int dy, int dx) {
  std::lock_guard<std::mutex> lock(mouseMutex);

  // Apply scroll speed
  if (dy != 0)
    dy = static_cast<int>(dy * scrollSpeed);
  if (dx != 0)
    dx = static_cast<int>(dx * scrollSpeed);

  if (uinputFd < 0)
    return false;

  input_event ev = {};
  auto emitScroll = [&](uint16_t code, int value) -> bool {
    ev.type = EV_REL;
    ev.code = code;
    ev.value = value;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
    return true;
  };
  debug("Scrolling: {} {}", dx, dy);
  // Emit relative scrolls
  if (dy != 0 && !emitScroll(REL_WHEEL, dy))
    return false;
  if (dx != 0 && !emitScroll(REL_HWHEEL, dx))
    return false;

  // Sync event
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  if (write(uinputFd, &ev, sizeof(ev)) < 0)
    return false;

  return true;
}

void IO::SendX11Key(const std::string &keyName, bool press) {
#if defined(__linux__)
  if (!display) {
    std::cerr << "X11 display not initialized!" << std::endl;
    return;
  }

  Key keycode = GetKeyCode(keyName);

  // Add state tracking to X11 as well
  if (press) {
    if (!TryPressKey(keycode))
      return; // Already pressed
  } else {
    if (!TryReleaseKey(keycode))
      return; // Not pressed
  }

  info("Sending key: " + keyName + " (" + std::to_string(keycode) + ")");
  XTestFakeKeyEvent(display, keycode, press, CurrentTime);
  XFlush(display);
#endif
}
void IO::SendUInput(int keycode, bool down) {
  static std::mutex uinputMutex;
  if (uinputFd < 0)
    return;
  std::lock_guard<std::mutex> lock(uinputMutex);

  // State tracking check
  if (down) {
    if (!TryPressKey(keycode))
      return; // Already pressed
  } else {
    if (!TryReleaseKey(keycode))
      return; // Not pressed
  }

  if (uinputFd < 0) {
    if (!SetupUinputDevice()) {
      error("Failed to initialize uinput device");
      return;
    }
  }

  struct input_event ev{};
  gettimeofday(&ev.time, nullptr);

  ev.type = EV_KEY;
  ev.code = keycode;
  ev.value = down ? 1 : 0;

  if (Configs::Get().GetVerboseKeyLogging())
    debug("Sending uinput key: {} ({})", keycode, down);

  if (write(uinputFd, &ev, sizeof(ev)) < 0) {
    error("Failed to write key event");
    return;
  }

  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;

  if (write(uinputFd, &ev, sizeof(ev)) < 0) {
    error("Failed to write sync event");
  }
}
// State tracking implementation
bool IO::TryPressKey(int keycode) {
  std::lock_guard<std::mutex> lock(keyStateMutex);
  if (pressedKeys.count(keycode)) {
    if (Configs::Get().GetVerboseKeyLogging())
      debug("Key " + std::to_string(keycode) + " already pressed, ignoring");
    return false; // Already pressed
  }
  pressedKeys.insert(keycode);
  return true; // OK to press
}

bool IO::TryReleaseKey(int keycode) {
  std::lock_guard<std::mutex> lock(keyStateMutex);
  if (!pressedKeys.count(keycode)) {
    if (Configs::Get().GetVerboseKeyLogging())
      debug("Key " + std::to_string(keycode) +
            " not pressed, ignoring release");
    return false; // Not pressed
  }
  pressedKeys.erase(keycode);
  return true; // OK to release
}

void IO::EmergencyReleaseAllKeys() {
  std::lock_guard<std::mutex> lock(keyStateMutex);
  std::cerr << "EMERGENCY: Releasing " << pressedKeys.size() << " stuck keys\n";

  // Copy the set to avoid iterator invalidation
  std::set<int> keysToRelease = pressedKeys;
  pressedKeys.clear();

  // Release without state checking (bypass TryReleaseKey)
  for (int keycode : keysToRelease) {
    if (uinputFd >= 0) {
      struct input_event ev{};
      gettimeofday(&ev.time, nullptr);
      ev.type = EV_KEY;
      ev.code = keycode;
      ev.value = 0; // RELEASE
      write(uinputFd, &ev, sizeof(ev));

      ev.type = EV_SYN;
      ev.code = SYN_REPORT;
      ev.value = 0;
      write(uinputFd, &ev, sizeof(ev));
    }
  }
}

// OPTIMIZED: Method to send keys with state tracking and event batching
void IO::Send(cstr keys) {
#if defined(WINDOWS)
  // Windows implementation unchanged
  for (size_t i = 0; i < keys.length(); ++i) {
    if (keys[i] == '{') {
      size_t end = keys.find('}', i);
      if (end != std::string::npos) {
        std::string sequence = keys.substr(i + 1, end - i - 1);
        if (sequence == "Alt down") {
          keybd_event(VK_MENU, 0, 0, 0);
        } else if (sequence == "Alt up") {
          keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        } else if (sequence == "Ctrl down") {
          keybd_event(VK_CONTROL, 0, 0, 0);
        } else if (sequence == "Ctrl up") {
          keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        } else if (sequence == "Shift down") {
          keybd_event(VK_SHIFT, 0, 0, 0);
        } else if (sequence == "Shift up") {
          keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
        } else {
          int virtualKey = StringToVirtualKey(sequence);
          if (virtualKey) {
            keybd_event(virtualKey, 0, 0, 0);
            keybd_event(virtualKey, 0, KEYEVENTF_KEYUP, 0);
          }
        }
        i = end;
        continue;
      }
    }

    int virtualKey = StringToVirtualKey(std::string(1, keys[i]));
    if (virtualKey) {
      keybd_event(virtualKey, 0, 0, 0);
      keybd_event(virtualKey, 0, KEYEVENTF_KEYUP, 0);
    }
  }
#else
  // OPTIMIZATION #1: Pre-parse the key string once instead of scanning repeatedly
  auto tokens = ParseKeyString(keys);
  
  // Linux implementation with state tracking and optimizations
  bool useUinput = true;
  bool useX11 = false;
  std::vector<std::string> activeModifiers;
  std::unordered_map<std::string, std::string> modifierKeys = {
      {"ctrl", "LCtrl"},    {"rctrl", "RCtrl"}, {"shift", "LShift"},
      {"rshift", "RShift"}, {"alt", "LAlt"},    {"ralt", "RAlt"},
      {"meta", "LMeta"},    {"rmeta", "RMeta"},
  };

  auto SendKeyImpl = [&](const std::string &keyName, bool down) {
    if (useUinput || (!useX11 && globalEvdev)) {
      // OPTIMIZATION #3: Cache keycode lookups to avoid repeated string->keycode conversions
      int code = GetKeyCacheLookup(keyName);
      if (Configs::Get().GetVerboseKeyLogging()) {
        info("Sending key: " + keyName + " (" + std::to_string(down) +
             ") code: " + std::to_string(code));
      }
      if (code != -1) {
        // Use EventListener's uinput if available, otherwise use old method
        if (eventListener && useNewEventListener) {
          eventListener->SendUinputEvent(EV_KEY, code, down ? 1 : 0);
        } else {
          SendUInput(code, down); // Fallback to old method
        }
      }
    } else {
      SendX11Key(keyName, down);
    }
  };

  auto SendKey = [&](const std::string &keyName, bool down) {
    SendKeyImpl(keyName, down);
  };
  
  // Release interfering modifiers
  std::set<std::string> toRelease;
  if (eventListener) {
    auto modState = eventListener->GetModifierState();
    auto checkMod = [&](bool leftPressed, bool rightPressed, 
                        const std::string& leftKey, const std::string& rightKey) {
        if (leftPressed) toRelease.insert(leftKey);
        if (rightPressed) toRelease.insert(rightKey);
    };
    checkMod(modState.leftCtrl, modState.rightCtrl, "ctrl", "rctrl");
    checkMod(modState.leftShift, modState.rightShift, "shift", "rshift");
    checkMod(modState.leftAlt, modState.rightAlt, "alt", "ralt");
    checkMod(modState.leftMeta, modState.rightMeta, "meta", "rmeta");
  }
  
  for (const auto &mod : toRelease) {
      debug("Releasing modifier: {}", mod);
      SendKey(modifierKeys[mod], false);
  }

  // OPTIMIZATION #2: Process pre-parsed tokens instead of rescanning
  bool shouldSleep = Configs::Get().Get<bool>("Advanced.SlowKeyDelay", false);
  
  for (const auto& token : tokens) {
    switch (token.type) {
      case KeyToken::Modifier: {
        if (token.value == "toggle_uinput") {
          useUinput = !useUinput;
          if (Configs::Get().GetVerboseKeyLogging())
            debug(useUinput ? "Switched to uinput" : "Switched to X11");
        } else if (token.value == "toggle_x11") {
          useX11 = !useX11;
          if (Configs::Get().GetVerboseKeyLogging())
            debug(useX11 ? "Switched to X11" : "Switched to uinput");
        } else if (modifierKeys.count(token.value)) {
          SendKey(modifierKeys[token.value], true);
          activeModifiers.push_back(token.value);
        }
        break;
      }
      
      case KeyToken::Special: {
        if (token.value == "emergency_release" || token.value == "panic") {
          EmergencyReleaseAllKeys();
        }
        break;
      }
      
      case KeyToken::ModifierDown: {
        if (modifierKeys.count(token.value)) {
          SendKey(modifierKeys[token.value], true);
          activeModifiers.push_back(token.value);
        } else {
          SendKey(token.value, true);
        }
        break;
      }
      
      case KeyToken::ModifierUp: {
        if (modifierKeys.count(token.value)) {
          SendKey(modifierKeys[token.value], false);
          activeModifiers.erase(
              std::remove(activeModifiers.begin(), activeModifiers.end(), token.value),
              activeModifiers.end());
        } else {
          SendKey(token.value, false);
        }
        break;
      }
      
      case KeyToken::Key: {
        debug("Sending key: " + token.value);
        SendKey(token.value, true);
        // OPTIMIZATION #4: Only sleep if explicitly configured (removed default 100μs sleep)
        if (shouldSleep) {
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        SendKey(token.value, false);
        break;
      }
    }
  }

  // Release all held modifiers (fail-safe)
  for (const auto &mod : activeModifiers) {
    if (modifierKeys.count(mod)) {
      debug("Releasing modifier: " + mod);
      SendKey(modifierKeys[mod], false);
    } else {
      debug("Releasing key: " + mod);
      SendKey(mod, false);
    }
  }
  activeModifiers.clear();
#endif
}
bool IO::Suspend() {
  try {
    if (isSuspended) {
      // Resume (was suspended, now resuming)
      for (auto &[id, hotkey] : hotkeys) {
        if (!hotkey.enabled && !hotkey.suspend) {
          if (!hotkey.evdev) {
            Grab(hotkey.key, hotkey.modifiers, DisplayManager::GetRootWindow(),
                 hotkey.grab);
          }
          hotkey.enabled = true;
        }
      }

      if (hotkeyManager) {
        hotkeyManager->conditionalHotkeysEnabled = true;

        // Restore conditional hotkeys to their original state before suspension
        if (!suspendedConditionalHotkeyStates.empty()) {
          std::lock_guard<std::mutex> lock(hotkeyManager->getHotkeyMutex());
          // Restore to the original state before suspension
          for (const auto& state : suspendedConditionalHotkeyStates) {
            auto& ch = hotkeyManager->conditionalHotkeys;
            auto it = std::find_if(ch.begin(), ch.end(),
                                   [state](const auto& ch_item) {
                                     return ch_item.id == state.id;
                                   });
            if (it != ch.end()) {
              if (state.wasGrabbed && !it->currentlyGrabbed) {
                // Previously grabbed, now restore
                GrabHotkey(state.id);
                it->currentlyGrabbed = true;
              } else if (!state.wasGrabbed && it->currentlyGrabbed) {
                // Previously not grabbed, now ungrab
                UngrabHotkey(state.id);
                it->currentlyGrabbed = false;
              }
            }
          }
          suspendedConditionalHotkeyStates.clear();
        } else {
          // No saved state, reevaluate as before
          hotkeyManager->reevaluateConditionalHotkeys(*this);
        }
      }

      wasSuspended = false;
      isSuspended = false;
      return true;
    } else {
      // Suspend (was active, now suspending)
      for (auto &[id, hotkey] : hotkeys) {
        if (hotkey.enabled && !hotkey.suspend) {
          if (!hotkey.evdev) {
            Ungrab(hotkey.key, hotkey.modifiers, DisplayManager::GetRootWindow());
          }
          hotkey.enabled = false;
        }
      }

      if (hotkeyManager) {
        // Track the original state of conditional hotkeys before suspension and update their states
        hotkeyManager->conditionalHotkeysEnabled = false;
        std::lock_guard<std::mutex> lock(hotkeyManager->getHotkeyMutex());
        suspendedConditionalHotkeyStates.clear();
        for (auto& ch : hotkeyManager->conditionalHotkeys) {
          ConditionalHotkeyState state;
          state.id = ch.id;
          state.wasGrabbed = ch.currentlyGrabbed;
          suspendedConditionalHotkeyStates.push_back(state);

          // Ungrab during suspension and update the state
          if (ch.currentlyGrabbed) {
            UngrabHotkey(ch.id);
            ch.currentlyGrabbed = false;
          }
        }
      }

      wasSuspended = true;
      isSuspended = true;
      return true;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error in IO::Suspend: " << e.what() << std::endl;
    return false;
  }
}
// Method to suspend hotkeys
bool IO::Suspend(int id) {
  std::cout << "Suspending hotkey ID: " << id << std::endl;
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    it->second.enabled = false;
    return true;
  }
  return false;
}

// Method to resume hotkeys
bool IO::Resume(int id) {
  std::cout << "Resuming hotkey ID: " << id << std::endl;
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    it->second.enabled = true;
    return true;
  }
  return false;
}

// Static method to exit the application
void IO::ExitApp() {
  info("Static ExitApp called - initiating emergency shutdown sequence");

  // This is a static method, so we can't access instance members directly
  // However, we can use std::exit to ensure immediate termination
  std::exit(0);
}

// Helper function to parse mouse button from string
int IO::ParseMouseButton(const std::string &str) {
  if (str == "LButton" || str == "Button1")
    return BTN_LEFT;
  if (str == "RButton" || str == "Button2")
    return BTN_RIGHT;
  if (str == "MButton" || str == "Button3")
    return BTN_MIDDLE;
  if (str == "XButton1" || str == "Button6" || str == "Side1")
    return BTN_SIDE;
  if (str == "XButton2" || str == "Button7" || str == "Side2")
    return BTN_EXTRA;
  if (str == "WheelUp" || str == "ScrollUp" || str == "Button4")
    return 1; // Special value for wheel up
  if (str == "WheelDown" || str == "ScrollDown" || str == "Button5")
    return -1; // Special value for wheel down
  return 0;
}

bool IO::AddHotkey(const std::string &alias, Key key, int modifiers,
                   std::function<void()> callback) {
  std::lock_guard<std::timed_mutex> lock(hotkeyMutex);
  std::cout << "Adding hotkey: " << alias << std::endl;
  HotKey hotkey;
  hotkey.id = ++hotkeyCount;
  hotkey.alias = alias;
  hotkey.key = key;
  hotkey.modifiers = modifiers;
  hotkey.callback = callback;
  hotkey.enabled = true;
  hotkey.grab = false;
  hotkey.suspend = false;
  hotkey.success = true;
  hotkey.type = HotkeyType::Keyboard;
  hotkey.eventType = HotkeyEventType::Down;
  hotkeys[hotkeyCount] = hotkey;
  return true;
}

ParsedHotkey IO::ParseHotkeyString(const std::string &rawInput) {
  ParsedHotkey result;
  std::string hotkeyStr = rawInput;

  // Parse event type suffix (:up, :down, :N, etc.)
  size_t colonPos = hotkeyStr.rfind(':');
  if (colonPos != std::string::npos && colonPos + 1 < hotkeyStr.size()) {
    std::string suffix = hotkeyStr.substr(colonPos + 1);

    // Check if suffix is a number (repeat interval)
    bool isNumber =
        !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);

    if (isNumber) {
      // Parse as repeat interval in milliseconds
      try {
        result.repeatInterval = std::stoi(suffix);
        hotkeyStr = hotkeyStr.substr(0, colonPos);
      } catch (const std::exception &) {
        // Invalid number, treat as event type
        result.eventType = ParseHotkeyEventType(suffix);
        hotkeyStr = hotkeyStr.substr(0, colonPos);
      }
    } else {
      // Parse as event type
      result.eventType = ParseHotkeyEventType(suffix);
      hotkeyStr = hotkeyStr.substr(0, colonPos);
    }
  }

  // Parse @evdev prefix with regex
  std::regex evdevRegex(R"(@(.*))");
  std::smatch matches;
  if (std::regex_search(hotkeyStr, matches, evdevRegex)) {
    result.isEvdev = true;
    hotkeyStr = matches[1].str();
  }

  if (globalEvdev) {
    result.isEvdev = true;
  }

  // Parse modifiers and flags
  auto parsed = ParseModifiersAndFlags(hotkeyStr, result.isEvdev);
  result.keyPart = parsed.keyPart;
  result.modifiers = parsed.modifiers;
  result.grab = parsed.grab;
  result.suspend = parsed.suspend;
  result.repeat = parsed.repeat;
  result.wildcard = parsed.wildcard;
  result.isEvdev = parsed.isEvdev || result.isEvdev;
  result.isX11 = parsed.isX11;

  return result;
}

ParsedHotkey IO::ParseModifiersAndFlags(const std::string &input,
                                        bool isEvdev) {
  ParsedHotkey result;
  result.isEvdev = isEvdev;

  size_t i = 0;
  
  // Parse text-based modifiers (loop until no more found)
  std::vector<std::string> textModifiers = {"ctrl", "shift", "alt", "meta", "win"};
  bool foundTextModifier = false;
  
  do {
    foundTextModifier = false;
    
    for (const auto& mod : textModifiers) {
      if (i + mod.size() <= input.size() && 
          input.substr(i, mod.size()) == mod) {
        // Check if this is followed by '+' or end of string
        if (i + mod.size() == input.size() || 
            input[i + mod.size()] == '+') {
          // Found text modifier
          if (mod == "ctrl") {
            result.modifiers |= isEvdev ? (1 << 0) : ControlMask;
          } else if (mod == "shift") {
            result.modifiers |= isEvdev ? (1 << 1) : ShiftMask;
          } else if (mod == "alt") {
            result.modifiers |= isEvdev ? (1 << 2) : Mod1Mask;
          } else if (mod == "meta" || mod == "win") {
            result.modifiers |= isEvdev ? (1 << 3) : Mod4Mask;
          }
          
          i += mod.size();
          foundTextModifier = true;
          
          // Skip the '+' if present
          if (i < input.size() && input[i] == '+') {
            ++i;
          }
          
          break; // Break inner for loop to restart with new position
        }
      }
    }
  } while (foundTextModifier);
  
  while (i < input.size()) {
    char c = input[i];

    // Check for escaped/doubled characters (e.g., "++", "##")
    if (i + 1 < input.size() && input[i] == input[i + 1]) {
      // Found doubled character - treat remaining as literal key
      // Skip first character and treat rest as key
      result.keyPart = input.substr(i + 1);
      return result;
    }

    switch (c) {
    case '@':
      result.isEvdev = true;
      break;
    case '%':
      result.isX11 = true;
      break;
    case '^':
      result.modifiers |= isEvdev ? (1 << 0) : ControlMask;
      break;
    case '+':
      result.modifiers |= isEvdev ? (1 << 1) : ShiftMask;
      break;
    case '!':
      result.modifiers |= isEvdev ? (1 << 2) : Mod1Mask;
      break;
    case '#':
      result.modifiers |= isEvdev ? (1 << 3) : Mod4Mask;
      break;
    case '*':
      // Wildcard - ignore all current modifiers but don't set repeat=false
      result.wildcard = true;
      break;
    case '|':
      result.repeat = false;
      break;
    case '~':
      result.grab = false;
      break;
    case '$':
      result.suspend = true;
      break;
    default:
      // Not a modifier - rest is the key part
      result.keyPart = input.substr(i);
      return result;
    }
    ++i;
  }

  // If we got here, entire string was modifiers
  result.keyPart = "";
  return result;
}

KeyCode IO::ParseKeyPart(const std::string &keyPart, bool isEvdev) {
  if (keyPart.empty()) {
    return 0;
  }

  // Handle raw keycode (kc123)
  if (keyPart.length() > 2 && keyPart.substr(0, 2) == "kc") {
    try {
      int kc = std::stoi(keyPart.substr(2));
      if (kc > 0 && kc <= 767) {
        return kc;
      }
    } catch (const std::exception &) {
      return 0;
    }
    return 0;
  }

  // Handle evdev names
  if (isEvdev) {
    return EvdevNameToKeyCode(keyPart);
  }

  // Handle X11 names
  std::string keyLower = toLower(keyPart);
  return GetKeyCode(keyLower);
}

HotKey IO::AddHotkey(const std::string &rawInput, std::function<void()> action,
                     int id) {
  auto wrapped_action = [action, rawInput]() {
    if (Configs::Get().GetVerboseKeyLogging())
      info("Hotkey pressed: " + rawInput);
    action();
  };

  bool hasAction = static_cast<bool>(action);

  // Parse the hotkey string
  std::string processedInput = rawInput;
  int comboTimeWindow = 500; // Default value

  // Check for combo time window in format "alias::500"
  size_t dblColonPos = processedInput.rfind("::");
  if (dblColonPos != std::string::npos &&
      dblColonPos + 2 < processedInput.size()) {
    std::string timeStr = processedInput.substr(dblColonPos + 2);
    try {
      comboTimeWindow = std::stoi(timeStr);
      // Remove the ::number part from the alias
      processedInput = processedInput.substr(0, dblColonPos);
    } catch (const std::exception &) {
      // If conversion fails, keep the default value and don't modify the input
    }
  }

  ParsedHotkey parsed = ParseHotkeyString(processedInput);

  // Build base hotkey
  HotKey hotkey;
  hotkey.comboTimeWindow = comboTimeWindow;
  hotkey.modifiers = parsed.modifiers;
  hotkey.eventType = parsed.eventType;
  hotkey.evdev = parsed.isEvdev;
  hotkey.x11 = parsed.isX11;
  hotkey.callback = wrapped_action;
  hotkey.alias = rawInput;
  hotkey.action = "";
  hotkey.enabled = true;
  hotkey.grab = parsed.grab;
  hotkey.suspend = parsed.suspend;
  hotkey.repeat = parsed.repeat;
  hotkey.wildcard = parsed.wildcard;
  hotkey.success = false;
  hotkey.repeatInterval = parsed.repeatInterval;

  // Check for mouse gesture pattern
  bool isGesture = false;

  // Check if the key part looks like a gesture pattern
  // This could be predefined gestures like "circle", "square", "triangle", etc.
  // or mouse-specific direction patterns like "mouseleft,mouseright,mouseup,mousedown"
  std::string keyLower = toLower(parsed.keyPart);
  bool isPredefinedGesture = (keyLower == "circle" || keyLower == "square" || keyLower == "triangle" ||
                             keyLower == "zigzag" || keyLower == "check");

  // Check if it's a comma-separated direction pattern (may include mouse directions)
  bool hasComma = parsed.keyPart.find(',') != std::string::npos;

  // Check if it's a single mouse direction (using the new mouse-specific names)
  bool isMouseDirection = (keyLower == "mouseleft" || keyLower == "mouseright" ||
                          keyLower == "mouseup" || keyLower == "mousedown" ||
                          keyLower == "mouseupleft" || keyLower == "mouseupright" ||
                          keyLower == "mousedownleft" || keyLower == "mousedownright");

  // Check if it's a comma-separated pattern that looks like a gesture pattern
  bool isGesturePattern = false;
  if (hasComma) {
      // Parse the comma-separated values to see if they look like gesture directions
      std::string tempPattern = parsed.keyPart; // Don't convert to lower yet, to check original
      std::istringstream iss(tempPattern);
      std::string part;
      bool allPartsAreMouseDirections = true;

      while (std::getline(iss, part, ',')) {
          // Remove leading/trailing whitespace
          part.erase(0, part.find_first_not_of(" \t\n\r"));
          part.erase(part.find_last_not_of(" \t\n\r") + 1);

          std::string lowerPart = toLower(part);

          // Check if this part is a mouse direction
          bool isMouseDir = (lowerPart == "mouseleft" || lowerPart == "mouseright" ||
                           lowerPart == "mouseup" || lowerPart == "mousedown" ||
                           lowerPart == "mouseupleft" || lowerPart == "mouseupright" ||
                           lowerPart == "mousedownleft" || lowerPart == "mousedownright" ||
                           // Corner directions without mouse prefix (for backward compatibility)
                           lowerPart == "up-left" || lowerPart == "upleft" ||
                           lowerPart == "up-right" || lowerPart == "upright" ||
                           lowerPart == "down-left" || lowerPart == "downleft" ||
                           lowerPart == "down-right" || lowerPart == "downright");

          if (!isMouseDir) {
              allPartsAreMouseDirections = false;
              break;
          }
      }

      isGesturePattern = allPartsAreMouseDirections;
  }

  // Only treat as gesture if it's a predefined gesture, contains mouse-specific directions in comma pattern, or is a single mouse direction
  // This avoids conflicting with regular arrow keys like "up", "down", "left", "right"
  if (isPredefinedGesture || isGesturePattern || isMouseDirection) {
    hotkey.type = HotkeyType::MouseGesture;
    hotkey.gesturePattern = parsed.keyPart; // Store the gesture pattern string

    // Determine gesture type and set up configuration based on the pattern
    if (isPredefinedGesture) {
        hotkey.gestureConfig.type = MouseGestureType::Shape;
    } else if (isMouseDirection) {
        hotkey.gestureConfig.type = MouseGestureType::Direction;
    } else {
        // It's a custom direction pattern
        hotkey.gestureConfig.type = MouseGestureType::Freeform;
    }

    hotkey.success = true;
    isGesture = true;
  }

  if (!isGesture) {
    // Check for mouse button or wheel
    int mouseButton = ParseMouseButton(parsed.keyPart);
    if (mouseButton != 0) {
      hotkey.type = (mouseButton == 1 || mouseButton == -1)
                        ? HotkeyType::MouseWheel
                        : HotkeyType::MouseButton;
      hotkey.wheelDirection =
          (mouseButton == 1 || mouseButton == -1) ? mouseButton : 0;
      hotkey.mouseButton =
          (mouseButton == 1 || mouseButton == -1) ? 0 : mouseButton;
      hotkey.key = static_cast<Key>(mouseButton);
      hotkey.success = true;
    } else {
      // Check for combo
      size_t ampPos = parsed.keyPart.find('&');
      if (ampPos != std::string::npos) {
        std::vector<std::string> parts;
        size_t start = 0;
        while (ampPos != std::string::npos) {
          parts.push_back(parsed.keyPart.substr(start, ampPos - start));
          start = ampPos + 1;
          ampPos = parsed.keyPart.find('&', start);
        }
        parts.push_back(parsed.keyPart.substr(start));

        hotkey.type = HotkeyType::Combo;
        for (const auto &part : parts) {
          auto subHotkey = AddHotkey(part, std::function<void()>{}, 0);
          hotkey.comboSequence.push_back(subHotkey);
        }
        hotkey.success = !hotkey.comboSequence.empty();
      } else {
        // Regular keyboard hotkey
        KeyCode keycode = ParseKeyPart(parsed.keyPart, parsed.isEvdev);

        if (keycode == 0) {
          std::cerr << "Invalid key: '" << parsed.keyPart
                    << "' in hotkey: " << rawInput << "\n";
          return {};
        }

        hotkey.type = HotkeyType::Keyboard;
        hotkey.key = static_cast<Key>(keycode);
        hotkey.success = true;
      }
    }
  }

  // Add to maps if has action (skip for combo subs)
  if (hasAction) {
    std::lock_guard<std::timed_mutex> lock(hotkeyMutex);
    if (id == 0)
      id = ++hotkeyCount;
    hotkey.id = id;  // Set the hotkey's id
    hotkeys[id] = hotkey;
    if (hotkey.x11)
      x11Hotkeys.insert(id);
    if (hotkey.evdev)
      evdevHotkeys.insert(id);

    // Register with EventListener if using it
    if (useNewEventListener && eventListener && hotkey.evdev) {
      eventListener->RegisterHotkey(id, hotkey);
    }
  }

  return hotkey;
}

HotKey IO::AddMouseHotkey(const std::string &hotkeyStr,
                          std::function<void()> action, int id) {
  bool hasAction = static_cast<bool>(action);

  // Use the same parser as keyboard hotkeys
  ParsedHotkey parsed = ParseHotkeyString(hotkeyStr);

  auto wrapped_action = [action, hotkeyStr, parsed]() {
    if (Configs::Get().GetVerboseKeyLogging()) {
      std::string eventTypeStr;
      switch (parsed.eventType) {
      case HotkeyEventType::Down:
        eventTypeStr = "Down";
        break;
      case HotkeyEventType::Up:
        eventTypeStr = "Up";
        break;
      case HotkeyEventType::Both:
        eventTypeStr = "Both";
        break;
      default:
        eventTypeStr = "Unknown";
      }
      info("Hotkey pressed: " + hotkeyStr +
           " | Modifiers: " + std::to_string(parsed.modifiers) +
           " | Key: " + parsed.keyPart + " | Event Type: " + eventTypeStr +
           " | Grab: " + (parsed.grab ? "true" : "false") +
           " | Suspend: " + (parsed.suspend ? "true" : "false") +
           " | Repeat: " + (parsed.repeat ? "true" : "false") +
           " | Wildcard: " + (parsed.wildcard ? "true" : "false"));
    }
    action();
  };
  // Build base hotkey
  HotKey hotkey;
  hotkey.alias = hotkeyStr;
  hotkey.modifiers = parsed.modifiers;
  hotkey.callback = wrapped_action;
  hotkey.action = "";
  hotkey.enabled = true;
  hotkey.grab = parsed.grab;
  hotkey.suspend = parsed.suspend;
  hotkey.repeat = parsed.repeat;
  hotkey.success = false;
  hotkey.evdev = parsed.isEvdev;
  hotkey.x11 = parsed.isX11;
  hotkey.eventType = parsed.eventType;
  hotkey.wildcard = parsed.wildcard;
  hotkey.repeatInterval = parsed.repeatInterval;

  // For mouse hotkeys, default to evdev unless explicitly X11
  if (!parsed.isX11 && !parsed.isEvdev) {
    hotkey.evdev = true;
    hotkey.x11 = false;
  }

  // Check for mouse button or wheel
  int button = ParseMouseButton(parsed.keyPart);
  if (button != 0) {
    bool isWheelEvent = (button == 1 || button == -1);
    hotkey.type =
        isWheelEvent ? HotkeyType::MouseWheel : HotkeyType::MouseButton;

    if (isWheelEvent) {
      // For wheel events, only set wheelDirection and leave key as 0
      hotkey.wheelDirection = button;
      hotkey.mouseButton = 0;
      hotkey.key = 0; // Explicitly set to 0 for wheel events
    } else {
      // For regular mouse buttons
      hotkey.wheelDirection = 0;
      hotkey.mouseButton = button;
      hotkey.key = static_cast<Key>(button);
    }
    hotkey.success = true;
  }

  // Check for combo
  size_t ampPos = parsed.keyPart.find('&');
  if (ampPos != std::string::npos) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (ampPos != std::string::npos) {
      parts.push_back(parsed.keyPart.substr(start, ampPos - start));
      start = ampPos + 1;
      ampPos = parsed.keyPart.find('&', start);
    }
    parts.push_back(parsed.keyPart.substr(start));

    hotkey.type = HotkeyType::Combo;
    for (const auto &part : parts) {
      auto subHotkey = AddMouseHotkey(part, std::function<void()>{}, 0);
      hotkey.comboSequence.push_back(subHotkey);
    }
    hotkey.success = !hotkey.comboSequence.empty();
  }

  // Add to maps if has action (skip for combo subs)
  if (hasAction) {
    std::lock_guard<std::timed_mutex> lock(hotkeyMutex);
    if (id == 0)
      id = ++hotkeyCount;
    hotkey.id = id;  // Set the hotkey's id
    hotkeys[id] = hotkey;
    if (hotkey.x11)
      x11Hotkeys.insert(id);
    if (hotkey.evdev)
      evdevHotkeys.insert(id);

    // Register with EventListener if using it
    if (useNewEventListener && eventListener && hotkey.evdev) {
      eventListener->RegisterHotkey(id, hotkey);
    }
  }

  return hotkey;
}
bool IO::Hotkey(const std::string &rawInput, std::function<void()> action,
                const std::string &condition, int id) {
  bool isMouseHotkey = (toLower(rawInput).find("button") != std::string::npos ||
                        toLower(rawInput).find("wheel") != std::string::npos ||
                        toLower(rawInput).find("scroll") != std::string::npos);
  HotKey hk;
  if (isMouseHotkey) {
    hk = AddMouseHotkey(rawInput, std::move(action), id);
  } else {
    hk = AddHotkey(rawInput, std::move(action), id);
  }
  if (!hk.success) {
    std::cerr << "Failed to register hotkey: " << rawInput << "\n";
    failedHotkeys.push_back(hk);
    return false;
  }
  if (!hk.evdev && hk.x11 && !globalEvdev && display) {
    bool isMouse = (hk.type == HotkeyType::MouseButton ||
                    hk.type == HotkeyType::MouseWheel);
    if (!Grab(hk.key, hk.modifiers, DefaultRootWindow(display), hk.grab,
              isMouse)) {
      failedHotkeys.push_back(hk);
    }
  }
  return true;
}
void IO::UpdateNumLockMask() {
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(display);
  for (i = 0; i < 8; i++) {
    for (j = 0; j < static_cast<unsigned int>(modmap->max_keypermod); j++) {
      if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
          XKeysymToKeycode(display, XK_Num_Lock)) {
        numlockmask = (1 << i);
      }
    }
  }
  XFreeModifiermap(modmap);
  debug("NumLock mask: 0x{:x}", numlockmask);
}
// Method to control send
void IO::ControlSend(const std::string &control, const std::string &keys) {
  std::cout << "Control send: " << control << " keys: " << keys << std::endl;
  // Use WindowManager to find the window
  wID hwnd = WindowManager::FindByTitle(control);
  if (!hwnd) {
    std::cerr << "Window not found: " << control << std::endl;
    return;
  }

  // Implementation to send keys to the window
}

// Method to get mouse position
int IO::GetMouse() {
  // No need to get root window here
  // Window root = DisplayManager::GetRootWindow();
  return 0;
}

Key IO::StringToVirtualKey(std::string keyName) {
  removeSpecialCharacters(keyName);
  keyName = ToLower(keyName);

  // Use KeyMap for lookup
#ifdef WINDOWS
  int code = KeyMap::ToWindows(keyName);
  if (code != 0) {
    return code;
  }
#else
  unsigned long code = KeyMap::ToX11(keyName);
  if (code != 0) {
    return code;
  }
#endif

  // Fallback to old logic for compatibility
  // Single character handling
  if (keyName.length() == 1) {
#ifdef WINDOWS
    return VkKeyScan(keyName[0]);
#else
    return XStringToKeysym(keyName.c_str());
#endif
  }

  // Unified lookup table with compile-time selection
  static const std::unordered_map<std::string, Key> keyMap = {
      // Common keys
      {"esc",
#ifdef WINDOWS
       VK_ESCAPE
#else
       XK_Escape
#endif
      },
      {"enter",
#ifdef WINDOWS
       VK_RETURN
#else
       XK_Return
#endif
      },
      {"space",
#ifdef WINDOWS
       VK_SPACE
#else
       XK_space
#endif
      },
      {"tab",
#ifdef WINDOWS
       VK_TAB
#else
       XK_Tab
#endif
      },
      {"backspace",
#ifdef WINDOWS
       VK_BACK
#else
       XK_BackSpace
#endif
      },
      {"delete",
#ifdef WINDOWS
       VK_DELETE
#else
       XK_Delete
#endif
      },

      // Modifiers
      {"ctrl",
#ifdef WINDOWS
       VK_CONTROL
#else
       XK_Control_L
#endif
      },
      {"lctrl",
#ifdef WINDOWS
       VK_LCONTROL
#else
       XK_Control_L
#endif
      },
      {"rctrl",
#ifdef WINDOWS
       VK_RCONTROL
#else
       XK_Control_R
#endif
      },
      {"shift",
#ifdef WINDOWS
       VK_SHIFT
#else
       XK_Shift_L
#endif
      },
      {"lshift",
#ifdef WINDOWS
       VK_LSHIFT
#else
       XK_Shift_L
#endif
      },
      {"rshift",
#ifdef WINDOWS
       VK_RSHIFT
#else
       XK_Shift_R
#endif
      },
      {"alt",
#ifdef WINDOWS
       VK_MENU
#else
       XK_Alt_L
#endif
      },
      {"lalt",
#ifdef WINDOWS
       VK_LMENU
#else
       XK_Alt_L
#endif
      },
      {"ralt",
#ifdef WINDOWS
       VK_RMENU
#else
       XK_Alt_R
#endif
      },
      {"win",
#ifdef WINDOWS
       0x5B
#else
       XK_Super_L
#endif
      },
      {"lwin",
#ifdef WINDOWS
       VK_LWIN
#else
       XK_Super_L
#endif
      },
      {"rwin",
#ifdef WINDOWS
       VK_RWIN
#else
       XK_Super_R
#endif
      },

      // Navigation
      {"home",
#ifdef WINDOWS
       VK_HOME
#else
       XK_Home
#endif
      },
      {"end",
#ifdef WINDOWS
       VK_END
#else
       XK_End
#endif
      },
      {"pgup",
#ifdef WINDOWS
       VK_PRIOR
#else
       XK_Page_Up
#endif
      },
      {"pgdn",
#ifdef WINDOWS
       VK_NEXT
#else
       XK_Page_Down
#endif
      },
      {"insert",
#ifdef WINDOWS
       VK_INSERT
#else
       XK_Insert
#endif
      },
      {"left",
#ifdef WINDOWS
       VK_LEFT
#else
       XK_Left
#endif
      },
      {"right",
#ifdef WINDOWS
       VK_RIGHT
#else
       XK_Right
#endif
      },
      {"up",
#ifdef WINDOWS
       VK_UP
#else
       XK_Up
#endif
      },
      {"down",
#ifdef WINDOWS
       VK_DOWN
#else
       XK_Down
#endif
      },

      // Lock keys
      {"capslock",
#ifdef WINDOWS
       VK_CAPITAL
#else
       XK_Caps_Lock
#endif
      },
      {"numlock",
#ifdef WINDOWS
       VK_NUMLOCK
#else
       XK_Num_Lock
#endif
      },
      {"scrolllock",
#ifdef WINDOWS
       VK_SCROLL
#else
       XK_Scroll_Lock
#endif
      },

      // Function keys
      {"f1",
#ifdef WINDOWS
       VK_F1
#else
       XK_F1
#endif
      },
      {"f2",
#ifdef WINDOWS
       VK_F2
#else
       XK_F2
#endif
      },
      {"f3",
#ifdef WINDOWS
       VK_F3
#else
       XK_F3
#endif
      },
      {"f4",
#ifdef WINDOWS
       VK_F4
#else
       XK_F4
#endif
      },
      {"f5",
#ifdef WINDOWS
       VK_F5
#else
       XK_F5
#endif
      },
      {"f6",
#ifdef WINDOWS
       VK_F6
#else
       XK_F6
#endif
      },
      {"f7",
#ifdef WINDOWS
       VK_F7
#else
       XK_F7
#endif
      },
      {"f8",
#ifdef WINDOWS
       VK_F8
#else
       XK_F8
#endif
      },
      {"f9",
#ifdef WINDOWS
       VK_F9
#else
       XK_F9
#endif
      },
      {"f10",
#ifdef WINDOWS
       VK_F10
#else
       XK_F10
#endif
      },
      {"f11",
#ifdef WINDOWS
       VK_F11
#else
       XK_F11
#endif
      },
      {
          "f12",
#ifdef WINDOWS
          VK_F21 // typo in original?
#else
          XK_F12
#endif
      },
      {"f13",
#ifdef WINDOWS
       VK_F13
#else
       XK_F13
#endif
      },
      {"f14",
#ifdef WINDOWS
       VK_F14
#else
       XK_F14
#endif
      },
      {"f15",
#ifdef WINDOWS
       VK_F15
#else
       XK_F15
#endif
      },
      {"f16",
#ifdef WINDOWS
       VK_F16
#else
       XK_F16
#endif
      },
      {"f17",
#ifdef WINDOWS
       VK_F17
#else
       XK_F17
#endif
      },
      {"f18",
#ifdef WINDOWS
       VK_F18
#else
       XK_F18
#endif
      },
      {"f19",
#ifdef WINDOWS
       VK_F19
#else
       XK_F19
#endif
      },
      {"f20",
#ifdef WINDOWS
       VK_F20
#else
       XK_F20
#endif
      },
      {"f21",
#ifdef WINDOWS
       VK_F21
#else
       XK_F21
#endif
      },
      {"f22",
#ifdef WINDOWS
       VK_F22
#else
       XK_F22
#endif
      },
      {"f23",
#ifdef WINDOWS
       VK_F23
#else
       XK_F23
#endif
      },
      {"f24",
#ifdef WINDOWS
       VK_F24
#else
       XK_F24
#endif
      },

      // Numpad
      {"numpad0",
#ifdef WINDOWS
       VK_NUMPAD0
#else
       XK_KP_0
#endif
      },
      {"numpad1",
#ifdef WINDOWS
       VK_NUMPAD1
#else
       XK_KP_1
#endif
      },
      {"numpad2",
#ifdef WINDOWS
       VK_NUMPAD2
#else
       XK_KP_2
#endif
      },
      {"numpad3",
#ifdef WINDOWS
       VK_NUMPAD3
#else
       XK_KP_3
#endif
      },
      {"numpad4",
#ifdef WINDOWS
       VK_NUMPAD4
#else
       XK_KP_4
#endif
      },
      {"numpad5",
#ifdef WINDOWS
       VK_NUMPAD5
#else
       XK_KP_5
#endif
      },
      {"numpad6",
#ifdef WINDOWS
       VK_NUMPAD6
#else
       XK_KP_6
#endif
      },
      {"numpad7",
#ifdef WINDOWS
       VK_NUMPAD7
#else
       XK_KP_7
#endif
      },
      {"numpad8",
#ifdef WINDOWS
       VK_NUMPAD8
#else
       XK_KP_8
#endif
      },
      {"numpad9",
#ifdef WINDOWS
       VK_NUMPAD9
#else
       XK_KP_9
#endif
      },
      {"numpadadd",
#ifdef WINDOWS
       VK_NUMPAD_ADD
#else
       XK_KP_Add
#endif
      },
      {"numpadsub",
#ifdef WINDOWS
       VK_NUMPAD_SUBTRACT
#else
       XK_KP_Subtract
#endif
      },
      {"numpadmul",
#ifdef WINDOWS
       VK_NUMPAD_MULTIPLY
#else
       XK_KP_Multiply
#endif
      },
      {"numpadmult",
#ifdef WINDOWS
       VK_NUMPAD_MULTIPLY
#else
       XK_KP_Multiply
#endif
      },
      {"numpaddiv",
#ifdef WINDOWS
       VK_NUMPAD_DIVIDE
#else
       XK_KP_Divide
#endif
      },
      {"numpaddec",
#ifdef WINDOWS
       VK_NUMPAD_DECIMAL
#else
       XK_KP_Decimal
#endif
      },
      {"numpaddecimal",
#ifdef WINDOWS
       VK_NUMPAD_DECIMAL
#else
       XK_KP_Decimal
#endif
      },
      {"numpaddot",
#ifdef WINDOWS
       VK_NUMPAD_DECIMAL
#else
       XK_KP_Decimal
#endif
      },
      {"numpadperiod",
#ifdef WINDOWS
       VK_NUMPAD_DECIMAL
#else
       XK_KP_Decimal
#endif
      },
      {"numpaddel",
#ifdef WINDOWS
       VK_NUMPAD_DECIMAL
#else
       XK_KP_Decimal
#endif
      },
      {"numpaddelete",
#ifdef WINDOWS
       VK_NUMPAD_DECIMAL
#else
       XK_KP_Decimal
#endif
      },
      {"numpadenter",
#ifdef WINDOWS
       VK_NUMPAD_ENTER
#else
       XK_KP_Enter
#endif
      },

  // Symbols (X11 only)
#ifndef WINDOWS
      {"minus", XK_minus},
      {"-", XK_minus},
      {"equal", XK_equal},
      {"equals", XK_equal},
      {"=", XK_equal},
      {"leftbrace", XK_bracketleft},
      {"rightbrace", XK_bracketright},
      {"semicolon", XK_semicolon},
      {";", XK_semicolon},
      {"apostrophe", XK_apostrophe},
      {"'", XK_apostrophe},
      {"grave", XK_grave},
      {"`", XK_grave},
      {"backslash", XK_backslash},
      {"\\", XK_backslash},
      {"comma", XK_comma},
      {",", XK_comma},
      {"dot", XK_period},
      {"period", XK_period},
      {".", XK_period},
      {"slash", XK_slash},
      {"/", XK_slash},
      {"less", XK_less},
      {"<", XK_less},
      {"*", XK_KP_Multiply},
#endif

      // Media keys
      {"volumeup",
#ifdef WINDOWS
       VK_VOLUME_UP
#else
       XF86XK_AudioRaiseVolume
#endif
      },
      {"volumedown",
#ifdef WINDOWS
       VK_VOLUME_DOWN
#else
       XF86XK_AudioLowerVolume
#endif
      },
      {
          "volumemute",
#ifdef WINDOWS
          0 // No direct VK constant
#else
          XF86XK_AudioMute
#endif
      },
      {
          "mediaplay",
#ifdef WINDOWS
          0 // No direct VK constant
#else
          XF86XK_AudioPlay
#endif
      },
      {
          "medianext",
#ifdef WINDOWS
          0 // No direct VK constant
#else
          XF86XK_AudioNext
#endif
      },
      {
          "mediaprev",
#ifdef WINDOWS
          0 // No direct VK constant
#else
          XF86XK_AudioPrev
#endif
      },

      // Special keys
      {"printscreen",
#ifdef WINDOWS
       VK_SNAPSHOT
#else
       XK_Print
#endif
      },
      {"pause",
#ifdef WINDOWS
       VK_PAUSE
#else
       XK_Pause
#endif
      },
      {"pausebreak",
#ifdef WINDOWS
       VK_PAUSE
#else
       XK_Pause
#endif
      },
      {"menu",
#ifdef WINDOWS
       VK_APPS
#else
       XK_Menu
#endif
      },
      {"apps",
#ifdef WINDOWS
       VK_APPS
#else
       XK_Menu
#endif
      },

  // Mouse buttons (Windows only)
#ifdef WINDOWS
      {"lbutton", VK_LBUTTON},
      {"rbutton", VK_RBUTTON},
#endif

      // Alphabet handled by single-char logic above
      // Numbers handled by single-char logic above

      // Special marker
      {"nosymbol",
#ifdef WINDOWS
       0
#else
       XK_VoidSymbol
#endif
      },
  };

  auto it = keyMap.find(keyName);
  if (it != keyMap.end()) {
    return it->second;
  }

#ifndef WINDOWS
  // Fallback for X11: try button lookup
  return IO::StringToButton(keyName);
#else
  return 0;
#endif
}

Key IO::StringToButton(const std::string &buttonNameRaw) {
  std::string buttonName = ToLower(buttonNameRaw);

  static const std::unordered_map<std::string, Key> buttonMap = {
      {"button1", Button1},   {"button2", Button2},
      {"button3", Button3},   {"button4", Button4},
      {"wheelup", Button4},   {"scrollup", Button4},
      {"button5", Button5},   {"wheeldown", Button5},
      {"scrolldown", Button5}
      // button6+ handled dynamically below
  };

  auto it = buttonMap.find(buttonName);
  if (it != buttonMap.end())
    return it->second;

  // Check for "buttonN" where N >= 6
  if (buttonName.rfind("button", 0) == 0) {
    int btnNum = std::atoi(buttonName.c_str() + 6);
    if (btnNum >= 6 && btnNum <= 32) // sane upper bound
      return static_cast<Key>(btnNum);
  }

  return 0; // Invalid / unrecognized
}
Key IO::EvdevNameToKeyCode(std::string keyName) {
  removeSpecialCharacters(keyName);
  keyName = ToLower(keyName);

  // Use KeyMap for lookup
  int code = KeyMap::FromString(keyName);
  if (code != 0) {
    return code;
  }

  // Fallback to old logic for compatibility
  // Single character handling for letters/numbers
  if (keyName.length() == 1) {
    char c = keyName[0];
    if (c >= 'a' && c <= 'z')
      return KEY_A + (c - 'a');
    if (c >= '0' && c <= '9')
      return (c == '0') ? KEY_0 : KEY_1 + (c - '1');
  }

  static const std::unordered_map<std::string, Key> evdevMap = {
      // Control keys
      {"esc", KEY_ESC},
      {"enter", KEY_ENTER},
      {"space", KEY_SPACE},
      {"tab", KEY_TAB},
      {"backspace", KEY_BACKSPACE},
      {"delete", KEY_DELETE},

      // Modifiers
      {"ctrl", KEY_LEFTCTRL},
      {"lctrl", KEY_LEFTCTRL},
      {"rctrl", KEY_RIGHTCTRL},
      {"shift", KEY_LEFTSHIFT},
      {"lshift", KEY_LEFTSHIFT},
      {"rshift", KEY_RIGHTSHIFT},
      {"alt", KEY_LEFTALT},
      {"lalt", KEY_LEFTALT},
      {"ralt", KEY_RIGHTALT},
      {"win", KEY_LEFTMETA},
      {"meta", KEY_LEFTMETA},
      {"lwin", KEY_LEFTMETA},
      {"lmeta", KEY_LEFTMETA},
      {"rwin", KEY_RIGHTMETA},
      {"rmeta", KEY_RIGHTMETA},

      // Navigation
      {"home", KEY_HOME},
      {"end", KEY_END},
      {"pgup", KEY_PAGEUP},
      {"pgdn", KEY_PAGEDOWN},
      {"pageup", KEY_PAGEUP},
      {"pagedown", KEY_PAGEDOWN},
      {"insert", KEY_INSERT},
      {"left", KEY_LEFT},
      {"right", KEY_RIGHT},
      {"up", KEY_UP},
      {"down", KEY_DOWN},

      // Lock keys
      {"capslock", KEY_CAPSLOCK},
      {"numlock", KEY_NUMLOCK},
      {"scrolllock", KEY_SCROLLLOCK},

      // Function keys
      {"f1", KEY_F1},
      {"f2", KEY_F2},
      {"f3", KEY_F3},
      {"f4", KEY_F4},
      {"f5", KEY_F5},
      {"f6", KEY_F6},
      {"f7", KEY_F7},
      {"f8", KEY_F8},
      {"f9", KEY_F9},
      {"f10", KEY_F10},
      {"f11", KEY_F11},
      {"f12", KEY_F12},
      {"f13", KEY_F13},
      {"f14", KEY_F14},
      {"f15", KEY_F15},
      {"f16", KEY_F16},
      {"f17", KEY_F17},
      {"f18", KEY_F18},
      {"f19", KEY_F19},
      {"f20", KEY_F20},
      {"f21", KEY_F21},
      {"f22", KEY_F22},
      {"f23", KEY_F23},
      {"f24", KEY_F24},

      // Numpad
      {"numpad0", KEY_KP0},
      {"numpad1", KEY_KP1},
      {"numpad2", KEY_KP2},
      {"numpad3", KEY_KP3},
      {"numpad4", KEY_KP4},
      {"numpad5", KEY_KP5},
      {"numpad6", KEY_KP6},
      {"numpad7", KEY_KP7},
      {"numpad8", KEY_KP8},
      {"numpad9", KEY_KP9},
      {"numpadadd", KEY_KPPLUS},
      {"numpadplus", KEY_KPPLUS},
      {"numpadsub", KEY_KPMINUS},
      {"numpadminus", KEY_KPMINUS},
      {"numpadmul", KEY_KPASTERISK},
      {"numpadmult", KEY_KPASTERISK},
      {"numpadasterisk", KEY_KPASTERISK},
      {"*", KEY_KPASTERISK},
      {"numpaddiv", KEY_KPSLASH},
      {"numpaddec", KEY_KPDOT},
      {"numpaddot", KEY_KPDOT},
      {"numpaddel", KEY_KPDOT},
      {"numpadperiod", KEY_KPDOT},
      {"numpaddelete", KEY_KPDOT},
      {"numpaddecimal", KEY_KPDOT},
      {"numpadenter", KEY_KPENTER},
      {"numpadequal", KEY_KPEQUAL},
      {"numpadcomma", KEY_KPCOMMA},
      {"numpadleftparen", KEY_KPLEFTPAREN},
      {"numpadrightparen", KEY_KPRIGHTPAREN},

      // Symbols
      {"minus", KEY_MINUS},
      {"-", KEY_MINUS},
      {"equal", KEY_EQUAL},
      {"equals", KEY_EQUAL},
      {"=", KEY_EQUAL},
      {"leftbrace", KEY_LEFTBRACE},
      {"[", KEY_LEFTBRACE},
      {"rightbrace", KEY_RIGHTBRACE},
      {"]", KEY_RIGHTBRACE},
      {"semicolon", KEY_SEMICOLON},
      {";", KEY_SEMICOLON},
      {"apostrophe", KEY_APOSTROPHE},
      {"'", KEY_APOSTROPHE},
      {"grave", KEY_GRAVE},
      {"`", KEY_GRAVE},
      {"backslash", KEY_BACKSLASH},
      {"\\", KEY_BACKSLASH},
      {"comma", KEY_COMMA},
      {",", KEY_COMMA},
      {"dot", KEY_DOT},
      {"period", KEY_DOT},
      {".", KEY_DOT},
      {"slash", KEY_SLASH},
      {"/", KEY_SLASH},
      {"less", KEY_102ND},
      {"<", KEY_102ND},

      // Media control keys
      {"playpause", KEY_PLAYPAUSE},
      {"play", KEY_PLAY},
      {"pause", KEY_PAUSE},
      {"stop", KEY_STOP},
      {"stopcd", KEY_STOPCD},
      {"record", KEY_RECORD},
      {"rewind", KEY_REWIND},
      {"fastforward", KEY_FASTFORWARD},
      {"ejectcd", KEY_EJECTCD},
      {"eject", KEY_EJECTCD},
      {"nextsong", KEY_NEXTSONG},
      {"previoussong", KEY_PREVIOUSSONG},
      {"next", KEY_NEXTSONG},
      {"prev", KEY_PREVIOUSSONG},
      {"previous", KEY_PREVIOUSSONG},

      // Volume control
      {"volumeup", KEY_VOLUMEUP},
      {"volumedown", KEY_VOLUMEDOWN},
      {"mute", KEY_MUTE},
      {"volumemute", KEY_MUTE},
      {"micmute", KEY_MICMUTE},

      // Browser keys
      {"homepage", KEY_HOMEPAGE},
      {"back", KEY_BACK},
      {"forward", KEY_FORWARD},
      {"search", KEY_SEARCH},
      {"bookmarks", KEY_BOOKMARKS},
      {"refresh", KEY_REFRESH},
      {"stop", KEY_STOP},
      {"favorites", KEY_FAVORITES},

      // Application launcher keys
      {"mail", KEY_MAIL},
      {"calc", KEY_CALC},
      {"calculator", KEY_CALC},
      {"computer", KEY_COMPUTER},
      {"media", KEY_MEDIA},
      {"www", KEY_WWW},
      {"finance", KEY_FINANCE},
      {"shop", KEY_SHOP},
      {"coffee", KEY_COFFEE},
      {"chat", KEY_CHAT},
      {"messenger", KEY_MESSENGER},
      {"calendar", KEY_CALENDAR},

      // Media player control
      {"mediaplay", KEY_PLAYPAUSE},
      {"medianext", KEY_NEXTSONG},
      {"mediaprev", KEY_PREVIOUSSONG},
      {"mediastop", KEY_STOPCD},
      {"mediarecord", KEY_RECORD},
      {"mediarewind", KEY_REWIND},
      {"mediaforward", KEY_FASTFORWARD},
      {"mediaeject", KEY_EJECTCD},

      // Power management
      {"power", KEY_POWER},
      {"sleep", KEY_SLEEP},
      {"wakeup", KEY_WAKEUP},
      {"suspend", KEY_SUSPEND},
      // Display/brightness
      {"brightnessup", KEY_BRIGHTNESSUP},
      {"brightnessdown", KEY_BRIGHTNESSDOWN},
      {"brightness", KEY_BRIGHTNESS_AUTO},
      {"brightnessauto", KEY_BRIGHTNESS_AUTO},
      {"displayoff", KEY_DISPLAY_OFF},
      {"switchvideomode", KEY_SWITCHVIDEOMODE},

      // Keyboard backlight
      {"kbdillumup", KEY_KBDILLUMUP},
      {"kbdillumdown", KEY_KBDILLUMDOWN},
      {"kbdillumtoggle", KEY_KBDILLUMTOGGLE},

      // Wireless
      {"wlan", KEY_WLAN},
      {"bluetooth", KEY_BLUETOOTH},
      {"wifi", KEY_WLAN},
      {"rfkill", KEY_RFKILL},

      // Battery
      {"battery", KEY_BATTERY},

      // Zoom
      {"zoomin", KEY_ZOOMIN},
      {"zoomout", KEY_ZOOMOUT},
      {"zoomreset", KEY_ZOOMRESET},

      // Screen control
      {"cyclewindows", KEY_CYCLEWINDOWS},
      {"scale", KEY_SCALE},
      {"dashboard", KEY_DASHBOARD},

      // File operations
      {"file", KEY_FILE},
      {"open", KEY_OPEN},
      {"close", KEY_CLOSE},
      {"save", KEY_SAVE},
      {"print", KEY_PRINT},
      {"cut", KEY_CUT},
      {"copy", KEY_COPY},
      {"paste", KEY_PASTE},
      {"find", KEY_FIND},
      {"undo", KEY_UNDO},
      {"redo", KEY_REDO},

      // Text editing
      {"again", KEY_AGAIN},
      {"props", KEY_PROPS},
      {"front", KEY_FRONT},
      {"help", KEY_HELP},
      {"menu", KEY_MENU},
      {"select", KEY_SELECT},
      {"cancel", KEY_CANCEL},

      // ISO keyboard extras
      {"iso", KEY_102ND},
      {"102nd", KEY_102ND},
      {"ro", KEY_RO},
      {"katakanahiragana", KEY_KATAKANAHIRAGANA},
      {"yen", KEY_YEN},
      {"henkan", KEY_HENKAN},
      {"muhenkan", KEY_MUHENKAN},
      {"kpjpcomma", KEY_KPJPCOMMA},
      {"hangeul", KEY_HANGEUL},
      {"hanja", KEY_HANJA},
      {"katakana", KEY_KATAKANA},
      {"hiragana", KEY_HIRAGANA},
      {"zenkakuhankaku", KEY_ZENKAKUHANKAKU},

      // Special system keys
      {"sysrq", KEY_SYSRQ},
      {"printscreen", KEY_SYSRQ},
      {"pausebreak", KEY_PAUSE},
      {"scrollup", KEY_SCROLLUP},
      {"scrolldown", KEY_SCROLLDOWN},

      // Gaming/multimedia extras
      {"prog1", KEY_PROG1},
      {"prog2", KEY_PROG2},
      {"prog3", KEY_PROG3},
      {"prog4", KEY_PROG4},
      {"macro", KEY_MACRO},
      {"fn", KEY_FN},
      {"fnesc", KEY_FN_ESC},
      {"fnf1", KEY_FN_F1},
      {"fnf2", KEY_FN_F2},
      {"fnf3", KEY_FN_F3},
      {"fnf4", KEY_FN_F4},
      {"fnf5", KEY_FN_F5},
      {"fnf6", KEY_FN_F6},
      {"fnf7", KEY_FN_F7},
      {"fnf8", KEY_FN_F8},
      {"fnf9", KEY_FN_F9},
      {"fnf10", KEY_FN_F10},
      {"fnf11", KEY_FN_F11},
      {"fnf12", KEY_FN_F12},

      // Special marker
      {"nosymbol", KEY_RO},
      {"reserved", KEY_RESERVED},
      {"unknown", KEY_UNKNOWN}};

  auto it = evdevMap.find(keyName);
  return (it != evdevMap.end()) ? it->second : 0;
}

// Display a message box
void IO::MsgBox(const std::string &message) {
  // Stub implementation for message box
  std::cout << "Message Box: " << message << std::endl;
}

// Assign a hotkey to a specific ID
void IO::AssignHotkey(HotKey hotkey, int id) {
  // Generate a unique ID if not provided
  if (id == 0) {
    id = ++hotkeyCount;
  }

  // Set the hotkey's id
  hotkey.id = id;

  // Register the hotkey
  hotkeys[id] = hotkey;

  // Platform-specific registration
#ifdef __linux__
  Display *display = DisplayManager::GetDisplay();
  if (!display)
    return;

  Window root = DefaultRootWindow(display);

  KeyCode keycode = XKeysymToKeycode(display, hotkey.key);
  if (keycode == 0) {
    std::cerr << "Invalid key code for hotkey: " << hotkey.alias << std::endl;
    return;
  }

  // Ungrab first in case it's already grabbed
  XUngrabKey(display, keycode, hotkey.modifiers, root);

  // Grab the key
  if (XGrabKey(display, keycode, hotkey.modifiers, root, x11::XFalse,
               GrabModeAsync, GrabModeAsync) != x11::XSuccess) {
    std::cerr << "Failed to grab key: " << hotkey.alias << std::endl;
  }

  XFlush(display);
#endif
}

#ifdef WINDOWS
LRESULT CALLBACK IO::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
    KBDLLHOOKSTRUCT *pKeyboard = (KBDLLHOOKSTRUCT *)lParam;

    // Detect the state of modifier keys
    bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool winPressed = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                      (GetKeyState(VK_RWIN) & 0x8000) != 0;
    for (const auto &[id, hotkey] : hotkeys) {
      if (!hotkey.grab && hotkey.enabled) {
        // Check if the virtual key matches and modifiers are valid
        if (pKeyboard->vkCode == static_cast<DWORD>(hotkey.key.virtualKey)) {
          // Check if the required modifiers are pressed
          bool modifiersMatch =
              ((hotkey.modifiers & MOD_SHIFT) ? shiftPressed : true) &&
              ((hotkey.modifiers & MOD_CONTROL) ? ctrlPressed : true) &&
              ((hotkey.modifiers & MOD_ALT) ? altPressed : true) &&
              ((hotkey.modifiers & MOD_WIN) ? winPressed
                                            : true); // Check Windows key

          if (modifiersMatch) {
            if (hotkey.callback) {
              std::cout << "Action from non-blocking callback for "
                        << hotkey.alias << "\n";
              hotkey.callback(); // Call the associated action
            }
            break; // Exit after the first match
          }
        }
      }
    }
  }
  return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}
#endif

int IO::GetKeyboard() {
  if (!display) {
    display = XOpenDisplay(nullptr);
    if (!display) {
      std::cerr << "Unable to open X display!" << std::endl;
      return EXIT_FAILURE;
    }
  }

  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      1, 1, 0, 0, 0);
  if (XGrabKeyboard(display, window, x11::XTrue, GrabModeAsync, GrabModeAsync,
                    CurrentTime) != GrabSuccess) {
    std::cerr << "Unable to grab keyboard!" << std::endl;
    XDestroyWindow(display, window);
    return EXIT_FAILURE;
  }

  XEvent event;
  while (true) {
    XNextEvent(display, &event);
    if (event.type == x11::XKeyPress) {
      std::cout << "Key pressed: " << event.xkey.keycode << std::endl;
    }
  }

  XUngrabKeyboard(display, CurrentTime);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}

int IO::ParseModifiers(str str) {
  int modifiers = 0;
#ifdef WINDOWS
  if (str.find("+") != str::npos) {
    modifiers |= MOD_SHIFT;
    str.erase(str.find("+"), 1);
  }
  if (str.find("^") != str::npos) {
    modifiers |= MOD_CONTROL;
    str.erase(str.find("^"), 1);
  }
  if (str.find("!") != str::npos) {
    modifiers |= MOD_ALT;
    str.erase(str.find("!"), 1);
  }
  if (str.find("#") != str::npos) {
    modifiers |= MOD_WIN;
    str.erase(str.find("#"), 1);
  }
#else
  if (str.find("+") != std::string::npos) {
    modifiers |= ShiftMask;
    str.erase(str.find("+"), 1);
  }
  if (str.find("^") != std::string::npos) {
    modifiers |= ControlMask;
    str.erase(str.find("^"), 1);
  }
  if (str.find("!") != std::string::npos) {
    modifiers |= Mod1Mask;
    str.erase(str.find("!"), 1);
  }
  if (str.find("#") != std::string::npos) {
    modifiers |= Mod4Mask; // For Meta/Windows
    str.erase(str.find("#"), 1);
  }
#endif
  return modifiers;
}
bool IO::GetKeyState(const std::string &keyName) {
  // Use EventListener if enabled
  if (useNewEventListener && eventListener) {
    int keycode = KeyMap::FromString(keyName);
    if (keycode != 0) {
      return eventListener->GetKeyState(keycode);
    }
  }

  // Try evdev first if available
  if (evdevRunning && !evdevKeyState.empty()) {
    int keycode = StringToVirtualKey(keyName);
    if (keycode != -1) {
      return evdevKeyState[keycode];
    }
  }

// Fallback to X11 if evdev not available
#ifdef __linux__
  if (display) {
    Key keycode = GetKeyCode(keyName);
    return GetKeyState(keycode);
  }
#endif

  return false;
}

bool IO::GetKeyState(int keycode) {
  // Use EventListener if enabled
  if (useNewEventListener && eventListener) {
    return eventListener->GetKeyState(keycode);
  }

  // Direct keycode lookup - faster for known codes
  if (evdevRunning) {
    auto it = evdevKeyState.find(keycode);
    return (it != evdevKeyState.end()) ? it->second : false;
  }

#ifdef __linux__
  if (display) {
    char keymap[32];
    XQueryKeymap(display, keymap);

    return (keymap[keycode / 8] & (1 << (keycode % 8))) != 0;
  }
#endif

  return false;
}
bool IO::IsAnyKeyPressed() {
  std::lock_guard<std::mutex> lk(keyStateMutex);
  if (evdevRunning && !eventListener->evdevKeyState.empty()) {
    for (const auto &[keycode, isDown] : eventListener->evdevKeyState) {
      if (isDown)
        return true;
    }
    return false;
  }

#ifdef __linux__
  if (display) {
    char keymap[32];
    XQueryKeymap(display, keymap);

    // Check if any bit is set in the entire keymap
    for (int i = 0; i < 32; i++) {
      if (keymap[i] != 0)
        return true;
    }
  }
#endif

  return false;
}

bool IO::IsAnyKeyPressedExcept(const std::string &excludeKey) {
  int excludeKeycode = EvdevNameToKeyCode(excludeKey);
  std::lock_guard<std::mutex> lk(keyStateMutex);
  if (evdevRunning && !eventListener->evdevKeyState.empty()) {
    for (const auto &[keycode, isDown] : eventListener->evdevKeyState) {
      if (isDown && keycode != excludeKeycode)
        return true;
    }
    return false;
  }

#ifdef __linux__
  if (display) {
    excludeKeycode = StringToVirtualKey(excludeKey);
    char keymap[32];
    XQueryKeymap(display, keymap);

    for (int keycode = 8; keycode < 256; keycode++) {
      if (keycode != excludeKeycode &&
          (keymap[keycode / 8] & (1 << (keycode % 8)))) {
        return true;
      }
    }
  }
#endif

  return false;
}

// Bonus: exclude multiple keys
bool IO::IsAnyKeyPressedExcept(const std::vector<std::string> &excludeKeys) {
  std::set<int> excludeCodes;
  for (const auto &key : excludeKeys) {
    int code = EvdevNameToKeyCode(key);
    if (code != -1)
      excludeCodes.insert(code);
  }
  std::lock_guard<std::mutex> lk(keyStateMutex);
  if (evdevRunning && !eventListener->evdevKeyState.empty()) {
    for (const auto &[keycode, isDown] : eventListener->evdevKeyState) {
      if (isDown && excludeCodes.find(keycode) == excludeCodes.end()) {
        return true;
      }
    }
    return false;
  }

  // X11 fallback
  if (display) {
    char keymap[32];
    XQueryKeymap(display, keymap);

    for (int keycode = 8; keycode < 256; keycode++) {
      if (excludeCodes.find(keycode) == excludeCodes.end() &&
          (keymap[keycode / 8] & (1 << (keycode % 8)))) {
        return true;
      }
    }
  }

  return false;
}
bool IO::IsShiftPressed() {
  if (eventListener) {
    auto modState = eventListener->GetModifierState();
    return modState.leftShift || modState.rightShift;
  }
  return currentModifierState.leftShift || currentModifierState.rightShift;
}

bool IO::IsCtrlPressed() {
  if (eventListener) {
    auto modState = eventListener->GetModifierState();
    return modState.leftCtrl || modState.rightCtrl;
  }
  return currentModifierState.leftCtrl || currentModifierState.rightCtrl;
}

bool IO::IsAltPressed() {
  if (eventListener) {
    auto modState = eventListener->GetModifierState();
    return modState.leftAlt || modState.rightAlt;
  }
  return currentModifierState.leftAlt || currentModifierState.rightAlt;
}

bool IO::IsWinPressed() {
  if (eventListener) {
    auto modState = eventListener->GetModifierState();
    return modState.leftMeta || modState.rightMeta;
  }
  return currentModifierState.leftMeta || currentModifierState.rightMeta;
}

Key IO::GetKeyCode(cstr keyName) {
  // Convert string to keysym
  KeySym keysym = StringToVirtualKey(keyName);
  if (keysym == NoSymbol) {
    std::cerr << "Unknown keysym for: " << keyName << "\n";
    return 0;
  }

  // Convert keysym to keycode
  KeyCode keycode = XKeysymToKeycode(DisplayManager::GetDisplay(), keysym);
  if (keycode == 0) {
    std::cerr << "Invalid keycode for keysym: " << keyName << "\n";
    return 0;
  }
  return keycode;
}
void IO::PressKey(const std::string &keyName, bool press) {
  std::cout << "Pressing key: " << keyName << " (press: " << press << ")"
            << std::endl;

#ifdef __linux__
  Display *display = havel::DisplayManager::GetDisplay();
  if (!display) {
    std::cerr << "No X11 display available for key press\n";
    return;
  }
  Key keycode = GetKeyCode(keyName);

  // Send fake key event
  XTestFakeKeyEvent(display, keycode, press ? x11::XTrue : x11::XFalse,
                    CurrentTime);
  XFlush(display);
#endif
}

bool IO::GrabHotkey(int hotkeyId) {
#ifdef __linux__
  if (!display)
    return false;

  auto it = hotkeys.find(hotkeyId);
  if (it == hotkeys.end()) {
    std::cerr << "Hotkey ID not found: " << hotkeyId << std::endl;
    return false;
  }

  const HotKey &hotkey = it->second;
  Window root = DefaultRootWindow(display);
  KeyCode keycode = hotkey.key;

  if (keycode == 0) {
    std::cerr << "Invalid keycode for hotkey: " << hotkey.alias << std::endl;
    return false;
  }

  // Use our improved method to grab with all modifier variants
  if (!hotkey.evdev) {
    Grab(keycode, hotkey.modifiers, root, hotkey.grab);
  }
  hotkeys[hotkeyId].enabled = true;

  std::cout << "Successfully grabbed hotkey: " << hotkey.alias << std::endl;
  return true;
#else
  return false;
#endif
}
bool IO::UngrabHotkey(int hotkeyId) {
#ifdef __linux__
  if (!display)
    return false;

  auto it = hotkeys.find(hotkeyId);
  if (it == hotkeys.end()) {
    error("Hotkey ID not found: {}", hotkeyId);
    return false;
  }

  const HotKey &hotkey = it->second;
  info("Ungrabbing hotkey: {}", hotkey.alias);

  if (hotkey.key == 0) {
    error("Invalid keycode for hotkey: {}", hotkey.alias);
    return false;
  }

  // Only ungrab X11 hotkeys (evdev hotkeys don't need ungrabbing)
  if (!hotkey.evdev && display) {
    Window root = DefaultRootWindow(display);

    // Check if any OTHER hotkeys are using the same key+modifiers
    bool hasOtherSameHotkey = false;
    for (const auto &[id, hk] : hotkeys) {
      if (id != hotkeyId && hk.enabled && !hk.evdev && hk.key == hotkey.key &&
          hk.modifiers == hotkey.modifiers) {
        hasOtherSameHotkey = true;
        break;
      }
    }

    // Only ungrab if no other enabled hotkeys use this key+modifier combo
    if (!hasOtherSameHotkey) {
      Ungrab(hotkey.key, hotkey.modifiers, root, false);
      info("Physically ungrabbed key {} with modifiers 0x{:x}", hotkey.key,
           hotkey.modifiers);
    } else {
      info("Not ungrabbing - other hotkeys still using key {} with modifiers "
           "0x{:x}",
           hotkey.key, hotkey.modifiers);
    }
  }

  // Always disable the hotkey entry
  hotkeys[hotkeyId].enabled = false;

  info("Successfully ungrabbed hotkey: {}", hotkey.alias);
  return true;
#else
  return false;
#endif
}

void IO::SetAnyKeyPressCallback(AnyKeyPressCallback callback) {
  if (eventListener) {
    eventListener->SetAnyKeyPressCallback(callback);
  }
}

bool IO::GrabHotkeysByPrefix(const std::string &prefix) {
#ifdef __linux__
  if (!display)
    return false;

  bool success = true;
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias.find(prefix) == 0) {
      if (!GrabHotkey(id)) {
        success = false;
      }
    }
  }
  return success;
#else
  return false;
#endif
}

bool IO::UngrabHotkeysByPrefix(const std::string &prefix) {
#ifdef __linux__
  if (!display)
    return false;

  bool success = true;
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias.find(prefix) == 0) {
      if (!UngrabHotkey(id)) {
        success = false;
      }
    }
  }
  return success;
#else
  return false;
#endif
}
bool IO::EnableHotkey(const std::string &keyName) {
  std::lock_guard<std::mutex> lock(hotkeySetMutex);
  bool found = false;

  // Find the hotkey by name and enable it
  for (auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias == keyName) {
      hotkey.enabled = true;
      found = true;

      // If the hotkey should be grabbed, re-grab it
      if (hotkey.grab && !hotkey.evdev) {
        GrabHotkey(id);
      }
    }
  }

  return found;
}

bool IO::DisableHotkey(const std::string &keyName) {
  std::lock_guard<std::mutex> lock(hotkeySetMutex);
  bool found = false;

  // Find the hotkey by name and disable it
  for (auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias == keyName) {
      hotkey.enabled = false;
      found = true;

      // If the hotkey was grabbed, ungrab it
      if (hotkey.grab && !hotkey.evdev) {
        UngrabHotkey(id);
      }
    }
  }

  return found;
}

bool IO::ToggleHotkey(const std::string &keyName) {
  std::lock_guard<std::mutex> lock(hotkeySetMutex);
  bool found = false;

  // Find the hotkey by name and toggle its state
  for (auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias == keyName) {
      hotkey.enabled = !hotkey.enabled;
      found = true;

      if (hotkey.enabled && hotkey.grab && !hotkey.evdev) {
        GrabHotkey(id);
      } else if (!hotkey.enabled && hotkey.grab && !hotkey.evdev) {
        UngrabHotkey(id);
      }
    }
  }

  return found;
}
void IO::Map(const std::string &from, const std::string &to) {
  // X11 mapping for backward compatibility
  KeySym fromKey = StringToVirtualKey(from);
  KeySym toKey = StringToVirtualKey(to);
  if (fromKey != NoSymbol && toKey != NoSymbol) {
    keyMapInternal[fromKey] = toKey;
  }

  // Evdev mapping
  int fromCode = EvdevNameToKeyCode(from);
  int toCode = EvdevNameToKeyCode(to);
  if (fromCode > 0 && toCode > 0) {
    evdevKeyMap[fromCode] = toCode;
    if (eventListener) {
      eventListener->AddKeyRemap(fromCode, toCode);
    }
    debug("Mapped evdev key {} ({}) to {} ({})", from, fromCode, to, toCode);
  } else {
    warn("Failed to map keys: {} -> {} (from:{} to:{})", from, to, fromCode,
         toCode);
  }
}

void IO::Remap(const std::string &key1, const std::string &key2) {
  // X11 remapping for backward compatibility
  KeySym k1 = StringToVirtualKey(key1);
  KeySym k2 = StringToVirtualKey(key2);
  if (k1 != NoSymbol && k2 != NoSymbol) {
    remappedKeys[k1] = k2;
    remappedKeys[k2] = k1;
  }

  // Evdev remapping
  int code1 = EvdevNameToKeyCode(key1);
  int code2 = EvdevNameToKeyCode(key2);
  if (code1 > 0 && code2 > 0) {
    evdevRemappedKeys[code1] = code2;
    evdevRemappedKeys[code2] = code1;
    debug("Remapped evdev keys: {} ({}) <-> {} ({})", key1, code1, key2, code2);

    // Also add to EventListener if enabled
    if (useNewEventListener && eventListener) {
      eventListener->AddKeyRemap(code1, code2);
      eventListener->AddKeyRemap(code2, code1);
    }
  } else {
    warn("Failed to remap keys: {} <-> {} ({} <-> {})", key1, key2, code1,
         code2);
  }
}
bool IO::MatchEvdevModifiers(int expectedModifiers,
                             const std::map<int, bool> &keyState) {
  auto isPressed = [&](int key) -> bool {
    auto it = keyState.find(key);
    return it != keyState.end() && it->second;
  };

  // Check each modifier type
  std::vector<std::pair<int, std::pair<int, int>>> modifiers = {
      {KEY_LEFTCTRL, {KEY_LEFTCTRL, KEY_RIGHTCTRL}},
      {KEY_LEFTSHIFT, {KEY_LEFTSHIFT, KEY_RIGHTSHIFT}},
      {KEY_LEFTALT, {KEY_LEFTALT, KEY_RIGHTALT}},
      {KEY_LEFTMETA, {KEY_LEFTMETA, KEY_RIGHTMETA}}};

  for (auto &[flag, keys] : modifiers) {
    bool expected = (expectedModifiers & flag) != 0;
    bool pressed = isPressed(keys.first) || isPressed(keys.second);

    if (expected != pressed)
      return false;
  }

  return true;
}
bool IO::IsKeyRemappedTo(int targetKey) {
  for (const auto &[original, mapped] : activeRemaps) {
    if (mapped == targetKey)
      return true;
  }
  return false;
}

bool IO::StartEvdevHotkeyListener(const std::string &devicePath) {
  if (evdevRunning)
    return false;
  evdevDevicePath = devicePath;
  // Create eventfd for clean shutdown signaling
  evdevShutdownFd = eventfd(0, EFD_NONBLOCK);
  if (evdevShutdownFd < 0) {
    error("Failed to create shutdown eventfd: {}", strerror(errno));
    return false;
  }

  evdevRunning = true;
  SendUInput(KEY_RESERVED, false); // no-op

  evdevThread = std::thread([this]() {
    int fd = open(evdevDevicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      error("evdev: cannot open " + evdevDevicePath + ": " +
            std::string(strerror(errno)) + "\n");
      evdevRunning = false;
      return;
    }
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
      error("evdev: failed to grab device exclusively: {}", strerror(errno));
      // Continue without exclusive grab - still works for monitoring
    } else {
      info("evdev: grabbed device exclusively");
    }
    if (!SetupUinputDevice()) {
      close(fd);
      evdevRunning = false;
      error("evdev: failed to setup uinput device\n");
      return;
    }
    auto parsedEmergencyHotkey = ParseHotkeyString(emergencyHotkey);
    auto emergencyKey = ParseKeyPart(parsedEmergencyHotkey.keyPart, true);

    struct input_event evs[64];
    std::map<int, bool> modState;

    while (evdevRunning) {
      // Use select() to monitor both evdev fd and shutdown fd
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      FD_SET(evdevShutdownFd, &readfds);

      int maxfd = std::max(fd, evdevShutdownFd);
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 10000; // 10ms timeout

      int ret = select(maxfd + 1, &readfds, nullptr, nullptr, &tv);

      if (ret < 0) {
        if (errno != EINTR) {
          error("select() error: {}", strerror(errno));
        }
        continue;
      }

      // Check if shutdown was signaled
      if (FD_ISSET(evdevShutdownFd, &readfds)) {
        info("Shutdown signal received, stopping evdev listener");
        break;
      }

      // No events ready
      if (!FD_ISSET(fd, &readfds)) {
        continue;
      }

      // Read multiple events at once
      ssize_t n = read(fd, evs, sizeof(evs));
      if (n < (ssize_t)sizeof(struct input_event)) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          error("Error reading from evdev: {}", strerror(errno));
        }
        continue;
      }

      int num_events = n / sizeof(struct input_event);

      // Sanity check: prevent processing absurdly large number of events
      if (num_events > 64 || num_events < 0) {
        error("Invalid number of events: {} - possible memory corruption",
              num_events);
        continue;
      }

      auto batch_start = std::chrono::steady_clock::now();
      constexpr int MAX_BATCH_PROCESSING_MS =
          100; // Max time to process a batch

      for (int i = 0; i < num_events; i++) {
        // Check if we've been processing this batch for too long
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - batch_start)
                           .count();

        if (elapsed > MAX_BATCH_PROCESSING_MS) {
          error("Batch processing timeout after {}ms at event {}/{} - breaking "
                "to prevent hang",
                elapsed, i, num_events);
          break;
        }

        auto &ev = evs[i];

        if (ev.type != EV_KEY)
          continue;

        bool repeat = (ev.value == 2);
        bool down = (ev.value == 1 || repeat);
        int originalCode = ev.code;

        keyDownState[originalCode] = down;
        int mappedCode = originalCode;

        {
          std::lock_guard<std::mutex> lk(remapMutex);
          if (down) {
            // On press: determine mapping and store it
            auto remapIt = evdevRemappedKeys.find(originalCode);
            if (remapIt != evdevRemappedKeys.end()) {
              mappedCode = remapIt->second;
            } else {
              auto mapIt = evdevKeyMap.find(originalCode);
              if (mapIt != evdevKeyMap.end()) {
                mappedCode = mapIt->second;
              }
            }
            activeRemaps[originalCode] = mappedCode;
          } else {
            // On release: use the same mapping we stored on press
            auto it = activeRemaps.find(originalCode);
            if (it != activeRemaps.end()) {
              mappedCode = it->second;
              activeRemaps.erase(it);
            } else {
              // Fallback (defensive): shouldn't happen but handle it
              auto remapIt = evdevRemappedKeys.find(originalCode);
              if (remapIt != evdevRemappedKeys.end()) {
                mappedCode = remapIt->second;
              } else {
                auto mapIt = evdevKeyMap.find(originalCode);
                if (mapIt != evdevKeyMap.end()) {
                  mappedCode = mapIt->second;
                }
              }
            }
          }
        }

        evdevKeyState[originalCode] = down;
        // Track active inputs for combos
        {
          std::lock_guard<std::mutex> lk(activeInputsMutex);
          if (down)
            activeInputs[mappedCode] = std::chrono::steady_clock::now();
          else
            activeInputs.erase(mappedCode);
        }

        // Update modifier state from evdev
        bool ctrl =
            evdevKeyState[KEY_LEFTCTRL] || evdevKeyState[KEY_RIGHTCTRL] ||
            IsKeyRemappedTo(KEY_LEFTCTRL) || IsKeyRemappedTo(KEY_RIGHTCTRL);
        bool shift =
            evdevKeyState[KEY_LEFTSHIFT] || evdevKeyState[KEY_RIGHTSHIFT] ||
            IsKeyRemappedTo(KEY_LEFTSHIFT) || IsKeyRemappedTo(KEY_RIGHTSHIFT);
        bool alt = evdevKeyState[KEY_LEFTALT] || evdevKeyState[KEY_RIGHTALT] ||
                   IsKeyRemappedTo(KEY_LEFTALT) ||
                   IsKeyRemappedTo(KEY_RIGHTALT);
        bool meta =
            evdevKeyState[KEY_LEFTMETA] || evdevKeyState[KEY_RIGHTMETA] ||
            IsKeyRemappedTo(KEY_LEFTMETA) || IsKeyRemappedTo(KEY_RIGHTMETA);
        modState[ControlMask] = ctrl;
        modState[ShiftMask] = shift;
        modState[Mod1Mask] = alt;
        modState[Mod4Mask] = meta;
        // Update exported atomic mask
        int mask = 0;
        if (ctrl)
          mask |= ControlMask;
        if (shift)
          mask |= ShiftMask;
        if (alt)
          mask |= Mod1Mask;
        if (meta)
          mask |= Mod4Mask;
        currentEvdevModifiers.store(mask);
        // Update detailed state
        currentModifierState.leftCtrl = evdevKeyState[KEY_LEFTCTRL];
        currentModifierState.rightCtrl = evdevKeyState[KEY_RIGHTCTRL];
        currentModifierState.leftShift = evdevKeyState[KEY_LEFTSHIFT];
        currentModifierState.rightShift = evdevKeyState[KEY_RIGHTSHIFT];
        currentModifierState.leftAlt = evdevKeyState[KEY_LEFTALT];
        currentModifierState.rightAlt = evdevKeyState[KEY_RIGHTALT];
        currentModifierState.leftMeta = evdevKeyState[KEY_LEFTMETA];
        currentModifierState.rightMeta = evdevKeyState[KEY_RIGHTMETA];
        // Legacy global alt
        if (modState[Mod1Mask]) {
          debug("Global alt state set to {}", down);
          setGlobalAltState(down);
        }
        keyDownState[originalCode] = down;
        debug("Key {} state set to {}", originalCode, down);
        // Process hotkeys using ORIGINAL code
        std::vector<std::function<void()>> callbacks;
        bool shouldBlockKey = false;
        // Emergency hotkey
        if (down && emergencyKey == originalCode) {
          bool modifierMatch;

          if (parsedEmergencyHotkey.modifiers == 0) {
            // No modifiers required - match if no modifiers pressed
            modifierMatch = (mask == 0);
          } else {
            // Exact modifier match required
            modifierMatch = (mask == parsedEmergencyHotkey.modifiers);
          }

          if (modifierMatch) {
            error("🚨 EMERGENCY HOTKEY TRIGGERED! Shutting down...");
            shouldBlockKey = true;

            // Set flag to exit cleanly after this event loop
            evdevRunning = false;
            shutdown = true;
            break; // Exit the event processing loop immediately
          }
        }

        {
          std::unique_lock<std::timed_mutex> hotkeyLock(hotkeyMutex,
                                                        std::defer_lock);

          // Try to acquire lock with timeout
          if (!hotkeyLock.try_lock_for(std::chrono::milliseconds(100))) {
            error("Failed to acquire hotkey mutex within timeout - possible "
                  "deadlock detected");
            // Skip this event rather than blocking forever
            continue;
          }

          for (auto &[id, hotkey] : hotkeys) {
            if (!hotkey.enabled || !hotkey.evdev)
              continue;

// Debug output to help diagnose hotkey matching
#ifdef DEBUG_HOTKEYS
            debug("Hotkey check - Original: {}, Mapped: {}, Hotkey: {}",
                  originalCode, mappedCode, static_cast<int>(hotkey.key));
#endif

            // Match against ORIGINAL key code
            if (hotkey.key != static_cast<Key>(originalCode))
              continue;

            // Event type check
            if (!hotkey.repeat && repeat)
              continue;

            if (hotkey.eventType == HotkeyEventType::Down && !down)
              continue;
            if (hotkey.eventType == HotkeyEventType::Up && down)
              continue;

            // Modifier matching
            bool isModifierKey =
                (originalCode == KEY_LEFTALT || originalCode == KEY_RIGHTALT ||
                 originalCode == KEY_LEFTCTRL ||
                 originalCode == KEY_RIGHTCTRL ||
                 originalCode == KEY_LEFTSHIFT ||
                 originalCode == KEY_RIGHTSHIFT ||
                 originalCode == KEY_LEFTMETA || originalCode == KEY_RIGHTMETA);

            bool modifierMatch;
            if (isModifierKey && hotkey.modifiers == 0) {
              modifierMatch = true;
            } else {
              bool ctrlRequired = (hotkey.modifiers & (1 << 0)) != 0;
              bool shiftRequired = (hotkey.modifiers & (1 << 1)) != 0;
              bool altRequired = (hotkey.modifiers & (1 << 2)) != 0;
              bool metaRequired = (hotkey.modifiers & (1 << 3)) != 0;

              bool ctrlPressed = modState[ControlMask];
              bool shiftPressed = modState[ShiftMask];
              bool altPressed = modState[Mod1Mask];
              bool metaPressed = modState[Mod4Mask];
              if (hotkey.wildcard) {
                // Wildcard: only check that REQUIRED modifiers are pressed
                // (ignore extra modifiers)
                modifierMatch = (!ctrlRequired || ctrlPressed) &&
                                (!shiftRequired || shiftPressed) &&
                                (!altRequired || altPressed) &&
                                (!metaRequired || metaPressed);
              } else {
                // Normal: exact modifier match
                modifierMatch = (ctrlRequired == ctrlPressed) &&
                                (shiftRequired == shiftPressed) &&
                                (altRequired == altPressed) &&
                                (metaRequired == metaPressed);
              }
            }

            if (!modifierMatch)
              continue;

            // Context checks
            if (!hotkey.contexts.empty()) {
              if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                               [](auto &ctx) { return ctx(); })) {
                continue;
              }
            }
            // Check repeat interval if specified
            if (hotkey.repeatInterval > 0 && repeat) {
              auto now = std::chrono::steady_clock::now();
              auto elapsed =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - hotkey.lastTriggerTime)
                      .count();

              if (elapsed < hotkey.repeatInterval) {
                // Too soon, skip this repeat
                continue;
              }
              hotkey.lastTriggerTime = now;
            } else if (down && !repeat) {
              // First press, initialize timer
              hotkey.lastTriggerTime = std::chrono::steady_clock::now();
            }

            hotkey.success = true;
            debug("Hotkey {} triggered, key: {}, modifiers: {}, down: {}, "
                  "repeat: {}",
                  hotkey.alias, hotkey.key, hotkey.modifiers, down, repeat);
            std::thread([callback = hotkey.callback, alias = hotkey.alias,
                         key = hotkey.key, modifiers = hotkey.modifiers,
                         this]() {
              try {
                info("Executing hotkey callback: {} key: {} modifiers: {}",
                     alias, key, modifiers);
                pendingCallbacks++;
                if (evdevRunning && !shutdown) {
                  callback();
                }
              } catch (const std::exception &e) {
                error("Hotkey '{}' threw: {}", alias, e.what());
              } catch (...) {
                error("Hotkey '{}' threw unknown exception", alias);
              }
              pendingCallbacks--;
            }).detach();
            if (hotkey.grab) {
              shouldBlockKey = true;
            }
          }
        }
        if (shutdown) {
          if (QApplication::instance()) {
            QApplication::quit();
          } else {
            std::raise(SIGKILL);
          }
        }
        if (evdevRunning && !shutdown) {
          if (shouldBlockKey) {
            if (!down) {
              // Always release keys so modifiers don't stick
              SendUInput(mappedCode, false);
            } else {
              debug("Blocking key {} down (mapped from {})", mappedCode,
                    originalCode);
            }
          } else {
            SendUInput(mappedCode, down);
          }
        }
      }
    }
    info("Evdev hotkey listener stopped, ungrabbing");
    ioctl(fd, EVIOCGRAB, 0);
    close(fd);
  });

  return true;
}
void IO::StopEvdevHotkeyListener() {
  if (!evdevRunning)
    return;

  info("Stopping evdev hotkey listener...");
  evdevRunning = false;
  // Signal shutdown
  if (evdevShutdownFd >= 0) {
    uint64_t val = 1;
    write(evdevShutdownFd, &val, sizeof(val));
  }

  // Wait for evdev thread to end BEFORE touching uinput
  if (evdevThread.joinable()) {
    info("Waiting for evdev thread to exit...");
    evdevThread.join(); // No timeout. Let it exit cleanly.
    info("Evdev thread exited.");
  }

  info("Evdev hotkey listener stopped");
}

void IO::CleanupUinputDevice() {
  if (mouseUinputFd >= 0) {
    ioctl(mouseUinputFd, UI_DEV_DESTROY);
    close(mouseUinputFd);
    mouseUinputFd = -1;
  }
  if (uinputFd >= 0) {
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    uinputFd = -1;
  }
}
void IO::StartEvdevGamepadListener(const std::string &devicePath) {
  // Similar to StartEvdevHotkeyListener but for gamepad events
  info("Starting gamepad listener for device: {}", devicePath);

  // Implementation would handle gamepad-specific events
  // like analog stick movements, trigger presses, etc.
  // This could be used for gamepad-based hotkeys or controls
}

void IO::StopEvdevGamepadListener() {
  // Stop gamepad listener
  info("Stopping gamepad listener");
}
// Mouse event handling methods
bool IO::StartEvdevMouseListener(const std::string &mouseDevicePath) {
  if (mouseEvdevRunning) {
    info("Mouse listener already running");
    return false;
  }

  if (mouseDevicePath.empty()) {
    error("No mouse device path provided");
    return false;
  }

  info("Starting mouse listener on device: {}", mouseDevicePath);
  mouseEvdevDevicePath = mouseDevicePath;

  // Verify device exists and is accessible
  if (access(mouseDevicePath.c_str(), R_OK) != 0) {
    error("Cannot access mouse device {}: {}", mouseDevicePath,
          strerror(errno));
    return false;
  }

  mouseEvdevRunning = true;
  mouseEvdevThread = std::thread([this]() {
    int mouseDeviceFd = -1; // Local variable, not member

    try {
      info("Opening mouse device: {}", mouseEvdevDevicePath);
      mouseDeviceFd = open(mouseEvdevDevicePath.c_str(), O_RDONLY | O_NONBLOCK);
      if (mouseDeviceFd < 0) {
        throw std::runtime_error("Failed to open device: " +
                                 std::string(strerror(errno)));
      }

      // Get device capabilities
      unsigned long evbit = 0;
      if (ioctl(mouseDeviceFd, EVIOCGBIT(0, sizeof(evbit) * 8), &evbit) < 0) {
        error("Failed to get device capabilities: {}", strerror(errno));
        close(mouseDeviceFd);
        mouseEvdevRunning = false;
        return;
      }

      // Check if device has mouse/pointer capabilities
      if (!(evbit & (1 << EV_REL)) || !(evbit & (1 << EV_KEY))) {
        error("Device does not appear to be a mouse (missing EV_REL or EV_KEY "
              "capability)");
        close(mouseDeviceFd);
        mouseEvdevRunning = false;
        return;
      }

      // Get device name
      char name[256] = "Unknown";
      if (ioctl(mouseDeviceFd, EVIOCGNAME(sizeof(name)), name) < 0) {
        warning("Failed to get device name: {}", strerror(errno));
      } else {
        info("Mouse device name: {}", name);
      }

      // Try to grab mouse exclusively (non-blocking)
      if (ioctl(mouseDeviceFd, EVIOCGRAB, 1) < 0) {
        warning("Failed to grab mouse exclusively (another process may have "
                "it): {}",
                strerror(errno));
        // Continue in non-exclusive mode
      } else {
        info("Successfully grabbed mouse exclusively");
      }

      if (!SetupMouseUinputDevice()) {
        close(mouseDeviceFd);
        mouseEvdevRunning = false;
        error("mouse evdev: failed to setup uinput device");
        return;
      }

      struct input_event ev{};
      fd_set fds;
      struct timeval tv;
      int retval;

      info("Starting mouse event loop");

      while (mouseEvdevRunning) {
        FD_ZERO(&fds);
        FD_SET(mouseDeviceFd, &fds);

        // Set timeout to 100ms
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        // Wait for input to become available
        retval = select(mouseDeviceFd + 1, &fds, NULL, NULL, &tv);

        if (retval == -1) {
          // Error occurred
          if (errno == EINTR)
            continue; // Interrupted by signal
          error("select() error: {}", strerror(errno));
          break;
        } else if (retval == 0) {
          // Timeout occurred, check if we should still be running
          continue;
        }

        // Data is available, read the event
        ssize_t n = read(mouseDeviceFd, &ev, sizeof(ev));
        if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          }
          error("mouse evdev: read error: {}", strerror(errno));
          break;
        } else if (n != sizeof(ev)) {
          warning("Incomplete read: {} bytes (expected {})", n, sizeof(ev));
          continue;
        }

        bool shouldBlock = false;

        // Handle different mouse events
        switch (ev.type) {
        case EV_KEY: {
          if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT ||
              ev.code == BTN_MIDDLE || ev.code == BTN_SIDE ||
              ev.code == BTN_EXTRA) {
            shouldBlock = handleMouseButton(ev);
          } else {
            // Non-mouse key events on mouse device (e.g., extra buttons)
            shouldBlock = handleMouseButton(ev);
          }
          break;
        }
        case EV_REL: // Mouse movement
        {
          // Only log movement events occasionally to avoid log spam
          static int moveCounter = 0;
          if (++moveCounter % 100 == 0) {
            debug("Mouse movement: code={}, value={}", ev.code, ev.value);
          }
          shouldBlock = handleMouseRelative(ev);
          if (shouldBlock) {
            if (moveCounter > 0) {
              debug("Blocking mouse movement: code={}, value={}", ev.code,
                    ev.value);
              moveCounter = 0;
            }
          }
        } break;

        case EV_SYN:
          break;

        default:
          // Forward other events when not blocking
          if (!shouldBlock && mouseUinputFd >= 0) {
            if (write(mouseUinputFd, &ev, sizeof(ev)) != sizeof(ev)) {
              error("Failed to forward event (type={}, code={}): {}", ev.type,
                    ev.code, strerror(errno));
            }
          } else {
            debug("Unhandled event type: {}, code: {}, value: {}", ev.type,
                  ev.code, ev.value);
          }
          break;
        }

        // Forward non-blocked events to uinput
        if (!shouldBlock) {
          SendMouseUInput(ev);
        }
      }

    } catch (const std::exception &e) {
      error("Exception in mouse event loop: {}", e.what());
    }

    // Cleanup section - always executed
    info("Shutting down mouse event loop");

    if (mouseDeviceFd >= 0) {
      // Release exclusive grab if we had one
      if (ioctl(mouseDeviceFd, EVIOCGRAB, 0) < 0) {
        warning("Failed to release exclusive grab: {}", strerror(errno));
      }
      close(mouseDeviceFd);
    }

    if (mouseUinputFd >= 0) {
      try {
        // Send a sync event to ensure all pending events are processed
        struct input_event syn = {};
        syn.type = EV_SYN;
        syn.code = SYN_REPORT;
        syn.value = 0;
        if (write(mouseUinputFd, &syn, sizeof(syn)) != sizeof(syn)) {
          error("Failed to send final sync event: {}", strerror(errno));
        }

        if (ioctl(mouseUinputFd, UI_DEV_DESTROY) < 0) {
          error("Failed to destroy uinput device: {}", strerror(errno));
        }

        close(mouseUinputFd);
        mouseUinputFd = -1;
        info("Successfully cleaned up uinput device");
      } catch (const std::exception &e) {
        error("Exception during uinput cleanup: {}", e.what());
      }
    }

    mouseEvdevRunning = false;
    info("Mouse event loop stopped");
  });

  // Set thread name for debugging
  std::string threadName =
      "mouse-evt-" +
      std::to_string(std::hash<std::thread::id>{}(mouseEvdevThread.get_id()));
  pthread_setname_np(mouseEvdevThread.native_handle(), threadName.c_str());

  info("Mouse event listener started successfully");
  return true;
}

void IO::StopEvdevMouseListener() {
  if (!mouseEvdevRunning) {
    debug("Mouse listener not running");
    return;
  }

  info("Stopping mouse event listener...");
  mouseEvdevRunning = false;

  // Wait for the thread to finish with timeout
  if (mouseEvdevThread.joinable()) {
    mouseEvdevThread.join();
  }

  info("Mouse event listener stopped");
}

bool IO::handleMouseButton(const input_event &ev) {
  bool shouldBlock = false;
  auto now = std::chrono::steady_clock::now();

  // Get current modifier state
  int currentModifiers = GetCurrentModifiers();

  // Check for registered hotkeys
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::MouseButton)
      continue;

    // Check if this is the right button and event type
    bool isButtonMatch = false;
    switch (ev.code) {
    case BTN_LEFT:
      isButtonMatch = (hotkey.mouseButton == BTN_LEFT);
      break;
    case BTN_RIGHT:
      isButtonMatch = (hotkey.mouseButton == BTN_RIGHT);
      break;
    case BTN_MIDDLE:
      isButtonMatch = (hotkey.mouseButton == BTN_MIDDLE);
      break;
    case BTN_SIDE:
      isButtonMatch = (hotkey.mouseButton == BTN_SIDE);
      break;
    case BTN_EXTRA:
      isButtonMatch = (hotkey.mouseButton == BTN_EXTRA);
      break;
    case BTN_FORWARD:
      isButtonMatch = (hotkey.mouseButton == BTN_FORWARD);
      break;
    case BTN_BACK:
      isButtonMatch = (hotkey.mouseButton == BTN_BACK);
      break;
    }

    if (isButtonMatch &&
        (hotkey.eventType == HotkeyEventType::Both ||
         (hotkey.eventType == HotkeyEventType::Down && ev.value == 1) ||
         (hotkey.eventType == HotkeyEventType::Up && ev.value == 0))) {

      // Check modifiers match
      if ((hotkey.modifiers & currentModifiers) == hotkey.modifiers) {
        // Execute the hotkey callback
        if (hotkey.callback) {
          info("Executing hotkey callback: {} key: {} modifiers: {}",
               hotkey.alias, hotkey.key, hotkey.modifiers);
          hotkey.callback();
        }
        shouldBlock = hotkey.grab;

        // If this is a blocking hotkey, we're done
        if (shouldBlock) {
          debug("Blocking mouse button {} down", ev.code);
          return true;
        }
      }
    }
  }

  // Note: additional mouse combo logic handled above in per-button branches
  // Maintain button states and activeInputs for all buttons
  switch (ev.code) {
  case BTN_LEFT:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_LEFT] = true;
      leftButtonDown.store(true);
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_LEFT] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_LEFT] = false;
      leftButtonDown.store(false);
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_LEFT);
    }
    break;
  case BTN_RIGHT:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_RIGHT] = true;
      rightButtonDown.store(true);
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_RIGHT] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_RIGHT] = false;
      rightButtonDown.store(false);
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_RIGHT);
    }
    break;
  case BTN_MIDDLE:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_MIDDLE] = true;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_MIDDLE] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_MIDDLE] = false;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_MIDDLE);
    }
    break;
  case BTN_SIDE:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_SIDE] = true;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_SIDE] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_SIDE] = false;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_SIDE);
    }
    break;
  case BTN_EXTRA:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_EXTRA] = true;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_EXTRA] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_EXTRA] = false;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_EXTRA);
    }
    break;
  case BTN_FORWARD:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_FORWARD] = true;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_FORWARD] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_FORWARD] = false;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_FORWARD);
    }
    break;
  case BTN_BACK:
    if (ev.value == 1) {
      evdevMouseButtonState[BTN_BACK] = true;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs[BTN_BACK] = now;
    } else if (ev.value == 0) {
      evdevMouseButtonState[BTN_BACK] = false;
      std::lock_guard<std::mutex> lk(activeInputsMutex);
      activeInputs.erase(BTN_BACK);
    }
    break;
  }
  // Evaluate any registered combo hotkeys (mouse + modifiers)
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::Combo)
      continue;
    if ((hotkey.modifiers & GetCurrentModifiers()) != hotkey.modifiers)
      continue;
    if (EvaluateCombo(hotkey)) {
      info("Executing combo hotkey button callback: {} key: {} modifiers: {}",
           hotkey.alias, hotkey.key, hotkey.modifiers);
      if (hotkey.callback)
        hotkey.callback();
      if (hotkey.grab)
        return true;
      shouldBlock = shouldBlock || hotkey.grab;
    }
  }
  return shouldBlock;
}

bool IO::handleMouseRelative(const input_event &ev) {
  bool shouldBlock = false;
  // Current modifiers in X11 mask form (ShiftMask, ControlMask, Mod1Mask,
  // Mod4Mask)
  int currentModifiersX11 = GetCurrentModifiers();
  // Build evdev-style bitmask: bit0=Ctrl, bit1=Shift, bit2=Alt, bit3=Meta
  int currentModifiersEvdev = 0;
  if (currentModifierState.leftCtrl || currentModifierState.rightCtrl)
    currentModifiersEvdev |= (1 << 0);
  if (currentModifierState.leftShift || currentModifierState.rightShift)
    currentModifiersEvdev |= (1 << 1);
  if (currentModifierState.leftAlt || currentModifierState.rightAlt)
    currentModifiersEvdev |= (1 << 2);
  if (currentModifierState.leftMeta || currentModifierState.rightMeta)
    currentModifiersEvdev |= (1 << 3);

  if (ev.code == REL_WHEEL) {
    debug("WHEEL: X11Mods=0x{:x}, EvdevMods=0x{:x} | ctrl(L/R)={}{} "
          "shift(L/R)={}{} alt(L/R)={}{} meta(L/R)={}{}",
          currentModifiersX11, currentModifiersEvdev,
          currentModifierState.leftCtrl, currentModifierState.rightCtrl,
          currentModifierState.leftShift, currentModifierState.rightShift,
          currentModifierState.leftAlt, currentModifierState.rightAlt,
          currentModifierState.leftMeta, currentModifierState.rightMeta);
  }
  // Check for registered wheel hotkeys
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::MouseWheel)
      continue;

    // Check if this is the right wheel direction
    bool isWheelMatch = false;
    if (ev.code == REL_WHEEL) { // Vertical wheel
      isWheelMatch = (hotkey.wheelDirection == (ev.value > 0 ? 1 : -1));
    } else if (ev.code == REL_HWHEEL) { // Horizontal wheel
      isWheelMatch = (hotkey.wheelDirection == (ev.value > 0 ? 1 : -1));
    }

    if (isWheelMatch) {
      // Choose correct modifier domain for comparison
      int currentMods =
          hotkey.evdev ? currentModifiersEvdev : currentModifiersX11;
      bool match = (hotkey.modifiers == currentMods);
      debug("WHEEL MODIFIER CHECK: expected={} current={} evdev={} match={} "
            "alias={}",
            hotkey.modifiers, currentMods, hotkey.evdev, match, hotkey.alias);

      if (match) {
        info("MODIFIER MATCH - executing hotkey");
        if (hotkey.callback) {
          hotkey.callback();
        }
        shouldBlock = hotkey.grab;

        // If this is a blocking hotkey, we're done
        if (shouldBlock) {
          return true;
        }
      }
    }
  }

  // Handle mouse movement
  switch (ev.code) {
  case REL_X:   // Mouse X movement
  case REL_Y: { // Mouse Y movement
    // Scale the movement value based on sensitivity
    struct input_event scaledEvent = ev;

    // Apply sensitivity scaling
    double scaledValue = ev.value * mouseSensitivity;
    int32_t scaledInt = static_cast<int32_t>(std::round(scaledValue));

    // Preserve direction for small movements that would otherwise be rounded to
    // zero
    if (scaledInt == 0 && ev.value != 0) {
      scaledInt = (ev.value > 0) ? 1 : -1;
    }

    scaledEvent.value = scaledInt;

    debug("Scaling mouse movement: original={}, sensitivity={}x, scaled={}",
          ev.value, mouseSensitivity, scaledInt);

    // Forward the scaled event
    SendMouseUInput(scaledEvent);
    return true;
  }

  case REL_WHEEL: // Vertical scroll
    if (currentModifierState.leftAlt || currentModifierState.rightAlt) {
      info("🎯 ALT+SCROLL WHEEL: {}", ev.value > 0 ? "UP" : "DOWN");

      if (ev.value > 0) {
        executeComboAction("alt_scroll_up");
      } else {
        executeComboAction("alt_scroll_down");
      }

      shouldBlock = true; // Block the scroll from reaching system
    }
    break;

  default:
    // Log unhandled event types but don't forward them
    debug("Unhandled relative event type: {}, code: {}, value: {}", ev.type,
          ev.code, ev.value);
    break;
  }

  // Evaluate combo hotkeys as well (e.g., wheel + button combos)
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::Combo)
      continue;
    int currentModsForCombo =
        hotkey.evdev ? currentModifiersEvdev : currentModifiersX11;
    if ((hotkey.modifiers & currentModsForCombo) != hotkey.modifiers)
      continue;
    if (EvaluateCombo(hotkey)) {
      info("Combo hotkey pressed: {}", hotkey.alias);
      if (hotkey.callback)
        hotkey.callback();
      if (hotkey.grab)
        return true;
      shouldBlock = shouldBlock || hotkey.grab;
    }
  }

  return shouldBlock;
}

bool havel::IO::EvaluateCombo(const HotKey &combo) {
  // Require all parts to be currently active (pressed) within comboTimeWindow
  if (combo.comboSequence.empty())
    return false;
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(activeInputsMutex);
  for (const auto &part : combo.comboSequence) {
    int code = -1;
    if (part.type == HotkeyType::MouseButton) {
      code = part.mouseButton;
    } else if (part.type == HotkeyType::MouseWheel) {
      // For wheel events, we use the wheelDirection to determine up/down
      // Wheel up is typically positive, wheel down is negative
      // We'll use the absolute value since we just need to match the button
      // state
      code = (part.wheelDirection > 0) ? 9 : 8;
    } else if (part.type == HotkeyType::Keyboard) {
      // Map keyboard key to evdev code
      code = part.key;
      if (code <= 0) {
        // If keyCode not set, try to get it from the key name
        code = EvdevNameToKeyCode(part.alias);
      }
    }
    if (code < 0)
      return false;
    auto it = activeInputs.find(code);
    if (it == activeInputs.end())
      return false;
    auto age =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second)
            .count();
    if (age > combo.comboTimeWindow)
      return false;
  }
  return true;
}

bool havel::IO::handleMouseAbsolute(const input_event &ev) {
  // Get current modifier state
  int currentModifiers = GetCurrentModifiers();

  // Check for registered hotkeys that might be interested in absolute events
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::MouseMove)
      continue;

    // Check if this hotkey is interested in this specific absolute axis
    bool isAxisMatch = false;
    switch (ev.code) {
    case ABS_X:
    case ABS_Y:                                // Position
      isAxisMatch = (hotkey.mouseButton == 0); // 0 means any position
      break;
    case ABS_PRESSURE:                          // Pressure
      isAxisMatch = (hotkey.mouseButton == -2); // -2 means pressure
      break;
    case ABS_DISTANCE:                          // Distance
      isAxisMatch = (hotkey.mouseButton == -3); // -3 means distance
      break;
    }

    if (isAxisMatch) {
      // Check if modifiers match (exact match required)
      if ((hotkey.modifiers & currentModifiers) == hotkey.modifiers) {
        // Execute the hotkey callback with the current value
        info("Mouse hotkey pressed: {}", hotkey.alias);
        if (hotkey.callback) {
          hotkey.callback();
        }

        // If this is a blocking hotkey, we're done
        if (hotkey.grab) {
          return true;
        }
      }
    }
  }

  // Currently not blocking any absolute events by default
  return false;
}

bool havel::IO::SetupMouseUinputDevice() {
  mouseUinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (mouseUinputFd < 0) {
    error("mouse uinput: failed to open /dev/uinput: {}", strerror(errno));
    return false;
  }

  struct uinput_setup usetup = {};
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5679;
  strcpy(usetup.name, "havel-uinput-mouse");

  // Enable mouse events
  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_KEY);
  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_REL);

  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_SYN);

  // Enable mouse buttons
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_SIDE);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_EXTRA);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_FORWARD);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_BACK);

  // Enable mouse movement and scroll
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_X);
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_Y);
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_WHEEL);
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_HWHEEL);

  if (ioctl(mouseUinputFd, UI_DEV_SETUP, &usetup) < 0) {
    error("mouse uinput: device setup failed: {}", strerror(errno));
    close(mouseUinputFd);
    mouseUinputFd = -1;
    return false;
  }

  if (ioctl(mouseUinputFd, UI_DEV_CREATE) < 0) {
    error("mouse uinput: device creation failed: {}", strerror(errno));
    close(mouseUinputFd);
    mouseUinputFd = -1;
    return false;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return true;
}

void havel::IO::SendMouseUInput(const input_event &ev) {
  // Use EventListener's uinput if available, otherwise use old method
  if (eventListener && useNewEventListener) {
    eventListener->SendUinputEvent(ev.type, ev.code, ev.value);
  } else if (mouseUinputFd >= 0) {
    // Use a mutex to serialize all uinput writes (REL/SYN pairs must be
    // atomic).
    static std::mutex uinputWriteMutex;
    std::lock_guard<std::mutex> lk(uinputWriteMutex);

    ssize_t res = write(mouseUinputFd, &ev, sizeof(ev));
    if (res != (ssize_t)sizeof(ev)) {
      error("Failed to write uinput event (type={}, code={}, value={}): {}",
            ev.type, ev.code, ev.value, strerror(errno));
      return;
    }

    struct input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;

    res = write(mouseUinputFd, &syn, sizeof(syn));
    if (res != (ssize_t)sizeof(syn)) {
      error("Failed to write uinput SYN: {}", strerror(errno));
    }
  }
}

void havel::IO::setGlobalAltState(bool pressed) {
  globalAltPressed.store(pressed);
}

bool havel::IO::getGlobalAltState() { return globalAltPressed.load(); }
void IO::executeComboAction(const std::string &action) {
  info("Executing combo action: {}", action);

  // Transform action to hotkey alias
  std::string targetAlias;
  if (action == "alt_scroll_up")
    targetAlias = "@!WheelUp";
  else if (action == "alt_scroll_down")
    targetAlias = "@!WheelDown";
  else
    targetAlias = action;

  debug("Looking for hotkey with alias: '{}'", targetAlias);

  // Get current modifier state
  int currentMods = GetCurrentModifiers();
  debug("Current modifiers: 0x{:x}", currentMods);

  for (auto &[id, hotkey] : hotkeys) {
    if (hotkey.enabled && hotkey.callback && hotkey.alias == targetAlias) {
      // Check modifiers match
      if (hotkey.modifiers == currentMods) {
        info("Executing hotkey '{}' for action '{}'", hotkey.alias, action);
        hotkey.callback();
        return;
      } else {
        debug("Modifiers don't match: expected={}, current={}",
              hotkey.modifiers, currentMods);
      }
    }
  }

  warn("No handler found for combo action: {}", action);
}
// OPTIMIZED: Mouse button click methods with event batching
void IO::MouseClick(int button) {
  MouseDown(button);
  std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay
  MouseUp(button);
}

void IO::MouseDown(int button) {
  // OPTIMIZATION: Use EventListener's batched uinput
  if (eventListener && useNewEventListener) {
    // Single combined write: press + sync
    eventListener->SendUinputEvent(EV_KEY, button, 1);     // Press
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0); // Sync
  } else if (mouseUinputFd >= 0) {
    static std::mutex uinputWriteMutex;
    std::lock_guard<std::mutex> lk(uinputWriteMutex);
    
    // OPTIMIZATION #1: Batch press and sync into one write operation
    struct input_event events[2];
    gettimeofday(&events[0].time, nullptr);
    events[0].type = EV_KEY;
    events[0].code = button;
    events[0].value = 1; // Press

    gettimeofday(&events[1].time, nullptr);
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;

    // ONE syscall for press + sync
    ssize_t res = write(mouseUinputFd, events, sizeof(events));
    if (res != (ssize_t)sizeof(events)) {
      error("Failed to write batched mouse down events: {}", strerror(errno));
    }
  }
}

void IO::MouseUp(int button) {
  // OPTIMIZATION: Use EventListener's batched uinput
  if (eventListener && useNewEventListener) {
    // Single combined write: release + sync
    eventListener->SendUinputEvent(EV_KEY, button, 0);     // Release
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0); // Sync
  } else if (mouseUinputFd >= 0) {
    static std::mutex uinputWriteMutex;
    std::lock_guard<std::mutex> lk(uinputWriteMutex);
    
    // OPTIMIZATION #1: Batch release and sync into one write operation
    struct input_event events[2];
    gettimeofday(&events[0].time, nullptr);
    events[0].type = EV_KEY;
    events[0].code = button;
    events[0].value = 0; // Release

    gettimeofday(&events[1].time, nullptr);
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;

    // ONE syscall for release + sync
    ssize_t res = write(mouseUinputFd, events, sizeof(events));
    if (res != (ssize_t)sizeof(events)) {
      error("Failed to write batched mouse up events: {}", strerror(errno));
    }
  }
}

void IO::MouseWheel(int amount) {
  // OPTIMIZATION: Use EventListener's batched uinput
  if (eventListener && useNewEventListener) {
    int wheelValue = amount > 0 ? 1 : -1;
    eventListener->SendUinputEvent(EV_REL, REL_WHEEL, wheelValue);
    eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0); // Sync
  } else if (mouseUinputFd >= 0) {
    static std::mutex uinputWriteMutex;
    std::lock_guard<std::mutex> lk(uinputWriteMutex);
    
    // OPTIMIZATION #1: Batch wheel and sync into one write operation
    struct input_event events[2];
    gettimeofday(&events[0].time, nullptr);
    events[0].type = EV_REL;
    events[0].code = REL_WHEEL;
    events[0].value = amount > 0 ? 1 : -1;

    gettimeofday(&events[1].time, nullptr);
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;

    // ONE syscall for wheel + sync
    ssize_t res = write(mouseUinputFd, events, sizeof(events));
    if (res != (ssize_t)sizeof(events)) {
      error("Failed to write batched mouse wheel events: {}", strerror(errno));
    }
  }
}
} // namespace havel
