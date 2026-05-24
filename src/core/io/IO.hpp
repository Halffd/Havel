#pragma once
#include "core/CallbackTypes.hpp"
#include "core/MouseGestureTypes.hpp"
#include "Device.hpp"
#include "HotkeyExecutor.hpp"
#include "IOBackend.hpp"
#include "KeyMap.hpp"
#include "MouseController.hpp"
#include "InputBackend.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "havel-lang/runtime/ImportManager.hpp"
#include "types.hpp"
#include "x11.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

// Forward declarations
class EventListener; // Add forward declaration for EventListener
class KeyTap;        // Forward declaration for KeyTap
struct HavelValue;   // Forward declaration for HavelValue
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

enum class HotkeyEventType { Both, Down, Up };

// ExecutorMode is defined in io/HotkeyExecutor.hpp

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
  // matching) - mutable because it's populated during combo evaluation
  mutable std::vector<int> requiredPhysicalKeys;

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
    // IOBackend adapter for platform-specific output (XTest, keybd_event, etc.)
    std::unique_ptr<IOBackend> ioBackend;

    // Unified event listener (input processing, uinput forwarding)
    std::unique_ptr<EventListener> eventListener;
    std::shared_ptr<HotkeyManager> hotkeyManager = nullptr;
    std::shared_ptr<ImportManager> importManager = nullptr;

    // Mouse controller for mouse operations
    std::unique_ptr<MouseController> mouseController;

    // Optional InputBackend (input device enumeration/adapter)
    std::unique_ptr<InputBackend> inputBackend;
    InputBackendType inputBackendType = InputBackendType::Unknown;

    ModifierState currentModifierState{};

public:
    bool isSuspended = false;
    static bool globalEvdev;
    std::vector<HotKey> failedHotkeys;
    std::set<int> x11Hotkeys;
    std::set<int> evdevHotkeys;
    IO();
    ~IO();

    // Public access methods for EventListener
    EventListener *GetEventListener() { return eventListener.get(); }
    ImportManager *GetImportManager() { return importManager.get(); }
    std::shared_ptr<ImportManager> getImportManagerShared() {
        return importManager;
    }

 // InputBackend access (new adapter pattern)
  InputBackend *GetInputBackend() { return inputBackend.get(); }
  InputBackendType GetInputBackendType() const { return inputBackendType; }
  void SetInputBackend(const std::string &backendName);

    // Backend device management
    std::vector<std::string> ListDevices();
    bool AddDevice(const std::string &path);
    bool RemoveDevice(const std::string &path);
    void ClearDevices();
    DeviceInfo GetDevice(const std::string &path);
    std::vector<DeviceInfo> GetDevices();

    // Evdev grab control
    bool SetEvdevGrab(bool grab);
    bool GetEvdevGrab() const;
    bool ToggleEvdevGrab();

    // Key repeat settings
    void SetRepeatInterval(int ms);
    int GetRepeatInterval() const;
    void SetAutoRepeat(bool enabled);
    bool GetAutoRepeat() const;

    std::vector<std::string> GetInputDevices(); // We'll implement this method
    void setHotkeyManager(std::shared_ptr<HotkeyManager> hotkeyManager) {
        this->hotkeyManager = hotkeyManager;
    }

  // Window information methods
  std::string GetActiveWindowTitle();
  std::string GetActiveWindowClass();
  pID GetActiveWindowPID();
  std::string GetActiveWindowProcess();

  // IOBackend access
  IOBackend *GetIOBackend() { return ioBackend.get(); }

  // Key sending methods
  void Send(Key key, bool down = true);

  void Send(cstr keys);
  void SendUInput(int keycode, bool down);
  void SendSpecific(const std::string &keys);

  // Text input - uses clipboard + paste (more reliable than key events)
  // BACKS UP AND RESTORES clipboard to avoid destroying user data
  void SendText(const std::string &text);

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

  // KeyTap methods for on tap/combo syntax
  void KeyTap(const std::string &key, std::function<void()> tapAction);
  void KeyCombo(const std::string &key, std::function<void()> comboAction);

  HotKey AddHotkey(const std::string &rawInput, std::function<void()> action,
                   int id = 0);

  bool Hotkey(const std::string &hotkeyStr, std::function<void()> action,
              const std::string &condition = "", int id = 0);
  bool Suspend();
  bool Suspend(int id);
  bool Resume();
  bool Resume(int id);
  bool IsSuspended() const { return isSuspended; }

  // Public cleanup method for safe shutdown
  void cleanup();

  // Static method for application exit
  static void ExitApp();

    // Mouse methods
    bool MouseMove(int dx, int dy, int speed = 1, float accel = 1.0f);
    bool MouseMoveTo(int targetX, int targetY, int speed = 1, float accel = 1.0f);
    void Scroll(int dy, int dx);
    bool ClickAt(int x, int y, int button = 1, int speed = 1, float accel = 1.0f);
    std::pair<int, int> GetMousePosition();

    // Mouse gesture methods
    void AddGesture(int id, const std::string &pattern);
    void AddGesture(int id, const std::vector<MouseGestureDirection> &directions);
    void RemoveGesture(int id);
    std::vector<int> GetGestures() const;
    bool HasGestures() const;
    void ClearGestures();

  // XInput2 hardware mouse control
  bool InitializeXInput2();
  bool SetHardwareMouseSensitivity(double sensitivity);

  void setGlobalAltState(bool pressed);
  bool getGlobalAltState();
  void executeComboAction(const std::string &action);
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
  void MouseClick(int button) { Click(button, MouseAction::Click); }
  bool MouseDown(MouseButton button) {
    return Click(button, MouseAction::Hold);
  }
  bool MouseDown(int button) { return Click(button, MouseAction::Hold); }
  bool MouseUp(MouseButton button) {
    return Click(button, MouseAction::Release);
  }
  bool MouseUp(int button) { return Click(button, MouseAction::Release); }

  void PressKey(const std::string &keyName, bool press);

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
  void SetInputEventCallback(InputEventCallback callback);
  void SetInputBlockCallback(std::function<bool(const InputEvent &)> callback);
    HotkeyExecutor *GetHotkeyExecutor() const;
    ExecutorMode GetExecutorMode() const;
    void SetExecutorMode(ExecutorMode mode);

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

  // Mouse button code conversion - multiple overloads for different types
  int GetMouseButtonCode(const std::string& arg);
  int GetMouseButtonCode(MouseButton btn) { return static_cast<int>(btn); }
  int GetMouseButtonCode(int idx);

  MouseAction GetMouseAction(const std::string& action);
  MouseAction GetMouseAction(MouseAction action) { return action; }
  MouseAction GetMouseAction(int idx);
  template <typename T, typename S> bool Click(T button, S action) {
    int btnCode = GetMouseButtonCode(button);
    MouseAction mouseAction = GetMouseAction(action);
    if constexpr (std::is_same_v<S, int>) {
      return EmitClick(btnCode, GetMouseAction(S(action)));
    } else if constexpr (std::is_enum_v<S>) {
      return EmitClick(btnCode, mouseAction);
    } else {
      static_assert(always_false<S>, "Unsupported type for action");
    }
  }

  template <typename T>
  bool MouseClick(T btnCode, int dx, int dy, int speed, float accel) {
    if (!MouseMove(dx, dy, speed, accel))
      return false;
    return EmitClick(static_cast<int>(btnCode), MouseAction::Click);
  }
  bool Scroll(double dy, double dx = 0);

  void EmergencyReleaseAllKeys();
  bool TryPressKey(int keycode);
  bool TryReleaseKey(int keycode);
  static Key GetKeyCode(cstr keyName);
  static double mouseSensitivity;
  static double scrollSpeed;
  bool shutdown = false;

  // Performance optimization: Keycode caching and batch event helpers
  static int GetKeyCacheLookup(const std::string &keyName);
  static std::vector<KeyToken> ParseKeyString(const std::string &keys);
  void SendBatchedKeyEvents(const std::vector<input_event> &events);

private:
  std::atomic<bool> globalAltPressed{false};
  std::mutex emergencyMutex;
  std::set<int> pressedKeys;
  std::mutex keyStateMutex;
  std::mutex hotkeySetMutex;

  template <typename T> static constexpr bool always_false = false;

  std::set<int> grabbedKeys;
  std::mutex grabbedKeysMutex;
  bool blockAllInput = false;

  struct ConditionalHotkeyState {
    int id;
    bool wasGrabbed;
  };
  std::vector<ConditionalHotkeyState> suspendedConditionalHotkeyStates;
  bool wasSuspended = false;

public:
  bool EmitClick(int btnCode, MouseAction action);

  static Key EvdevNameToKeyCode(std::string keyName);

  bool MatchEvdevModifiers(int expectedModifiers,
                           const std::map<int, bool> &keyState);
  std::map<int, bool> evdevKeyState;
  std::map<int, bool> evdevMouseButtonState;
  std::unique_ptr<HotkeyExecutor> hotkeyExecutor;
    std::atomic<ExecutorMode> executorMode_{ExecutorMode::Scheduler};
  std::unordered_map<std::string, HotKey> instanceHotkeys;
  std::unordered_map<std::string, bool> hotkeyStates;
  std::set<int> blockedKeys;

  // Static members
  static bool hotkeyEnabled;
  static int hotkeyCount;
  static std::atomic<int> syntheticEventsExpected;
  std::timed_mutex hotkeyMutex;
  std::mutex blockedKeysMutex;
  std::map<int, bool> keyDownState;

  std::mutex remapMutex;
  std::unordered_map<int, int> activeRemaps;

  mutable std::mutex mouseMutex;

  bool IsKeyRemappedTo(int targetKey);
  std::unordered_map<int, int> evdevKeyMap;
  std::unordered_map<int, int> evdevRemappedKeys;

  static std::vector<IoEvent> ParseKeysString(const std::string &keys);
  std::string findEvdevDevice(const std::string &deviceName);
  std::vector<InputDevice> getInputDevices();
  void listInputDevices();

  std::string getGamepadDevice();
  // Device detection helpers
  std::string detectEvdevDevice(
      const std::vector<std::string> &patterns,
      const std::function<bool(const std::string &, int)> &deviceFilter);
  std::string getKeyboardDevice();
  std::string getMouseDevice();
};
} // namespace havel
