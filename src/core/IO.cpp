#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/io/KeyTap.hpp"

// Global storage for KeyTap instances
static std::mutex g_keyTapMutex;
static std::vector<std::unique_ptr<havel::KeyTap>> g_keyTapStorage;

#include "include/x11_includes.h"
#include "io/EventListener.hpp"
#include "io/HotkeyExecutor.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "utils/Utils.hpp"
#include "window/WindowManager.hpp"
#include "window/WindowManagerDetector.hpp"
#include <chrono>
#include <fcntl.h>
#include <future>
#include <linux/input.h>
#include <linux/uinput.h>
#include <mutex>
#include <qapplication.h>
#include <qtmetamacros.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
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
static auto &hotkeys = HotkeyManager::RegisteredHotkeys();
static auto &hotkeysMutex = HotkeyManager::RegisteredHotkeysMutex();
bool IO::hotkeyEnabled = true;
int IO::hotkeyCount = 0;
bool IO::globalEvdev = true;
double IO::mouseSensitivity = 1.0;
double IO::scrollSpeed = 1.0;
std::atomic<int> IO::syntheticEventsExpected{0};
// Scroll accumulation for fractional values
static double scrollAccumY = 0.0;
static double scrollAccumX = 0.0;
// Static keycode cache - eliminates repeated lookups for same keys
static std::unordered_map<std::string, int> g_keycodeCache;
static std::mutex g_keycodeCacheMutex;

int IO::GetKeyCacheLookup(const std::string &keyName) {
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
std::vector<KeyToken> IO::ParseKeyString(const std::string &keys) {
  std::vector<KeyToken> tokens;
  size_t i = 0;

  static std::unordered_map<char, std::string> shorthandModifiers = {
      {'^', "ctrl"}, {'!', "alt"},           {'+', "shift"},
      {'#', "meta"}, {'@', "toggle_uinput"}, {'%', "toggle_x11"}};

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
void IO::SendBatchedKeyEvents(const std::vector<input_event> &events) {
  if (events.empty())
    return;

  // Delegate to EventListener
  if (eventListener) {
    eventListener->BeginUinputBatch();
    for (const auto &ev : events) {
      eventListener->QueueUinputEvent(ev.type, ev.code, ev.value);
    }
    eventListener->EndUinputBatch();
  }
}

void IO::SendUInput(int keycode, bool down) {
  if (!eventListener) {
    warning("SendUInput called without EventListener");
    return;
  }
  eventListener->SendUinputEvent(EV_KEY, keycode, down ? 1 : 0);
}

int IO::XErrorHandler(Display *dpy, XErrorEvent *ee) {
  // Suppress BadWindow errors on Wayland (common when XWayland windows
  // disappear)
  if (ee->error_code == 3) { // BadWindow error code is 3
    return 0;                // Suppress BadWindow errors
  }

  // Only suppress access errors, not BadWindow errors
  if ((ee->request_code == X_GrabButton || ee->request_code == X_GrabKey) &&
      ee->error_code == 1) { // BadAccess error code is 1
    return 0;                // Suppress access errors for grabbing
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
    debug("✅ Selected keyboard: '{}' -> {} (confidence: {:.1f}%)",
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
    debug("✅ Selected mouse: '{}' -> {} (confidence: {:.1f}%)", mice[0].name,
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
    debug("✅ Found gamepad: '{}' -> {} (confidence: {:.1f}%)", gamepads[0].name,
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

std::string IO::GetActiveWindowTitle() {
  return WindowManager::GetActiveWindowTitle();
}

std::string IO::GetActiveWindowClass() {
  return WindowManager::GetActiveWindowClass();
}

std::string IO::GetActiveWindowProcess() {
  return WindowManager::GetActiveWindowProcess();
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
  XSetErrorHandler(IO::XErrorHandler);
  DisplayManager::Initialize();
  display = DisplayManager::GetDisplay();

  // Initialize ImportManager
  importManager = std::make_shared<ImportManager>();

  // Initialize KeyMap
  KeyMap::Initialize();

  // Register cleanup handler to ensure evdev ungrab on exit
  static bool cleanupRegistered = false;
  if (!cleanupRegistered) {
    std::atexit([]() {
      debug("atexit: forcing evdev ungrab on process exit");
      // Note: This is a last-resort cleanup if IO destructor doesn't run
      // The IO destructor should handle normal cleanup
    });
    cleanupRegistered = true;
  }

  // Read process priority and thread count from config
  int processPriority = Configs::Get().Get<int>(
      "Advanced.ProcessPriority", 0); // -20 (highest) to 19 (lowest)
  int workerThreads = Configs::Get().Get<int>("Advanced.WorkerThreads",
                                              4); // Number of worker threads

  // Set process priority (nice value)
  if (processPriority != 0) {
    processPriority = std::clamp(processPriority, -20, 19);
    if (setpriority(PRIO_PROCESS, 0, processPriority) == 0) {
      debug("Process priority set to nice {}", processPriority);
    } else {
      warning("Failed to set process priority to {}: {}", processPriority,
              strerror(errno));
    }
  }

  // Clamp worker threads to reasonable range
  workerThreads = std::clamp(workerThreads, 1, 32);

  // Initialize HotkeyExecutor for thread-safe hotkey execution
  hotkeyExecutor = std::make_unique<HotkeyExecutor>(workerThreads, 256);
  debug("HotkeyExecutor initialized with {} worker threads", workerThreads);

  InitKeyMap();
  mouseSensitivity = Configs::Get().Get<double>("Mouse.Sensitivity", 1.0);

#ifdef __linux__
  if (display) {
    UpdateNumLockMask();
    // Use new unified EventListener
    debug("Using new unified EventListener");

    std::vector<std::string> devices;

    // Get all keyboard devices (including auxiliary keyboards)
    auto keyboardDevices = Device::findKeyboards();
    for (const auto &kb : keyboardDevices) {
      devices.push_back(kb.eventPath);
      debug("Adding keyboard device: '{}' -> {} (confidence: {:.1f}%)", kb.name,
           kb.eventPath, kb.confidence * 100);
    }

    std::string mouseDevice = getMouseDevice();
    std::string gamepadDevice;

    if (!mouseDevice.empty() &&
        !Configs::Get().Get<bool>("Device.IgnoreMouse", false)) {
      // Only add mouse device if it's not already in the keyboard devices
      // list
      bool alreadyAdded = false;
      for (const auto &kb_device : keyboardDevices) {
        if (kb_device.eventPath == mouseDevice) {
          alreadyAdded = true;
          break;
        }
      }
      if (!alreadyAdded) {
        devices.push_back(mouseDevice);
        debug("Adding mouse device: {}", mouseDevice);
      }
    }

    bool enableGamepad =
        Configs::Get().Get<bool>("Device.EnableGamepad", false);
    if (enableGamepad) {
      gamepadDevice = getGamepadDevice();
      if (!gamepadDevice.empty()) {
        devices.push_back(gamepadDevice);
        debug("Adding gamepad device: {}", gamepadDevice);
      }
    }

    if (!devices.empty()) {
      try {
        eventListener = std::make_unique<EventListener>();
        eventListener->SetupUinput();

        // Initialize MouseController
        mouseController =
            std::make_unique<MouseController>(eventListener.get());
        mouseController->SetSensitivity(mouseSensitivity);
        mouseController->SetScrollSpeed(
            Configs::Get().Get<double>("Mouse.ScrollSpeed", 1.0));

        // Pass HotkeyExecutor to EventListener for thread-safe execution
        eventListener->SetHotkeyExecutor(hotkeyExecutor.get());

        // Set mouse and scroll sensitivity on EventListener (for internal use)
        eventListener->SetMouseSensitivity(mouseSensitivity);
        eventListener->SetScrollSpeed(
            Configs::Get().Get<double>("Mouse.ScrollSpeed", 1.0));

        globalEvdev = true;
        bool grab = ProcessManager::isTraced() ? false : Configs::Get().Get<bool>("Device.GrabDevices", true);
        debug("Starting EventListener with {} devices (grab={})", devices.size(),
              grab);
        eventListener->Start(devices, grab);
      } catch (const std::exception &e) {
        error("Failed to start unified EventListener: {}", e.what());
        globalEvdev = false;
      }
    } else {
      globalEvdev = false;
      error("No input devices found for EventListener");
    }

    // Debug output - show what we detected
    if (Configs::Get().Get<bool>("Device.ShowDetectionResults", false)) {
      listInputDevices();
    }
  }
#endif
}
IO::~IO() { cleanup(); }

void IO::cleanup() {
  // EventListener handles all cleanup now

  // Stop EventListener if using new event system
  if (eventListener) {
    // Force ungrab all evdev devices FIRST (before stopping thread)
    eventListener->ForceUngrabAllDevices();
    eventListener->Stop();
  }

  // Ungrab all hotkeys before closing
#ifdef __linux__
  if (display && !globalEvdev) {
    x11::Window root = DefaultRootWindow(display);

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
  // uinput cleanup handled by EventListener
  display = nullptr;

  // Additional safety: Force ungrab any remaining evdev devices
#ifdef __linux__
  // Force cleanup of any remaining evdev resources
  debug("Final cleanup completed - all devices should be ungrabbed");
#endif

  std::cout << "IO cleanup completed" << std::endl;
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
      debug("Successfully grabbed entire keyboard after {} attempts", i + 1);
      return true;
    }

    nanosleep(&ts, nullptr);
  }

  error("Cannot grab keyboard after 1000 attempts");
  return false;
}
bool IO::Grab(Key input, unsigned int modifiers, x11::Window root, bool grab,
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

void IO::Ungrab(Key input, unsigned int modifiers, x11::Window root,
                bool isMouse) {
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
bool IO::FastGrab(Key input, unsigned int modifiers, x11::Window root) {
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
bool IO::EmitClick(int btnCode, MouseAction action) {
  // Use EventListener's uinput for all mouse events
  if (eventListener) {
    // Map integer button codes to proper mouse button codes
    int mouseBtnCode = btnCode;
    if (btnCode >= 1 && btnCode <= 9) {
      // Map 1-9 to BTN_LEFT through BTN_9
      mouseBtnCode = BTN_LEFT + (btnCode - 1);
    }

    // Send events through EventListener
    switch (action) {
    case MouseAction::Release:
      eventListener->SendUinputEvent(EV_KEY, mouseBtnCode, 0);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      return true;

    case MouseAction::Hold:
      eventListener->SendUinputEvent(EV_KEY, mouseBtnCode, 1);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      return true;

    case MouseAction::Click: // Click (FAST)
      eventListener->SendUinputEvent(EV_KEY, mouseBtnCode, 1);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      eventListener->SendUinputEvent(EV_KEY, mouseBtnCode, 0);
      eventListener->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
      return true;

    default:
      error("Invalid mouse action: {}", static_cast<int>(action));
      return false;
    }
  }

  error("EmitClick: EventListener not available");
  return false;
}
bool IO::MouseMove(int dx, int dy, int speed, float accel) {
  // Use EventListener's uinput if available
  if (eventListener) {
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

  error("MouseMove: EventListener not available");
  return false;
}
bool IO::MouseMoveTo(int targetX, int targetY, int speed, float accel) {
  if (!eventListener) {
    error("MouseMoveTo: EventListener not available");
    return false;
  }

  // For X11, use XTestFakeMotionEvent for pixel-perfect positioning
  if (WindowManagerDetector::IsX11()) {
    Display *display = DisplayManager::GetDisplay();
    if (!display) {
      error("MouseMoveTo: No X11 display available");
      return false;
    }

    // Get current position for smooth animation
    auto currentPos = GetMousePositionX11();
    int currentX = currentPos.first;
    int currentY = currentPos.second;

    // Calculate distance
    int dx = targetX - currentX;
    int dy = targetY - currentY;
    int distance = std::abs(dx) + std::abs(dy);

    if (distance < 3) {
      // Already close enough, just jump directly
      XTestFakeMotionEvent(display, DefaultScreen(display), targetX, targetY,
                           CurrentTime);
      XFlush(display);
      return true;
    }

    // Calculate steps for smooth movement
    if (speed <= 0)
      speed = 5;
    int steps = std::min(30, std::max(5, distance / (speed * 10)));

    // Smooth animation using XTest
    for (int i = 0; i <= steps; ++i) {
      double progress = static_cast<double>(i) / steps;

      // Calculate intermediate position
      int stepX = currentX + static_cast<int>(dx * progress);
      int stepY = currentY + static_cast<int>(dy * progress);

      // Use XTest for exact positioning
      XTestFakeMotionEvent(display, DefaultScreen(display), stepX, stepY,
                           CurrentTime);
      XFlush(display);

      // Minimal sleep for smooth movement
      int sleepMs = std::max(1, 5 / std::max(1, speed));
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }

    // Ensure final position is exact
    XTestFakeMotionEvent(display, DefaultScreen(display), targetX, targetY,
                         CurrentTime);
    XFlush(display);

    return true;
  }

  // For Wayland, fall back to REL with feedback loop
  int currentX = 0, currentY = 0;
  (void)currentX;
  (void)currentY;
  return false;
}

bool IO::ClickAt(int x, int y, int button, int speed, float accel) {
  if (!mouseController) {
    error("Cannot click: MouseController not initialized");
    return false;
  }
  return mouseController->ClickAt(x, y, button, speed, accel);
}

bool IO::MouseMoveSensitive(int dx, int dy, int baseSpeed, float accel) {
  if (!mouseController) {
    error("Cannot move mouse: MouseController not initialized");
    return false;
  }
  return mouseController->MoveSensitive(dx, dy, baseSpeed, accel);
}

bool IO::Scroll(double dy, double dx) {
  if (!mouseController) {
    error("Cannot scroll: MouseController not initialized");
    return false;
  }
  return mouseController->Scroll(dy, dx);
}

void IO::SetMouseSensitivity(double sensitivity) {
  mouseSensitivity = sensitivity;
  if (mouseController) {
    mouseController->SetSensitivity(sensitivity);
  }
  if (eventListener) {
    eventListener->SetMouseSensitivity(sensitivity);
  }
}

double IO::GetMouseSensitivity() const { return mouseSensitivity; }

void IO::SetScrollSpeed(double speed) {
  scrollSpeed = speed;
  if (mouseController) {
    mouseController->SetScrollSpeed(speed);
  }
  if (eventListener) {
    eventListener->SetScrollSpeed(speed);
  }
}

double IO::GetScrollSpeed() const { return scrollSpeed; }

std::pair<int, int> IO::GetMousePosition() {
  if (!mouseController) {
    error("Cannot get mouse position: MouseController not initialized");
    return {0, 0};
  }
  return mouseController->GetPosition();
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

  debug("Set hardware mouse sensitivity to: {}", sensitivity);
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

  debug("Sending key: " + keyName + " (" + std::to_string(keycode) + ")");
  XTestFakeKeyEvent(display, keycode, press, CurrentTime);
  XFlush(display);
#endif
}
// SendUInput removed - delegate to EventListener instead
// EventListener now manages uinput through UinputDevice class
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

  // Delegate to EventListener for emergency release
  if (eventListener) {
    eventListener->EmergencyReleaseAllKeys();
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
  // OPTIMIZATION #1: Pre-parse the key string once instead of scanning
  // repeatedly
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
      // OPTIMIZATION #3: Cache keycode lookups to avoid repeated
      // string->keycode conversions
      int code = GetKeyCacheLookup(keyName);
      if (Configs::Get().GetVerboseKeyLogging()) {
        debug("Sending key: " + keyName + " (" + std::to_string(down) +
             ") code: " + std::to_string(code));
      }
      if (code != -1) {
        // Use EventListener's uinput if available, otherwise use old method
        if (eventListener) {
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
                        const std::string &leftKey,
                        const std::string &rightKey) {
      if (leftPressed)
        toRelease.insert(leftKey);
      if (rightPressed)
        toRelease.insert(rightKey);
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

  // OPTIMIZATION #5: Batch all uinput events for reduced syscall overhead
  // Events are queued and flushed in a single write() call
  if (eventListener) {
    eventListener->BeginUinputBatch();
  }

  for (const auto &token : tokens) {
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
        activeModifiers.erase(std::remove(activeModifiers.begin(),
                                          activeModifiers.end(), token.value),
                              activeModifiers.end());
      } else {
        SendKey(token.value, false);
      }
      break;
    }

    case KeyToken::Key: {
      debug("Sending key: " + token.value);
      SendKey(token.value, true);
      // OPTIMIZATION #4: Only sleep if explicitly configured (removed default
      // 100μs sleep)
      if (shouldSleep) {
        // Flush batch before sleep to ensure key is sent
        if (eventListener) {
          eventListener->EndUinputBatch();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        // Resume batching after sleep
        if (eventListener) {
          eventListener->BeginUinputBatch();
        }
      }
      SendKey(token.value, false);
      break;
    }
    }
  }

  // Flush any remaining batched events
  if (eventListener) {
    eventListener->EndUinputBatch();
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
        hotkeyManager->setMode("default"); // Switch to default mode

        // Restore conditional hotkeys to their original state before suspension
        if (!suspendedConditionalHotkeyStates.empty()) {
          std::lock_guard<std::mutex> lock(hotkeyManager->getHotkeyMutex());
          // Restore to the original state before suspension
          for (const auto &state : suspendedConditionalHotkeyStates) {
            auto &ch = *hotkeyManager->activeConditionalHotkeys;
            auto it = std::find_if(ch.begin(), ch.end(),
                                   [state](const auto &ch_item) {
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
            Ungrab(hotkey.key, hotkey.modifiers,
                   DisplayManager::GetRootWindow());
          }
          hotkey.enabled = false;
        }
      }

      if (hotkeyManager) {
        // Track the original state of conditional hotkeys before suspension and
        // update their states
        hotkeyManager->conditionalHotkeysEnabled = false;
        hotkeyManager->setMode("suspend"); // Switch to suspend mode

        std::lock_guard<std::mutex> lock(hotkeyManager->getHotkeyMutex());
        suspendedConditionalHotkeyStates.clear();
        for (auto &ch : *hotkeyManager->activeConditionalHotkeys) {
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

// Method to resume all hotkeys (alias for Suspend() when already suspended)
bool IO::Resume() {
  return Suspend(); // Suspend() toggles, so call it again to resume
}

// Static method to exit the application
void IO::ExitApp() {
  debug("Static ExitApp called - initiating emergency shutdown sequence");

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
  std::vector<std::string> textModifiers = {"ctrl", "shift", "alt", "meta",
                                            "win"};
  bool foundTextModifier = false;

  do {
    foundTextModifier = false;

    for (const auto &mod : textModifiers) {
      if (i + mod.size() <= input.size() &&
          input.substr(i, mod.size()) == mod) {
        // Check if this is followed by '+' or end of string
        if (i + mod.size() == input.size() || input[i + mod.size()] == '+') {
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

// KeyTap method for on tap syntax
void IO::KeyTap(const std::string &key, std::function<void()> tapAction) {
  if (!hotkeyManager) {
    error("KeyTap: hotkeyManager not initialized");
    return;
  }

  // Create KeyTap with tap action only
  auto keyTap =
      std::make_unique<havel::KeyTap>(hotkeyManager, key, tapAction,
                                      "", // tap condition (empty = always)
                                      "", // combo condition (empty = always)
                                      nullptr, // combo action
                                      true,    // grab down
                                      true     // grab up
      );

  // Store in global storage to keep alive
  std::lock_guard<std::mutex> lock(g_keyTapMutex);
  g_keyTapStorage.push_back(std::move(keyTap));
}

// KeyCombo method for on combo syntax
void IO::KeyCombo(const std::string &key, std::function<void()> comboAction) {
  if (!hotkeyManager) {
    error("KeyCombo: hotkeyManager not initialized");
    return;
  }

  // Create KeyTap with combo action only
  auto keyTap =
      std::make_unique<havel::KeyTap>(hotkeyManager, key,
                                      nullptr, // tap action
                                      "",      // tap condition (empty = always)
                                      "", // combo condition (empty = always)
                                      comboAction, // combo action
                                      true,        // grab down
                                      true         // grab up
      );

  // Store in global storage to keep alive
  std::lock_guard<std::mutex> lock(g_keyTapMutex);
  g_keyTapStorage.push_back(std::move(keyTap));
}

HotKey IO::AddHotkey(const std::string &rawInput, std::function<void()> action,
                     int id) {
  auto wrapped_action = [action, rawInput]() {
    if (Configs::Get().GetVerboseKeyLogging())
      debug("Hotkey pressed: " + rawInput);
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
  // or mouse-specific direction patterns like
  // "mouseleft,mouseright,mouseup,mousedown"
  std::string keyLower = toLower(parsed.keyPart);
  bool isPredefinedGesture =
      (keyLower == "circle" || keyLower == "square" || keyLower == "triangle" ||
       keyLower == "zigzag" || keyLower == "check");

  // Check if it's a comma-separated direction pattern (may include mouse
  // directions)
  bool hasComma = parsed.keyPart.find(',') != std::string::npos;

  // Check if it's a single mouse direction (using the new mouse-specific names)
  bool isMouseDirection =
      (keyLower == "mouseleft" || keyLower == "mouseright" ||
       keyLower == "mouseup" || keyLower == "mousedown" ||
       keyLower == "mouseupleft" || keyLower == "mouseupright" ||
       keyLower == "mousedownleft" || keyLower == "mousedownright");

  // Check if it's a comma-separated pattern that looks like a gesture pattern
  bool isGesturePattern = false;
  if (hasComma) {
    // Parse the comma-separated values to see if they look like gesture
    // directions
    std::string tempPattern =
        parsed.keyPart; // Don't convert to lower yet, to check original
    std::istringstream iss(tempPattern);
    std::string part;
    bool allPartsAreMouseDirections = true;

    while (std::getline(iss, part, ',')) {
      // Remove leading/trailing whitespace
      part.erase(0, part.find_first_not_of(" \t\n\r"));
      part.erase(part.find_last_not_of(" \t\n\r") + 1);

      std::string lowerPart = toLower(part);

      // Check if this part is a mouse direction
      bool isMouseDir =
          (lowerPart == "mouseleft" || lowerPart == "mouseright" ||
           lowerPart == "mouseup" || lowerPart == "mousedown" ||
           lowerPart == "mouseupleft" || lowerPart == "mouseupright" ||
           lowerPart == "mousedownleft" || lowerPart == "mousedownright" ||
           // Corner directions without mouse prefix (for backward
           // compatibility)
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

  // Only treat as gesture if it's a predefined gesture, contains mouse-specific
  // directions in comma pattern, or is a single mouse direction This avoids
  // conflicting with regular arrow keys like "up", "down", "left", "right"
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
          std::string part = parsed.keyPart.substr(start, ampPos - start);
          // Trim whitespace
          size_t first = part.find_first_not_of(" \t");
          size_t last = part.find_last_not_of(" \t");
          if (first != std::string::npos) {
            parts.push_back(part.substr(first, last - first + 1));
          }
          start = ampPos + 1;
          ampPos = parsed.keyPart.find('&', start);
        }
        std::string lastPart = parsed.keyPart.substr(start);
        // Trim whitespace from last part
        size_t first = lastPart.find_first_not_of(" \t");
        size_t last = lastPart.find_last_not_of(" \t");
        if (first != std::string::npos) {
          parts.push_back(lastPart.substr(first, last - first + 1));
        }

        hotkey.type = HotkeyType::Combo;
        for (const auto &part : parts) {
          // Preserve the @evdev flag for combo parts
          // If the original hotkey had @, the parts should too
          std::string partWithPrefix = parsed.isEvdev ? "@" + part : part;
          auto subHotkey =
              AddHotkey(partWithPrefix, std::function<void()>{}, 0);
          hotkey.comboSequence.push_back(subHotkey);

          // Track specific physical keys for precise modifier matching
          // This ensures @RShift requires Right Shift specifically
          if (subHotkey.type == HotkeyType::Keyboard) {
            int keyCode = static_cast<int>(subHotkey.key);
            if (keyCode == KEY_LEFTCTRL || keyCode == KEY_RIGHTCTRL ||
                keyCode == KEY_LEFTSHIFT || keyCode == KEY_RIGHTSHIFT ||
                keyCode == KEY_LEFTALT || keyCode == KEY_RIGHTALT ||
                keyCode == KEY_LEFTMETA || keyCode == KEY_RIGHTMETA) {
              hotkey.requiredPhysicalKeys.push_back(keyCode);
            }
          }
        }
        hotkey.success = !hotkey.comboSequence.empty();
        // Mark this combo as requiring a wheel event if any part is a wheel
        hotkey.requiresWheel = std::any_of(
            hotkey.comboSequence.begin(), hotkey.comboSequence.end(),
            [](const HotKey &k) { return k.type == HotkeyType::MouseWheel; });
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

  // Add to maps if has action (skip for combo subs) - keyboard hotkey path
  if (hasAction) {
    std::lock_guard<std::timed_mutex> lock(hotkeyMutex);

    // Check for duplicate alias (case-insensitive) to prevent double
    // registration
    std::string normalizedAlias = toLower(rawInput);
    for (const auto &[existingId, existingHotkey] : hotkeys) {
      if (toLower(existingHotkey.alias) == normalizedAlias) {
        warn("Duplicate hotkey alias detected: '{}' (registered as '{}'). "
             "Skipping duplicate registration.",
             rawInput, existingHotkey.alias);
        return hotkey; // Return existing hotkey without re-registering
      }
    }

    if (id == 0)
      id = ++hotkeyCount;
    hotkey.id = id; // Set the hotkey's id
    hotkeys[id] = hotkey;
    if (hotkey.x11)
      x11Hotkeys.insert(id);
    if (hotkey.evdev)
      evdevHotkeys.insert(id);
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
      debug("Hotkey pressed: " + hotkeyStr +
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
  } else {
    // Check for combo
    size_t ampPos = parsed.keyPart.find('&');
    if (ampPos != std::string::npos) {
      std::vector<std::string> parts;
      size_t start = 0;
      while (ampPos != std::string::npos) {
        std::string part = parsed.keyPart.substr(start, ampPos - start);
        // Trim whitespace
        size_t first = part.find_first_not_of(" \t");
        size_t last = part.find_last_not_of(" \t");
        if (first != std::string::npos) {
          parts.push_back(part.substr(first, last - first + 1));
        }
        start = ampPos + 1;
        ampPos = parsed.keyPart.find('&', start);
      }
      std::string lastPart = parsed.keyPart.substr(start);
      // Trim whitespace from last part
      size_t first = lastPart.find_first_not_of(" \t");
      size_t last = lastPart.find_last_not_of(" \t");
      if (first != std::string::npos) {
        parts.push_back(lastPart.substr(first, last - first + 1));
      }

      hotkey.type = HotkeyType::Combo;
      for (const auto &part : parts) {
        // Use AddHotkey() instead of AddMouseHotkey() to properly handle
        // keyboard keys in combos (e.g., "RShift & WheelDown")
        // Preserve the @evdev flag for combo parts
        std::string partWithPrefix = parsed.isEvdev ? "@" + part : part;
        auto subHotkey = AddHotkey(partWithPrefix, std::function<void()>{}, 0);
        hotkey.comboSequence.push_back(subHotkey);

        // Track specific physical keys for precise modifier matching
        // This ensures @RShift requires Right Shift specifically
        if (subHotkey.type == HotkeyType::Keyboard) {
          int keyCode = static_cast<int>(subHotkey.key);
          if (keyCode == KEY_LEFTCTRL || keyCode == KEY_RIGHTCTRL ||
              keyCode == KEY_LEFTSHIFT || keyCode == KEY_RIGHTSHIFT ||
              keyCode == KEY_LEFTALT || keyCode == KEY_RIGHTALT ||
              keyCode == KEY_LEFTMETA || keyCode == KEY_RIGHTMETA) {
            hotkey.requiredPhysicalKeys.push_back(keyCode);
          }
        }
      }
      hotkey.success = !hotkey.comboSequence.empty();
      // Mark this combo as requiring a wheel event if any part is a wheel
      hotkey.requiresWheel = std::any_of(
          hotkey.comboSequence.begin(), hotkey.comboSequence.end(),
          [](const HotKey &k) { return k.type == HotkeyType::MouseWheel; });
    } else {
      // Not a mouse button/wheel, not a combo - try to parse as keyboard key
      KeyCode keycode = ParseKeyPart(parsed.keyPart, parsed.isEvdev);
      if (keycode != 0) {
        hotkey.type = HotkeyType::Keyboard;
        hotkey.key = static_cast<Key>(keycode);
        hotkey.success = true;
      } else {
        // If it's not a valid key, mouse button, wheel, or combo, mark as
        // failed
        hotkey.type = HotkeyType::Keyboard;
        hotkey.key = 0;
        hotkey.success = false;
      }
    }
  }

  // Add to maps if has action (skip for combo subs) - mouse hotkey path
  if (hasAction) {
    std::lock_guard<std::timed_mutex> lock(hotkeyMutex);

    // Check for duplicate alias (case-insensitive) to prevent double
    // registration
    std::string normalizedAlias = toLower(hotkeyStr);
    for (const auto &[existingId, existingHotkey] : hotkeys) {
      if (toLower(existingHotkey.alias) == normalizedAlias) {
        warn("Duplicate mouse hotkey alias detected: '{}' (registered as "
             "'{}'). Skipping duplicate registration.",
             hotkeyStr, existingHotkey.alias);
        return hotkey; // Return existing hotkey without re-registering
      }
    }

    if (id == 0)
      id = ++hotkeyCount;
    hotkey.id = id; // Set the hotkey's id
    hotkeys[id] = hotkey;
    if (hotkey.x11)
      x11Hotkeys.insert(id);
    if (hotkey.evdev)
      evdevHotkeys.insert(id);
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

// Send text using clipboard + paste (more reliable than key events for complex text)
// BACKS UP AND RESTORES clipboard to avoid destroying user data
void IO::SendText(const std::string &text) {
  if (text.empty()) {
    return;
  }

#ifdef WINDOWS
  // Windows: Backup clipboard, set text, paste, restore
  if (OpenClipboard(nullptr)) {
    // Backup old clipboard
    HANDLE hOldClip = GetClipboardData(CF_TEXT);
    std::string oldText;
    if (hOldClip) {
      char* pOld = static_cast<char*>(GlobalLock(hOldClip));
      if (pOld) {
        oldText = pOld;
        GlobalUnlock(hOldClip);
      }
    }
    
    // Set new text
    EmptyClipboard();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hGlobal) {
      char* pBuffer = static_cast<char*>(GlobalLock(hGlobal));
      if (pBuffer) {
        strcpy(pBuffer, text.c_str());
        GlobalUnlock(hGlobal);
        SetClipboardData(CF_TEXT, hGlobal);
      }
    }
    
    // Send Ctrl+V
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('V', 0, 0, 0);
    keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    
    // Restore old clipboard
    if (!oldText.empty()) {
      EmptyClipboard();
      hGlobal = GlobalAlloc(GMEM_MOVEABLE, oldText.size() + 1);
      if (hGlobal) {
        char* pBuffer = static_cast<char*>(GlobalLock(hGlobal));
        if (pBuffer) {
          strcpy(pBuffer, oldText.c_str());
          GlobalUnlock(hGlobal);
          SetClipboardData(CF_TEXT, hGlobal);
        }
      }
    }
    
    CloseClipboard();
  }
#else
  // Linux: Use ClipboardManager if available (backup/restore)
  // Note: This is called from bridge layer with clipboardManager context
  // Fallback to key events for now
  Send(text.c_str());
#endif
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

  // Use KeyMap for X11 lookup
  unsigned long code = KeyMap::ToX11(keyName);
  if (code != 0) {
    return code;
  }

  // Fallback for single character
  if (keyName.length() == 1) {
    return XStringToKeysym(keyName.c_str());
  }

  return 0;
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

  // Fallback for single character a-z and 0-9
  if (keyName.length() == 1) {
    char c = keyName[0];
    if (c >= 'a' && c <= 'z')
      return KEY_A + (c - 'a');
    if (c >= '0' && c <= '9')
      return (c == '0') ? KEY_0 : KEY_1 + (c - '1');
  }

  return 0;
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

  x11::Window root = DefaultRootWindow(display);

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
    display = havel::DisplayManager::GetDisplay();
    if (!display) {
      error("Unable to open X display!");
      return EXIT_FAILURE;
    }
  }

  x11::Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                           0, 0, 1, 1, 0, 0, 0);
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
  if (eventListener) {
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
  if (eventListener) {
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
  x11::Window root = DefaultRootWindow(display);
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
  debug("Ungrabbing hotkey: {}", hotkey.alias);

  if (hotkey.key == 0) {
    error("Invalid keycode for hotkey: {}", hotkey.alias);
    return false;
  }

  // Only ungrab X11 hotkeys (evdev hotkeys don't need ungrabbing)
  if (!hotkey.evdev && display) {
    x11::Window root = DefaultRootWindow(display);

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
      debug("Physically ungrabbed key {} with modifiers 0x{:x}", hotkey.key,
           hotkey.modifiers);
    } else {
      debug("Not ungrabbing - other hotkeys still using key {} with modifiers "
           "0x{:x}",
           hotkey.key, hotkey.modifiers);
    }
  }

  // Always disable the hotkey entry
  hotkeys[hotkeyId].enabled = false;

  debug("Successfully ungrabbed hotkey: {}", hotkey.alias);
  return true;
#else
  return false;
#endif
}

void IO::SetAnyKeyPressCallback(AnyKeyPressCallback callback) {
  debug("IO::SetAnyKeyPressCallback called, eventListener={}",
        (void *)eventListener.get());
  if (!eventListener) {
    warn("IO::SetAnyKeyPressCallback: eventListener is null - callback will "
         "not be registered");
    warn("  This usually means no input devices were found or EventListener "
         "failed to start");
    warn("  Check if /dev/input/event* devices are accessible and grabDevices "
         "is enabled");
    return;
  }
  try {
    // Double-check that eventListener is valid
    debug("Calling eventListener->SetAnyKeyPressCallback");
    eventListener->SetAnyKeyPressCallback(callback);
    debug("SetAnyKeyPressCallback completed successfully");
  } catch (const std::exception &e) {
    error("IO::SetAnyKeyPressCallback failed: {}", e.what());
  } catch (...) {
    error("IO::SetAnyKeyPressCallback failed with unknown exception");
  }
}

void IO::SetInputEventCallback(InputEventCallback callback) {
  if (!eventListener) {
    warn("IO::SetInputEventCallback: eventListener is null - callback will not "
         "be registered");
    return;
  }
  try {
    eventListener->SetInputEventCallback(std::move(callback));
  } catch (const std::exception &e) {
    error("IO::SetInputEventCallback failed: {}", e.what());
  } catch (...) {
    error("IO::SetInputEventCallback failed with unknown exception");
  }
}

void IO::SetInputBlockCallback(
    std::function<bool(const InputEvent &)> callback) {
  if (!eventListener) {
    warn("IO::SetInputBlockCallback: eventListener is null - callback will not "
         "be registered");
    return;
  }
  try {
    eventListener->SetInputBlockCallback(std::move(callback));
  } catch (const std::exception &e) {
    error("IO::SetInputBlockCallback failed: {}", e.what());
  } catch (...) {
    error("IO::SetInputBlockCallback failed with unknown exception");
  }
}

HotkeyExecutor *IO::GetHotkeyExecutor() const { return hotkeyExecutor.get(); }

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

bool IO::RemoveHotkey(const std::string &keyName) {
  std::lock_guard<std::mutex> lock(hotkeySetMutex);
  bool found = false;

  // Find the hotkey by name and remove it
  for (auto it = hotkeys.begin(); it != hotkeys.end();) {
    if (it->second.alias == keyName) {
      int id = it->first;

      // Ungrab if currently grabbed
      if (it->second.grab && !it->second.evdev) {
        UngrabHotkey(id);
      }

      it = hotkeys.erase(it);
      found = true;
    } else {
      ++it;
    }
  }

  return found;
}

bool IO::RemoveHotkey(int hotkeyId) {
  std::lock_guard<std::mutex> lock(hotkeySetMutex);

  auto it = hotkeys.find(hotkeyId);
  if (it == hotkeys.end()) {
    return false;
  }

  // Ungrab if currently grabbed
  if (it->second.grab && !it->second.evdev) {
    UngrabHotkey(hotkeyId);
  }

  hotkeys.erase(it);
  return true;
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
    if (eventListener) {
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

std::pair<int, int> IO::GetMousePositionX11() {
  Display *display = DisplayManager::GetDisplay();
  if (!display) {
    error("GetMousePositionX11: Display is null");
    return {0, 0};
  }

  // Prefer XInput2 if available
  if (xinput2Available && xinput2DeviceId != -1) {
    ::Window root_return, child_return;
    double root_x, root_y, win_x, win_y;
    XIButtonState buttons;
    XIModifierState mods;
    XIGroupState group;
    if (XIQueryPointer(display, xinput2DeviceId, DefaultRootWindow(display),
                       &root_return, &child_return, &root_x, &root_y, &win_x,
                       &win_y, &buttons, &mods, &group) == x11::XTrue) {
      return {(int)root_x, (int)root_y};
    }
  }

  // Fallback to legacy XQueryPointer
  ::Window root = DefaultRootWindow(display);
  ::Window root_return, child_return;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;

  if (XQueryPointer(display, root, &root_return, &child_return, &root_x,
                    &root_y, &win_x, &win_y, &mask)) {
    return {root_x, root_y};
  } else {
    error("GetMousePositionX11: XQueryPointer failed");
    return {0, 0};
  }
}

void havel::IO::setGlobalAltState(bool pressed) {
  globalAltPressed.store(pressed);
}

bool havel::IO::getGlobalAltState() { return globalAltPressed.load(); }
void IO::executeComboAction(const std::string &action) {
  debug("Executing combo action: {}", action);

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
        debug("Executing hotkey '{}' for action '{}'", hotkey.alias, action);
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
// (removed - now inline in header)

// Mouse button code conversion implementations
int IO::GetMouseButtonCode(const std::string &arg) {
  std::string s = toLower(arg);
  if (s == "right")
    return BTN_RIGHT;
  if (s == "middle")
    return BTN_MIDDLE;
  if (s == "side1" || s == "xbutton1")
    return BTN_SIDE;
  if (s == "side2" || s == "xbutton2")
    return BTN_EXTRA;
  if (s == "side3" || s == "forward")
    return BTN_FORWARD;
  if (s == "side4" || s == "back")
    return BTN_BACK;
  if (s == "left")
    return BTN_LEFT;
  try {
    if (std::stoi(s) > 0) {
      return GetMouseButtonCode(std::stoi(s));
    }
  } catch (...) {
    // not a number, continue
  }
  return BTN_LEFT; // fallback
}

int IO::GetMouseButtonCode(int idx) {
  switch (idx) {
  case 1:
    return BTN_LEFT;
  case 2:
    return BTN_RIGHT;
  case 3:
    return BTN_MIDDLE;
  case 4:
    return BTN_SIDE;
  case 5:
    return BTN_EXTRA;
  case 6:
    return BTN_FORWARD;
  case 7:
    return BTN_BACK;
  default:
    return BTN_LEFT;
  }
}

MouseAction IO::GetMouseAction(const std::string &action) {
  std::string s = toLower(action);
  if (s == "press" || s == "hold")
    return MouseAction::Hold;
  if (s == "release")
    return MouseAction::Release;
  if (s == "click")
    return MouseAction::Click;
  try {
    if (std::stoi(s) > 0) {
      return GetMouseAction(std::stoi(s));
    }
  } catch (...) {
    // not a number, continue
  }
  return MouseAction::Click; // fallback
}

MouseAction IO::GetMouseAction(int idx) {
  switch (idx) {
  case 0:
    return MouseAction::Hold;
  case 1:
    return MouseAction::Release;
  case 2:
    return MouseAction::Click;
  default:
    return MouseAction::Click;
  }
}

} // namespace havel

// Additional methods for HostAPI
pID havel::IO::GetActiveWindowPID() {
  return WindowManager::GetActiveWindowPID();
}

void havel::IO::Scroll(int dy, int dx) {
  if (mouseController) {
    mouseController->Scroll(dy, dx);
  }
}

void havel::IO::SendKeyEvent(Key key, bool down) {
  // Simple implementation using XTest extension
  if (display) {
    XTestFakeKeyEvent(display, key, down ? true : false, CurrentTime);
    XFlush(display);
  }
}
