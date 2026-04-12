#pragma once
#include "HotkeyExecutor.hpp" // Include HotkeyExecutor
#include "MouseGestureEngine.hpp"
#include "UinputDevice.hpp"           // Include UinputDevice
#include "core/CallbackTypes.hpp"     // Include callback types
#include "../MouseGestureTypes.hpp" // Include mouse gesture types
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <linux/input.h>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <sys/eventfd.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include "X11HotkeyMonitor.hpp"
#endif

namespace havel {

// Forward declarations
namespace compiler {
  class HostBridge;
}

// Forward declarations
struct HotKey;
class SignalHandler;

// Event listener that handles all input devices with unified evdev logic
class EventListener {
public:
  // Modifier keys bitmask
  enum Modifier {
    Null = 0,
    Ctrl = 1 << 0,
    Shift = 1 << 1,
    Alt = 1 << 2,
    Meta = 1 << 3
  };
  EventListener();
  ~EventListener();

  // Start listening on specified devices
  bool Start(const std::vector<std::string> &devicePaths,
             bool grabDevices = false);
  
  // Set HostBridge for timer checking
  void setHostBridge(havel::compiler::HostBridge *hostBridge);
  
  std::map<int, bool> evdevKeyState;

  // Stop listening
  void Stop();
  // Drain all pending events from a device
  void DrainDeviceEvents(int fd);
  // Enable/disable device grabbing
  void SetGrabDevices(bool grab) { grabDevices = grab; }

  // Check if running
  bool IsRunning() const { return running.load(); }

  // Get key state
  bool GetKeyState(int evdevCode) const;

  // Get modifier state
  struct ModifierState {
    bool leftCtrl = false;
    bool rightCtrl = false;
    bool leftShift = false;
    bool rightShift = false;
    bool leftAlt = false;
    bool rightAlt = false;
    bool leftMeta = false;
    bool rightMeta = false;

    bool IsCtrlPressed() const { return leftCtrl || rightCtrl; }
    bool IsShiftPressed() const { return leftShift || rightShift; }
    bool IsAltPressed() const { return leftAlt || rightAlt; }
    bool IsMetaPressed() const { return leftMeta || rightMeta; }
  };

  const ModifierState &GetModifierState() const;

  // Debugging
  std::string GetModifiersString() const;
  std::string GetActiveInputsString() const;

  int GetCurrentModifiersMask() const;
  // Hotkey optimization
  struct ActiveInput {
    std::chrono::steady_clock::time_point timestamp;
    int modifiers; // Modifiers that were held when this key was pressed

    ActiveInput() : modifiers(0) {}
    explicit ActiveInput(int mods)
        : timestamp(std::chrono::steady_clock::now()), modifiers(mods) {}
    ActiveInput(int mods, std::chrono::steady_clock::time_point time)
        : timestamp(time), modifiers(mods) {}
  };

  // Setup uinput for event forwarding
  bool SetupUinput();

  // Send event through uinput
  void SendUinputEvent(int type, int code, int value);

  // Batch event sending for reduced syscall overhead
  void BeginUinputBatch();
  void QueueUinputEvent(int type, int code, int value);
  void EndUinputBatch();

  // Emergency release all keys
  void EmergencyReleaseAllKeys();

  // Set blocking mode for specific keys
  void SetBlockInput(bool block);

  // Add key remap
  void AddKeyRemap(int fromCode, int toCode);

  // Remove key remap
  void RemoveKeyRemap(int fromCode);

  // Set emergency shutdown key
  void SetEmergencyShutdownKey(int evdevCode);

  // Set mouse sensitivity (1.0 = default)
  void SetMouseSensitivity(double sensitivity);
  double GetMouseSensitivity() const { return mouseSensitivity; }

  // Set scroll speed (1.0 = default)
  void SetScrollSpeed(double speed);
  double GetScrollSpeed() const { return scrollSpeed; }
  using MouseMovementCallback = std::function<void(int dx, int dy)>;

  void SetMouseMovementCallback(MouseMovementCallback callback) {
    mouseMovementCallback = callback;
  }
// X11 hotkey monitoring (separate from evdev)
#ifdef __linux__
  bool StartX11Monitor(Display *display);
  void StopX11Monitor();
  bool IsX11MonitorRunning() const;
#endif

  // Callback for any key press
  void SetAnyKeyPressCallback(AnyKeyPressCallback callback);

  // Raw input event stream for external hotkey ownership.
  void SetInputEventCallback(InputEventCallback callback) {
    inputEventCallback = std::move(callback);
  }

  // Callback to decide if an input event should be blocked from forwarding.
  void SetInputBlockCallback(std::function<bool(const InputEvent &)> callback) {
    inputBlockCallback = std::move(callback);
  }

  // Callback for raw key down events
  using KeyCallback = std::function<void(int keyCode)>;
  void SetKeyDownCallback(KeyCallback callback) {
    keyDownCallback = std::move(callback);
  }

  // Callback for raw key up events
  void SetKeyUpCallback(KeyCallback callback) {
    keyUpCallback = std::move(callback);
  }

  // Set HotkeyExecutor for thread-safe callback execution
  void SetHotkeyExecutor(HotkeyExecutor *executor) {
    hotkeyExecutor = executor;
  }

  // Get current mouse position (from evdev ABS events)
  std::pair<int, int> GetMousePosition() const {
    return {currentMouseX, currentMouseY};
  }

  // Callback for any key press
  std::function<void(const std::string &key)> anyKeyPressCallback = nullptr;

  // Raw key down/up callbacks
  KeyCallback keyDownCallback = nullptr;
  KeyCallback keyUpCallback = nullptr;

  // Raw input events emitted before internal matching.
  InputEventCallback inputEventCallback = nullptr;
  std::function<bool(const InputEvent &)> inputBlockCallback = nullptr;

  // Callback for input notification (for watchdog)
  std::function<void()> inputNotificationCallback = nullptr;

  // Release all pressed virtual keys (for clean shutdown)
  void ReleaseAllVirtualKeys();

  // Force ungrab all devices - async-signal-safe for signal handlers
  // This is the CRITICAL method that prevents stuck grabs
  void ForceUngrabAllDevices();

  // Signal handling methods
  void SetupSignalHandling();
  void ProcessSignal();
  void RequestShutdownFromSignal(int sig);

private:
  friend class SignalHandler;
  void HandleSignal(int sig);
  void SignalSafeShutdown(int sig, bool exitAfter = false);
  // Signal handling members
  std::atomic<bool> signalReceived{false};
  int signalFd = -1; // fd for signalfd to integrate with select loop
  sigset_t signalMask{};
  std::sig_atomic_t asyncSignalRequested = 0;

  // Signal handling for device cleanup
  std::atomic<int> pendingSignal{0};

  // Mouse position tracking
  int currentMouseX = 0;
  int currentMouseY = 0;

private:
  std::unique_ptr<SignalHandler> signalHandler;
  
  // HostBridge for timer checking (single-threaded VM timer queue)
  havel::compiler::HostBridge *hostBridge = nullptr;

  // Device info
  struct DeviceInfo {
    std::string path;
    int fd = -1;
    std::string name;
  };

  // Main event loop (exact logic from IO.cpp)
  void EventLoop();

  // Process keyboard event (exact logic from IO.cpp)
  void ProcessKeyboardEvent(const input_event &ev);

  // Process mouse event
  void ProcessMouseEvent(const input_event &ev);

  // Hotkey evaluation helpers (legacy; kept for compatibility)
  void ExecuteHotkeyCallback(const HotKey &hotkey);
  bool EvaluateHotkeys(int evdevCode, bool down, bool repeat);
  bool EvaluateCombo(const HotKey &hotkey);
  bool EvaluateWheelCombo(const HotKey &hotkey, int wheelDirection);
  void QueueMouseMovementHotkey(int virtualKey);
  void ProcessQueuedMouseMovementHotkeys();
  void EvaluateMouseMovementHotkeys(int virtualKey);

  // Mouse gesture helpers (legacy; kept for compatibility)
  void ProcessMouseGesture(int dx, int dy);
  MouseGestureDirection GetGestureDirection(int dx, int dy) const;
  bool
  MatchGesturePattern(const std::vector<MouseGestureDirection> &expected,
                      const std::vector<MouseGestureDirection> &actual) const;
  void ResetMouseGesture();
  void ResetInputState();
  void
  RegisterGestureHotkey(int id,
                        const std::vector<MouseGestureDirection> &directions);
  bool IsGestureValid(const std::vector<MouseGestureDirection> &pattern,
                      int minDistance) const;
  std::vector<MouseGestureDirection>
  ParseGesturePattern(const std::string &patternStr) const;
  std::vector<MouseGestureDirection>
  ParseGesturePattern(const HotKey &hotkey) const;

  // Check modifier match (exact logic from IO.cpp)
  bool CheckModifierMatch(int requiredModifiers, bool wildcard) const;

  // Check modifier match while excluding a specific modifier (for remapped
  // keys)
  bool CheckModifierMatchExcludingModifier(int requiredModifiers, bool wildcard,
                                           int excludeModifier) const;

  // Update modifier state
  void UpdateModifierState(int evdevCode, bool down);

  // Handle key remap
  int RemapKey(int evdevCode, bool down);

  // Helper function to check if specific physical keys are pressed
  bool ArePhysicalKeysPressed(const std::vector<int> &requiredKeys) const;

  std::atomic<bool> running{false};
  std::atomic<bool> shutdown{false};
  std::atomic<bool> blockInput{false};
  std::atomic<int> pendingCallbacks{0};
  std::thread eventThread;

  std::vector<DeviceInfo> devices;
  int shutdownFd = -1; // eventfd for clean shutdown
  int emergencyShutdownKey = 0;

  // Uinput device for virtual input
  std::unique_ptr<UinputDevice> uinputDevice;

  // State tracking (exact from IO.cpp)
  mutable std::shared_mutex stateMutex;
  std::map<int, std::chrono::steady_clock::time_point> keyDownTime;
  std::unordered_map<int, ActiveInput> activeInputs;
  ModifierState modifierState;
  std::unordered_map<int, bool> physicalKeyStates;

  // Track pressed virtual keys sent to uinput to release on shutdown
  std::unordered_set<int> pressedVirtualKeys;

  // Key remapping (exact from IO.cpp)
  std::mutex remapMutex;
  std::map<int, int> keyRemaps;
  std::map<int, int> activeRemaps;

  // Mouse and scroll sensitivity
  double mouseSensitivity = 1.0;
  double scrollSpeed = 1.0;

  // Hotkey management (legacy)
  mutable std::shared_mutex hotkeyMutex;
  std::unordered_map<int, std::vector<int>> combosByKey;
  std::unordered_map<int, int> comboPressedCount;
  int comboTimeWindow = 0; // milliseconds (0 = infinite)
  std::map<int, bool> mouseButtonState;
  bool isProcessingWheelEvent = false;
  int currentWheelDirection = 0;
  std::chrono::steady_clock::time_point lastWheelUpTime{};
  std::chrono::steady_clock::time_point lastWheelDownTime{};
  MouseGestureEngine mouseGestureEngine;
  std::chrono::steady_clock::time_point lastMovementHotkeyTime{};
  mutable std::shared_mutex movementHotkeyMutex;
  std::queue<int> queuedMovementHotkeys;
  std::atomic<bool> movementHotkeyProcessing{false};

  // Device grabbing
  bool grabDevices = false;
  MouseMovementCallback mouseMovementCallback;

  // HotkeyExecutor for thread-safe callback execution
  HotkeyExecutor *hotkeyExecutor = nullptr;
  std::mutex hotkeyExecMutex;
  std::unordered_set<std::string> executingHotkeys;

  // Event batching for reduced syscall overhead
  std::vector<input_event> uinputBatchBuffer;
  std::mutex uinputBatchMutex; // Protects batch buffer
  std::atomic<bool> uinputBatching{false};
  static constexpr size_t MAX_BATCH_SIZE = 16;  // Flush after 16 events
  static constexpr int BATCH_TIMEOUT_US = 5000; // Or after 5ms

// X11 hotkey monitor (separate component)
#ifdef __linux__
  std::unique_ptr<X11HotkeyMonitor> x11Monitor;
#endif
};

} // namespace havel
