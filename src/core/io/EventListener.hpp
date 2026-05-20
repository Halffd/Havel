#pragma once
#include "HotkeyExecutor.hpp"
#include "MouseGestureEngine.hpp"
#include "InputBackend.hpp"
#include "core/CallbackTypes.hpp"
#include "../MouseGestureTypes.hpp" // Include mouse gesture types
#include "core/HotkeyManager.hpp"  // Include HotkeyManager
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
  class ExecutionEngine;
}

// Forward declarations
struct HotKey;
class SignalHandler;

// Event listener that handles all input devices with unified evdev logic
class EventListener {
public:
  enum Modifier {
    Null = 0,
    Ctrl = 1 << 0,
    Shift = 1 << 1,
    Alt = 1 << 2,
    Meta = 1 << 3
  };
  EventListener();
  ~EventListener();

  bool Start(const std::vector<std::string> &devicePaths,
             bool grabDevices = false);

  void setHostBridge(havel::compiler::HostBridge *hostBridge);
  void setExecutionEngine(havel::compiler::ExecutionEngine *executionEngine);
  void setHotkeyManager(HotkeyManager *manager);

  std::map<int, bool> evdevKeyState;

  void Stop();
  void SetGrabDevices(bool grab) { grabDevices = grab; }
  bool IsRunning() const { return running.load(); }
  bool GetKeyState(int evdevCode) const;

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

  std::string GetModifiersString() const;
  std::string GetActiveInputsString() const;
  int GetCurrentModifiersMask() const;

  struct ActiveInput {
    std::chrono::steady_clock::time_point timestamp;
    int modifiers;

    ActiveInput() : modifiers(0) {}
    explicit ActiveInput(int mods)
        : timestamp(std::chrono::steady_clock::now()), modifiers(mods) {}
    ActiveInput(int mods, std::chrono::steady_clock::time_point time)
        : timestamp(time), modifiers(mods) {}
  };

  bool SetupUinput();
  void SendUinputEvent(int type, int code, int value);
  void BeginUinputBatch();
  void QueueUinputEvent(int type, int code, int value);
  void EndUinputBatch();
  void EmergencyReleaseAllKeys();
  void SetBlockInput(bool block);
  void AddKeyRemap(int fromCode, int toCode);
  void RemoveKeyRemap(int fromCode);
  void SetEmergencyShutdownKey(int evdevCode);
  void SetMouseSensitivity(double sensitivity);
  double GetMouseSensitivity() const { return mouseSensitivity; }
  void SetScrollSpeed(double speed);
  double GetScrollSpeed() const { return scrollSpeed; }

  using MouseMovementCallback = std::function<void(int dx, int dy)>;
  void SetMouseMovementCallback(MouseMovementCallback callback) {
    mouseMovementCallback = callback;
  }

#ifdef __linux__
  bool StartX11Monitor(Display *display);
  void StopX11Monitor();
  bool IsX11MonitorRunning() const;
#endif

  void SetAnyKeyPressCallback(AnyKeyPressCallback callback);
  void SetInputEventCallback(InputEventCallback callback) {
    inputEventCallback = std::move(callback);
  }
  void SetInputBlockCallback(std::function<bool(const InputEvent &)> callback) {
    inputBlockCallback = std::move(callback);
  }

  using KeyCallback = std::function<void(int keyCode)>;
  void SetKeyDownCallback(KeyCallback callback) {
    keyDownCallback = std::move(callback);
  }
  void SetKeyUpCallback(KeyCallback callback) {
    keyUpCallback = std::move(callback);
  }

  void SetHotkeyExecutor(HotkeyExecutor *executor) {
    hotkeyExecutor = executor;
  }
  void SetExecutorMode(ExecutorMode mode) {
    executorMode_ = mode;
  }

  std::pair<int, int> GetMousePosition() const {
    return {currentMouseX, currentMouseY};
  }

  std::function<void(const std::string &key)> anyKeyPressCallback = nullptr;
  KeyCallback keyDownCallback = nullptr;
  KeyCallback keyUpCallback = nullptr;
  InputEventCallback inputEventCallback = nullptr;
  std::function<bool(const InputEvent &)> inputBlockCallback = nullptr;
  std::function<void()> inputNotificationCallback = nullptr;

  void ReleaseAllVirtualKeys();
  void ForceUngrabAllDevices();

  void SetupSignalHandling();
  void ProcessSignal();
  void RequestShutdownFromSignal(int sig);

  // Callback invoked when the event loop exits due to a shutdown signal
  // Used by embedders (e.g., Qt main loop) to unblock the main thread
  void SetShutdownCallback(std::function<void()> cb) {
    shutdownCallback_ = std::move(cb);
  }

private:
  friend class IO;
  friend class SignalHandler;
  void HandleSignal(int sig);
  void SignalSafeShutdown(int sig, bool exitAfter = false);
  void InitInputBackend(const std::vector<std::string> &devicePaths, bool grab);
  void OnBackendKeyEvent(const KeyEvent &ke);
  void OnBackendMouseEvent(const MouseEvent &me);

  std::atomic<bool> signalReceived{false};
  int signalFd = -1;
  sigset_t signalMask{};
  std::sig_atomic_t asyncSignalRequested = 0;
  std::atomic<int> pendingSignal{0};

  int currentMouseX = 0;
  int currentMouseY = 0;

private:
  std::unique_ptr<SignalHandler> signalHandler;
  std::unique_ptr<InputBackend> backend_;

  std::function<void()> shutdownCallback_;

  havel::compiler::HostBridge *hostBridge = nullptr;
  havel::compiler::ExecutionEngine *executionEngine = nullptr;
  HotkeyManager *hotkeyManager = nullptr;

  void EventLoop();
  void ProcessKeyboardEvent(const input_event &ev);
  void ProcessMouseEvent(const input_event &ev);

  // Hotkey evaluation helpers (legacy; kept for compatibility)
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

  int shutdownFd = -1;
  int emergencyShutdownKey = 0;

  // State tracking
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
ExecutorMode executorMode_ = ExecutorMode::Scheduler;
std::mutex hotkeyExecMutex;
  std::unordered_set<std::string> executingHotkeys;



// X11 hotkey monitor (separate component)
#ifdef __linux__
  std::unique_ptr<X11HotkeyMonitor> x11Monitor;
#endif
};

} // namespace havel
