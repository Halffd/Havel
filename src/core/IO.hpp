#pragma once
#include "types.hpp"
#include <atomic>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <X11/extensions/XInput2.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cerrno>
#include <condition_variable>
#include <mutex>
#include "x11.h"
#define CLEANMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

namespace havel {

enum class MouseButton {
  Left = BTN_LEFT,
  Right = BTN_RIGHT,
  Middle = BTN_MIDDLE,
  Side1 = BTN_SIDE,
  Side2 = BTN_EXTRA
};

enum class MouseAction { Hold = 1, Release = 0, Click = 2 };

enum class HotkeyEventType { Both, Down, Up };

enum class HotkeyType {
    Keyboard,
    MouseButton,
    MouseWheel,
    MouseMove,
    Combo
};

struct HotKey {
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

struct IoEvent {
  Key key;
  int modifiers;
  bool isDown;
};
class GrabException : public std::exception {
public:
  const char *what() const noexcept override {
    return "Failed to grab hotkey";
  }
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
  std::thread mouseEvdevThread;
  int mouseUinputFd = -1;
  std::atomic<bool> globalAltPressed{false};
  std::chrono::steady_clock::time_point lastLeftPress;
  std::chrono::steady_clock::time_point lastRightPress;
  
  // Deadlock protection
  int evdevShutdownFd = -1;  // eventfd for clean shutdown
  std::atomic<bool> callbackInProgress{false};
  std::chrono::steady_clock::time_point lastCallbackStart;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::atomic<int> pendingCallbacks{0};
  static constexpr int CALLBACK_TIMEOUT_MS = 5000;  // 5 second timeout for callbacks
  static constexpr int WATCHDOG_INTERVAL_MS = 1000;  // Check every second

public:
  static std::unordered_map<int, HotKey> hotkeys;
  bool isSuspended = false;
  bool globalEvdev = true;
  std::vector<HotKey> failedHotkeys;
  std::set<int> x11Hotkeys;
  std::set<int> evdevHotkeys;
  IO();

  ~IO();

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

  bool AddHotkey(const std::string &alias, Key key, int modifiers,
                 std::function<void()> callback);
  HotKey AddMouseHotkey(const std::string &hotkeyStr,
  std::function<void()> action, int id = 0, bool grab = false);

  HotKey AddHotkey(const std::string &rawInput, std::function<void()> action, int id = 0);

  bool Hotkey(const std::string &hotkeyStr, std::function<void()> action,
              int id = 0);
  bool Suspend();
  bool Suspend(int id);

  bool Resume(int id);

  // Mouse methods
  bool MouseMove(int dx, int dy, int speed = 1, float accel = 1.0f);
  
  // XInput2 hardware mouse control
  bool InitializeXInput2();
  bool SetHardwareMouseSensitivity(double sensitivity);
  
  // Mouse event handling
  bool StartEvdevMouseListener(const std::string &mouseDevicePath);
  void StopEvdevMouseListener();
  bool handleMouseButton(const input_event& ev, bool& leftPressed, bool& rightPressed, bool altPressed);
  bool handleMouseRelative(const input_event& ev, bool altPressed);
  bool handleMouseAbsolute(const input_event& ev, bool altPressed);
  bool SetupMouseUinputDevice();
  void SendMouseUInput(const input_event& ev);
  void setGlobalAltState(bool pressed);
  bool getGlobalAltState();
  void executeComboAction(const std::string& action);
  
  // Mouse sensitivity control (1.0 is default, lower values decrease sensitivity, higher values increase it)
  void SetMouseSensitivity(double sensitivity);
  double GetMouseSensitivity() const;
  
  // Mouse scroll speed control (1.0 is default, lower values decrease speed, higher values increase it)
  void SetScrollSpeed(double speed);
  double GetScrollSpeed() const;
  
  // Enhanced mouse movement with custom sensitivity
  bool MouseMoveSensitive(int dx, int dy, int baseSpeed = 5, float accel = 1.5f);

  void MouseClick(int button);
  void MouseDown(int button);
  void MouseUp(int button);
  void MouseWheel(int amount);

  // State methods
  bool GetKeyState(const std::string& keyName);
  bool GetKeyState(int keycode); // For raw keycodes
  bool IsKeyPressed(const std::string& keyName) { return GetKeyState(keyName); }
  
  // Modifier state helpers
  bool IsShiftPressed();
  bool IsCtrlPressed(); 
  bool IsAltPressed();
  bool IsWinPressed();

  static void PressKey(const std::string &keyName, bool press);

  // Utility methods
  std::shared_ptr<std::atomic<bool>>
  SetTimer(int milliseconds, const std::function<void()> &func);

  void MsgBox(const std::string &message);

  int GetMouse();

  int GetKeyboard();

  int ParseModifiers(std::string str);
  static int ParseMouseButton(const std::string& str);

  void AssignHotkey(HotKey hotkey, int id);

  // Add new methods for dynamic hotkey grabbing/ungrabbing
  bool GrabHotkey(int hotkeyId);

  bool UngrabHotkey(int hotkeyId);

  bool GrabHotkeysByPrefix(const std::string &prefix);

  bool UngrabHotkeysByPrefix(const std::string &prefix);

  // Key mapping
  void Map(const std::string& from, const std::string& to);
  void Remap(const std::string& key1, const std::string& key2);

  // Static methods
  static void removeSpecialCharacters(std::string &keyName);

  static Key StringToButton(const std::string &buttonNameRaw);

  static Key StringToVirtualKey(std::string keyName);

  // Call this to start listening on your keyboard device
  bool StartEvdevHotkeyListener(const std::string &devicePath);

  // Call this to stop the thread cleanly
  void StopEvdevHotkeyListener();

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
    return Click(btnCode, MouseAction::Click);
  }
  bool Scroll(int dy, int dx = 0);

  void EmergencyReleaseAllKeys();
  bool TryPressKey(int keycode);
  bool TryReleaseKey(int keycode);
  static Key GetKeyCode(cstr keyName);
private:
  Display* display;
  int uinputFd;
  std::mutex emergencyMutex;
  std::set<int> pressedKeys;
  std::mutex keyStateMutex;
  template <typename T> static constexpr bool always_false = false;
  unsigned int numlockmask = 0;


  std::set<int> grabbedKeys;  // Keys that should be blocked
  std::mutex grabbedKeysMutex;
  bool blockAllInput = false;  // Emergency block all input
  static int XErrorHandler(Display *dpy, XErrorEvent *ee);
  
  void UpdateNumLockMask();
  bool ModifierMatch(unsigned int expected, unsigned int actual);
  bool EmitClick(int btnCode, int action);

  void CleanupUinputDevice();
  bool SetupUinputDevice();
  // X11 hotkey monitoring
  void MonitorHotkeys();

  static Key EvdevNameToKeyCode(std::string keyName);

  bool MatchEvdevModifiers(int expectedModifiers, const std::map<int, bool>& keyState);
  // Platform specific implementations
  std::map<std::string, Key> keyMap;
  std::map<int, bool> evdevKeyState;
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
  std::timed_mutex hotkeyMutex;  // Use timed_mutex for try_lock_for() support
  std::mutex blockedKeysMutex;
  std::map<int, bool> keyDownState;
  
  // Mouse control members
  mutable std::mutex mouseMutex;
  double mouseSensitivity = 1.0;  // Default sensitivity (1.0 = 100%)
  double scrollSpeed = 1.0;       // Default scroll speed (1.0 = 100%)
  
  // XInput2 device ID for the pointer device
  int xinput2DeviceId = -1;
  bool xinput2Available = false;

  // Key mapping and sending utilities
  void InitKeyMap();
  std::unordered_map<KeySym, KeySym> keyMapInternal;
  std::unordered_map<KeySym, KeySym> remappedKeys;
  
  // Evdev key mapping
  std::unordered_map<int, int> evdevKeyMap;        // Maps from scancode to scancode
  std::unordered_map<int, int> evdevRemappedKeys;  // Bidirectional remapping

  void SendKeyEvent(Key key, bool down);

  std::vector<IoEvent> ParseKeysString(const std::string &keys);

  // Helper methods for X11 key grabbing
  bool Grab(Key input, unsigned int modifiers, Window root, bool grab,
            bool isMouse = false);

  void Ungrab(Key input, unsigned int modifiers, Window root, bool isMouse = false);
  bool GrabKeyboard();
  bool FastGrab(Key input, unsigned int modifiers, Window root);
  bool GrabAllHotkeys();
  void UngrabAll();
  std::string findEvdevDevice(const std::string& deviceName);
  std::vector<InputDevice> getInputDevices();
  void listInputDevices();
  
  // Device detection helpers
  std::string detectEvdevDevice(
    const std::vector<std::string> &patterns,
    const std::function<bool(const std::string &, int)> &deviceFilter);
  std::string getKeyboardDevice();
  std::string getMouseDevice();
};
} // namespace havel
