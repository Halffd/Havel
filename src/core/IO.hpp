#pragma once
#include "core/CallbackTypes.hpp"     // Include callback types
#include "core/MouseGestureTypes.hpp" // Include the mouse gesture types
#include "core/io/Device.hpp"
#include "core/io/HotkeyExecutor.hpp"
#include "core/io/KeyMap.hpp"
#include "types.hpp"
#include "x11.h"
#include <X11/extensions/XInput2.h>
#include <atomic>
#include <condition_variable>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#define CLEANMASK(mask)                                                        \
  (mask & ~(numlockmask | LockMask) &                                          \
   (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask |      \
    Mod5Mask))

namespace havel {

// Forward declarations
class EventListener; // Add forward declaration for EventListener
class HotkeyManager;

enum class MouseButton {
  Left = BTN_LEFT,
  Right = BTN_RIGHT,
  Middle = BTN_MIDDLE,
  Side1 = BTN_SIDE,
  Side2 = BTN_EXTRA,
  Side3 = BTN_FORWARD,
  Side4 = BTN_BACK
};

enum class MouseAction { Hold = 1, Release = 0, Click = 2 };

enum class HotkeyEventType { Both, Down, Up };

enum class HotkeyType {
  Keyboard,
  MouseButton,
  MouseWheel,
  MouseMove,
  MouseGesture, // New type for mouse gestures
  Combo
};

struct HotKey {
  int id = 0; // Unique identifier for this hotkey
  std::string alias;
  Key key;
  int modifiers;
  std::function<void()> callback;
  std::string action;
  std::vector<std::function<bool()>> contexts;
  bool enabled = true;
  bool grab = false;
  bool suspend = false;
  bool success = false;
  bool evdev = false;
  bool x11 = false;
  bool repeat = true;
  bool wildcard = false;
  HotkeyType type = HotkeyType::Keyboard;
  HotkeyEventType eventType = HotkeyEventType::Down;

  // For mouse buttons
  int mouseButton = 0;
  // For wheel events: 1 = up, -1 = down
  int wheelDirection = 0;
  // For combos
  std::vector<HotKey> comboSequence;
  // Time window for combo in milliseconds
  int comboTimeWindow = 500;
  // Whether this combo requires a wheel event to trigger
  bool requiresWheel = false;
  // Specific physical keys required by this combo (for precise modifier
  // matching)
  std::vector<int> requiredPhysicalKeys;

  // For mouse gestures
  MouseGesture gestureConfig;
  std::string gesturePattern; // Pattern string (e.g., "up,down,left,right" or
                              // predefined names like "circle")

  // Repeat interval in milliseconds (0 = use default key repeat)
  int repeatInterval = 0;
  std::chrono::steady_clock::time_point lastTriggerTime;
};
struct ParsedHotkey {
  std::string keyPart;
  int modifiers = 0;
  bool isEvdev = false;
  bool isX11 = false;
  bool grab = true;
  bool suspend = false;
  bool repeat = true;
  bool wildcard = false;
  HotkeyEventType eventType = HotkeyEventType::Down;
  int repeatInterval = 0; // Custom repeat interval in milliseconds
};

struct InputDevice {
  int id;
  std::string name;
  std::string type;
  bool enabled;
  std::string evdevPath;
};
// Helper for parsing event type from string
inline HotkeyEventType ParseHotkeyEventType(const std::string &str) {
  if (str == "down")
    return HotkeyEventType::Down;
  if (str == "up")
    return HotkeyEventType::Up;
  return HotkeyEventType::Both;
};

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

// Event batching structures for optimization
struct KeyToken {
  enum Type {
    Modifier,     // ^, +, !, #, etc.
    Key,          // Regular key or {key_name}
    ModifierDown, // {ctrl down}, {shift up}, etc.
    ModifierUp,
    Special // {emergency_release}, {panic}
  } type;
  std::string value;
  bool down = true;
};

// Helper to create input_event structs efficiently
inline input_event MakeEvent(uint16_t type, uint16_t code, int32_t value) {
  struct input_event ev{};
  gettimeofday(&ev.time, nullptr);
  ev.type = type;
  ev.code = code;
  ev.value = value;
  return ev;
}

struct IoEvent {
  Key key;
  int modifiers;
  bool isDown;
};
class GrabException : public std::exception {
public:
  const char *what() const noexcept override { return "Failed to grab hotkey"; }
};
class EvdevException : public std::exception {
public:
  const char *what() const noexcept override {
    return "Failed to initialize evdev";
  }
};
class IO {
  std::thread evdevThread;
  std::atomic<bool> evdevRunning{false};
  std::atomic<bool> mouseEvdevRunning{false};
  std::string evdevDevicePath;
  std::string mouseEvdevDevicePath;
  int mouseDeviceFd = -1; // Track the mouse device file descriptor
  std::thread mouseEvdevThread;
  int mouseUinputFd = -1;
  std::atomic<bool> globalAltPressed{false};
  std::chrono::steady_clock::time_point lastLeftPress;
  std::chrono::steady_clock::time_point lastRightPress;
  // Unified modifier state (evdev-sourced)
  std::atomic<int> currentEvdevModifiers{0};
  ModifierState currentModifierState{};
  // Active input tracking for combos (keycode or button code -> down time)
  std::mutex activeInputsMutex;
  std::unordered_map<int, std::chrono::steady_clock::time_point> activeInputs;
  // Mouse button states
  std::atomic<bool> leftButtonDown{false};
  std::atomic<bool> rightButtonDown{false};
  std::string emergencyHotkey = "^!esc";
  // Deadlock protection
  int evdevShutdownFd = -1; // eventfd for clean shutdown
  std::atomic<bool> callbackInProgress{false};
  std::chrono::steady_clock::time_point lastCallbackStart;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::atomic<int> pendingCallbacks{0};
  static constexpr int CALLBACK_TIMEOUT_MS =
      5000; // 5 second timeout for callbacks
  static constexpr int WATCHDOG_INTERVAL_MS = 1000; // Check every second

  // Track active callback threads for proper cleanup
  std::vector<std::shared_ptr<std::thread>> activeCallbackThreads;

  // New unified event listener
  std::unique_ptr<EventListener> eventListener;
  bool useNewEventListener = false;
  std::shared_ptr<HotkeyManager> hotkeyManager = nullptr;

public:
  static std::unordered_map<int, HotKey> hotkeys;
  bool isSuspended = false;
  static bool globalEvdev;
  std::vector<HotKey> failedHotkeys;
  std::set<int> x11Hotkeys;
  std::set<int> evdevHotkeys;
  IO();
  ~IO();

  // Public access methods for EventListener
  EventListener *GetEventListener() { return eventListener.get(); }
  bool IsUsingNewEventListener() const { return useNewEventListener; }
  std::vector<std::string> GetInputDevices(); // We'll implement this method
  void setHotkeyManager(std::shared_ptr<HotkeyManager> hotkeyManager) {
    this->hotkeyManager = hotkeyManager;
  }

  // Key sending methods
  void Send(Key key, bool down = true);

  void Send(cstr keys);
  void SendUInput(int keycode, bool down);
  void SendSpecific(const std::string &keys);

  void ControlSend(const std::string &control, const std::string &keys);

  void ProcessKeyCombination(const std::string &keys);

  void SendX11Key(const std::string &keyName, bool press);

  // Hotkey methods
  bool ContextActive(std::vector<std::function<bool()>> contexts);
  bool EnableHotkey(const std::string &keyName);
  bool DisableHotkey(const std::string &keyName);
  bool ToggleHotkey(const std::string &keyName);
  bool RemoveHotkey(const std::string &keyName); // Remove by name
  bool RemoveHotkey(int hotkeyId);               // Remove by ID
  bool AddHotkey(const std::string &alias, Key key, int modifiers,
                 std::function<void()> callback);
  HotKey AddMouseHotkey(const std::string &hotkeyStr,
                        std::function<void()> action, int id = 0);

  HotKey AddHotkey(const std::string &rawInput, std::function<void()> action,
                   int id = 0);

  bool Hotkey(const std::string &hotkeyStr, std::function<void()> action,
              const std::string &condition = "", int id = 0);
  bool Suspend();
  bool Suspend(int id);

  // Public cleanup method for safe shutdown
  void cleanup();

  bool Resume(int id);

  // Static method for application exit
  static void ExitApp();

  // Mouse methods
  bool MouseMove(int dx, int dy, int speed = 1, float accel = 1.0f);
  bool MouseMoveTo(int targetX, int targetY, int speed = 1, float accel = 1.0f);

  // XInput2 hardware mouse control
  bool InitializeXInput2();
  bool SetHardwareMouseSensitivity(double sensitivity);

  // Mouse event handling
  bool StartEvdevMouseListener(const std::string &mouseDevicePath);
  void StopEvdevMouseListener();
  bool handleMouseButton(const input_event &ev);
  bool handleMouseRelative(const input_event &ev);
  bool handleMouseAbsolute(const input_event &ev);
  bool SetupMouseUinputDevice();
  void SendMouseUInput(const input_event &ev);
  void setGlobalAltState(bool pressed);
  bool getGlobalAltState();
  void executeComboAction(const std::string &action);
  void CleanupUinputDevice();
  // Access current modifier bitmask (ShiftMask|ControlMask|Mod1Mask|Mod4Mask)
  int GetCurrentModifiers() const {
    if (currentModifierState.leftCtrl || currentModifierState.rightCtrl)
      return ControlMask;
    if (currentModifierState.leftShift || currentModifierState.rightShift)
      return ShiftMask;
    if (currentModifierState.leftAlt || currentModifierState.rightAlt)
      return Mod1Mask;
    if (currentModifierState.leftMeta || currentModifierState.rightMeta)
      return Mod4Mask;
    return 0;
  }
  const ModifierState &GetModifierState() const { return currentModifierState; }

  // Mouse sensitivity control (1.0 is default, lower values decrease
  // sensitivity, higher values increase it)
  void SetMouseSensitivity(double sensitivity);
  double GetMouseSensitivity() const;

  // Mouse scroll speed control (1.0 is default, lower values decrease speed,
  // higher values increase it)
  void SetScrollSpeed(double speed);
  double GetScrollSpeed() const;

  // Enhanced mouse movement with custom sensitivity
  bool MouseMoveSensitive(int dx, int dy, int baseSpeed = 5,
                          float accel = 1.5f);

  void MouseClick(int button);
  void MouseDown(int button);
  void MouseUp(int button);
  void MouseWheel(int amount);

  // State methods
  bool GetKeyState(const std::string &keyName);
  bool GetKeyState(int keycode); // For raw keycodes
  bool IsAnyKeyPressed();
  bool IsAnyKeyPressedExcept(const std::string &excludeKey);
  bool IsAnyKeyPressedExcept(const std::vector<std::string> &excludeKeys);
  bool IsKeyPressed(const std::string &keyName) { return GetKeyState(keyName); }

  // Modifier state helpers
  bool IsShiftPressed();
  bool IsCtrlPressed();
  bool IsAltPressed();
  bool IsWinPressed();

  // Convenience overloads for MouseButton enum
  void MouseClick(MouseButton button) { Click(button, MouseAction::Click); }
  void MouseDown(MouseButton button) { Click(button, MouseAction::Hold); }
  void MouseUp(MouseButton button) { Click(button, MouseAction::Release); }

  static void PressKey(const std::string &keyName, bool press);

  // Utility methods

  void MsgBox(const std::string &message);

  int GetMouse();

  int GetKeyboard();

  static ParsedHotkey ParseModifiersAndFlags(const std::string &input,
                                             bool isEvdev);
  static KeyCode ParseKeyPart(const std::string &keyPart, bool isEvdev);
  static ParsedHotkey ParseHotkeyString(const std::string &rawInput);
  static int ParseModifiers(std::string str);
  static int ParseMouseButton(const std::string &str);

  void AssignHotkey(HotKey hotkey, int id);

  // Add new methods for dynamic hotkey grabbing/ungrabbing
  bool GrabHotkey(int hotkeyId);

  bool UngrabHotkey(int hotkeyId);

  // Method to register callback for any key press
  void SetAnyKeyPressCallback(AnyKeyPressCallback callback);

  bool GrabHotkeysByPrefix(const std::string &prefix);

  bool UngrabHotkeysByPrefix(const std::string &prefix);

  // Key mapping
  void Map(const std::string &from, const std::string &to);
  void Remap(const std::string &key1, const std::string &key2);

  // Static methods
  static void removeSpecialCharacters(std::string &keyName);

  static Key StringToButton(const std::string &buttonNameRaw);

  static Key StringToVirtualKey(std::string keyName);

  // Old hotkey system removed - use EventListener and KeyMap instead

  // Public method for emergency cleanup (signals)
  void UngrabAll();

  template <typename T, typename S> bool Click(T button, S action) {
    int btnCode;

    if constexpr (std::is_same_v<T, int>) {
      btnCode = button;
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (button == "left")
        btnCode = BTN_LEFT;
      else if (button == "right")
        btnCode = BTN_RIGHT;
      else if (button == "middle")
        btnCode = BTN_MIDDLE;
      else if (button == "side1")
        btnCode = BTN_SIDE;
      else if (button == "side2")
        btnCode = BTN_EXTRA;
      else if (button == "side3")
        btnCode = BTN_FORWARD;
      else if (button == "side4")
        btnCode = BTN_BACK;
      else if (button == "back")
        btnCode = BTN_BACK;
      else {
        std::cerr << "Unknown button string: " << button << "\n";
        return false;
      }
    } else if constexpr (std::is_enum_v<T>) {
      btnCode = static_cast<int>(button);
    } else {
      static_assert(always_false<T>, "Unsupported type for button");
    }

    if constexpr (std::is_same_v<S, int>) {
      return EmitClick(btnCode, S(action));
    } else if constexpr (std::is_enum_v<S>) {
      return EmitClick(btnCode, static_cast<int>(action));
    } else {
      static_assert(always_false<S>, "Unsupported type for action");
    }
  }

  template <typename T>
  bool MouseClick(T btnCode, int dx, int dy, int speed, float accel) {
    if (!MouseMove(dx, dy, speed, accel))
      return false;
    return EmitClick(static_cast<int>(btnCode), 2);
  }
  bool Scroll(double dy, double dx = 0);

  void EmergencyReleaseAllKeys();
  bool TryPressKey(int keycode);
  bool TryReleaseKey(int keycode);
  static Key GetKeyCode(cstr keyName);
  static double mouseSensitivity;
  static double scrollSpeed;
  bool shutdown = false;

  // X11 specific functions
  std::pair<int, int> GetMousePositionX11();

  // Performance optimization: Keycode caching and batch event helpers
  static int GetKeyCacheLookup(const std::string &keyName);
  static std::vector<KeyToken> ParseKeyString(const std::string &keys);
  void SendBatchedKeyEvents(const std::vector<input_event> &events);

private:
  Display *display;
  int uinputFd;
  std::mutex emergencyMutex;
  std::set<int> pressedKeys;
  std::mutex keyStateMutex;
  std::mutex hotkeySetMutex;

  template <typename T> static constexpr bool always_false = false;
  unsigned int numlockmask = 0;

  std::set<int> grabbedKeys; // Keys that should be blocked
  std::mutex grabbedKeysMutex;
  bool blockAllInput = false; // Emergency block all input
  static int XErrorHandler(Display *dpy, XErrorEvent *ee);

  // Track the original state of conditional hotkeys during suspend
  struct ConditionalHotkeyState {
    int id;
    bool wasGrabbed;
  };
  std::vector<ConditionalHotkeyState> suspendedConditionalHotkeyStates;
  bool wasSuspended = false;

  void UpdateNumLockMask();
  bool ModifierMatch(unsigned int expected, unsigned int actual);

public:
  bool EmitClick(int btnCode, int action);

  bool SetupUinputDevice();
  // X11 hotkey monitoring removed - use EventListener instead

  static Key EvdevNameToKeyCode(std::string keyName);

  bool MatchEvdevModifiers(int expectedModifiers,
                           const std::map<int, bool> &keyState);
  // Platform specific implementations
  std::map<std::string, Key> keyMap;
  std::map<int, bool> evdevKeyState;
  // Track mouse button state for combos
  std::map<int, bool> evdevMouseButtonState;
  std::unique_ptr<HotkeyExecutor> hotkeyExecutor;
  std::map<std::string, HotKey> instanceHotkeys;
  // Renamed to avoid conflict
  std::map<std::string, bool> hotkeyStates;
  std::thread timerThread;
  bool timerRunning = false;
  std::set<int> blockedKeys;
  std::mutex x11Mutex;

  // Static members
  static bool hotkeyEnabled;
  static int hotkeyCount;

  static std::atomic<int> syntheticEventsExpected;
  std::timed_mutex hotkeyMutex; // Use timed_mutex for try_lock_for() support
  std::mutex blockedKeysMutex;
  std::map<int, bool> keyDownState;

  // Remap tracking (persistent across events)
  std::mutex remapMutex;
  std::unordered_map<int, int> activeRemaps; // original -> mapped

  // Mouse control members
  mutable std::mutex mouseMutex;

  // XInput2 device ID for the pointer device
  int xinput2DeviceId = -1;
  bool xinput2Available = false;

  // Key mapping and sending utilities
  void InitKeyMap();
  std::unordered_map<KeySym, KeySym> keyMapInternal;
  std::unordered_map<KeySym, KeySym> remappedKeys;
  bool IsKeyRemappedTo(int targetKey);
  // Evdev key mapping
  std::unordered_map<int, int> evdevKeyMap; // Maps from scancode to scancode
  std::unordered_map<int, int> evdevRemappedKeys; // Bidirectional remapping

  void SendKeyEvent(Key key, bool down);

  static std::vector<IoEvent> ParseKeysString(const std::string &keys);

  // Helper methods for X11 key grabbing
  bool Grab(Key input, unsigned int modifiers, Window root, bool grab,
            bool isMouse = false);

  void Ungrab(Key input, unsigned int modifiers, Window root,
              bool isMouse = false);
  bool GrabKeyboard();
  bool FastGrab(Key input, unsigned int modifiers, Window root);
  bool GrabAllHotkeys();
  std::string findEvdevDevice(const std::string &deviceName);
  std::vector<InputDevice> getInputDevices();
  void listInputDevices();

  std::string getGamepadDevice();
  void StartEvdevGamepadListener(const std::string &devicePath);
  void StopEvdevGamepadListener();
  // Combo evaluation
  bool EvaluateCombo(const HotKey &combo);

  // Device detection helpers
  std::string detectEvdevDevice(
      const std::vector<std::string> &patterns,
      const std::function<bool(const std::string &, int)> &deviceFilter);
  std::string getKeyboardDevice();
  std::string getMouseDevice();
};
} // namespace havel
