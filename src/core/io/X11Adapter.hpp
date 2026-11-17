#pragma once

#include "InputBackend.hpp"
#include <shared_mutex>

#ifdef __linux__
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#endif

namespace havel {

// Whether the X11 adapter should re-request the root window event
// subscription.
//
// The X server refuses XSelectInput on the root window (BadAccess) while
// another client holds an active keyboard or pointer grab: the request
// would be delivered to the grabbing client instead. The refusal is
// transient - the other client eventually lets go - but before this rule
// existed the adapter asked once and never again, so the client received
// no key events for the rest of the process lifetime.
//
// Pure function: no X server, no display, no timing side effects.
bool ShouldRetrySelectInput(bool subscribed, bool hasDisplay,
                             std::chrono::steady_clock::time_point now,
                             std::chrono::steady_clock::time_point nextRetry);

class X11Adapter : public InputBackend {
public:
    X11Adapter();
    ~X11Adapter() override;

    InputBackendType GetType() const override { return InputBackendType::X11; }
    std::string GetName() const override { return "x11"; }

    bool Init() override;
    void Shutdown() override;

    bool IsInitialized() const override { return initialized_; }

    std::vector<DeviceInfo> EnumerateDevices() override;
    bool OpenDevice(const std::string &path) override;
    void CloseDevice(const std::string &path) override;

    bool GrabDevice(const std::string &path) override;
    void UngrabDevice(const std::string &path) override;
    void UngrabAllDevices() override;

    std::vector<int> GetInputFds() const override;
    void OnFdsReady(const std::vector<std::pair<int, short>> &ready) override;

    // Re-request the root window event subscription if the X server
    // refused it earlier.
    void Poll() override;

    std::pair<int, int> GetMousePosition() const override;
    bool GetKeyState(uint32_t code) const override;
    uint32_t GetModifiers() const override;

    bool SupportsGrab() const override { return true; }
    bool SupportsSynthesis() const override { return true; }

    bool SendKeyEvent(uint32_t code, bool down) override;
    bool SendMouseEvent(const MouseEvent &event) override;

    void SetMouseSensitivity(double sens) override { mouseSensitivity_ = sens; }
    double GetMouseSensitivity() const override { return mouseSensitivity_; }
    void SetScrollSpeed(double speed) override { scrollSpeed_ = speed; }
    double GetScrollSpeed() const override { return scrollSpeed_; }

    void SetKeyRemap(uint32_t from, uint32_t to) override;
    void RemoveKeyRemap(uint32_t from) override;

    void BeginBatch() override {}
    void QueueEvent(int, int, int) override {}
    void EndBatch() override {}

    void ReleaseAllKeys() override;
    void ReleaseAllVirtualKeys() override { ReleaseAllKeys(); }
    void EmergencyReleaseAllKeys() override { ReleaseAllKeys(); }

    void SetBlockInput(bool block) override { blockInput_ = block; }
    bool IsInputBlocked() const override { return blockInput_; }

    void SetEmergencyShutdownKey(uint32_t code) override { emergencyShutdownKey_ = code; }
    uint32_t GetEmergencyShutdownKey() const override { return emergencyShutdownKey_; }

    const std::unordered_map<uint32_t, ActiveInput>& GetActiveInputs() const override { return activeInputs_; }
    std::string GetActiveInputsString() const override;

    havel::ModifierState GetModifierState() const override;
    int GetCurrentModifiersMask() const override;

    bool ArePhysicalKeysPressed(const std::vector<uint32_t> &keys) const override;

    bool GetMouseButtonState(uint32_t button) const override;

    std::chrono::steady_clock::time_point GetKeyDownTime(uint32_t code) const override;

    void SetKeyDownCallback(KeyCallback cb) override { keyDownCallback_ = std::move(cb); }
    void SetKeyUpCallback(KeyCallback cb) override { keyUpCallback_ = std::move(cb); }
    void SetAnyKeyPressCallback(AnyKeyPressCallback cb) override { anyKeyPressCallback_ = std::move(cb); }
    void SetMouseMovementCallback(MouseMovementCallback cb) override { mouseMovementCallback_ = std::move(cb); }
    void SetInputNotificationCallback(std::function<void()> cb) override { inputNotificationCallback_ = std::move(cb); }
    void SetInputEventCallback(std::function<void(const InputEvent &)> cb) override { inputEventCallback_ = std::move(cb); }
    void SetInputBlockCallback(std::function<bool(const InputEvent &)> cb) override { inputBlockCallback_ = std::move(cb); }

    void SetComboTimeWindow(int ms) override { comboTimeWindow_ = ms; }
    int GetComboTimeWindow() const override { return comboTimeWindow_; }

    std::chrono::steady_clock::time_point GetLastWheelUpTime() const override { return lastWheelUpTime_; }
    std::chrono::steady_clock::time_point GetLastWheelDownTime() const override { return lastWheelDownTime_; }

    size_t GetDeviceCount() const override { return 1; }
    size_t GetGrabbedDeviceCount() const override { return (keyboardGrabbed_ || pointerGrabbed_) ? 1 : 0; }

    // X11-specific
    void SetDisplayName(const std::string &name) { displayName_ = name; }

private:
#ifdef __linux__
    void ProcessX11Event(XEvent &event);
    void ProcessKeyEvent(const XKeyEvent &ke, bool down);
    void ProcessButtonEvent(const XButtonEvent &be, bool down);
    void ProcessMotionEvent(const XMotionEvent &me);

    uint32_t X11KeycodeToEvdev(KeyCode keycode) const;
    KeyCode EvdevToX11Keycode(uint32_t evdev) const;
    uint32_t X11ButtonToEvdev(unsigned int button) const;
    uint32_t X11ModifiersToHavel(unsigned int state) const;
    unsigned int HavelModifiersToX11(uint32_t modifiers) const;
    unsigned int CleanMask(unsigned int mask) const;

    void BuildKeycodeMap();
    void UpdateModifierStateInternal(uint32_t code, bool down);

    // Ask the X server to deliver key/button/motion events on the root
    // window. Returns false when the server refused the request, which
    // happens (BadAccess) while another client holds an active keyboard or
    // pointer grab: the request would be delivered to the grabbing client
    // instead. That refusal is transient - the other client eventually lets
    // go - so the call is retried until it succeeds.
    bool SelectRootInputEvents();

    // Retry a refused SelectRootInputEvents(). Called from the poll loop,
    // which runs continuously, so a grab taken by another client only
    // delays delivery, it does not kill it.
    void RetrySelectInput();
#endif

    bool initialized_ = false;
    bool keyboardGrabbed_ = false;
    bool pointerGrabbed_ = false;
    std::string displayName_;

#ifdef __linux__
    Display *display_ = nullptr;
    Window root_ = 0;
    int xfd_ = -1;
    // True once the root window has been subscribed to key/button/motion
    // events. False means the X server refused the subscription, so no
    // events arrive at all.
    bool rootInputSelected_ = false;
    // When the next retry of a refused subscription may be attempted. The
    // poll loop runs every millisecond, and the X server would answer every
    // one of those requests with BadAccess; retrying once a second is
    // enough to recover promptly and keeps the X server quiet.
    std::chrono::steady_clock::time_point nextSelectRetry_{};
#endif

    bool blockInput_ = false;
    uint32_t emergencyShutdownKey_ = 0;

    mutable std::shared_mutex stateMutex_;

    std::unordered_map<uint32_t, bool> keyStates_;
    std::unordered_map<uint32_t, bool> buttonStates_;
    std::unordered_map<uint32_t, ActiveInput> activeInputs_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> keyDownTime_;
    havel::ModifierState modifierState_;
    uint32_t modifiers_ = 0;
    mutable int32_t mouseX_ = 0;
    mutable int32_t mouseY_ = 0;

    std::unordered_map<uint32_t, uint32_t> keyRemaps_;
    std::unordered_map<uint32_t, uint32_t> activeRemaps_;
    mutable std::mutex remapMutex_;

    double mouseSensitivity_ = 1.0;
    double scrollSpeed_ = 1.0;
    int comboTimeWindow_ = 0;
    std::chrono::steady_clock::time_point lastWheelUpTime_{};
    std::chrono::steady_clock::time_point lastWheelDownTime_{};

#ifdef __linux__
    std::unordered_map<KeyCode, uint32_t> keycodeToEvdev_;
    std::unordered_map<uint32_t, KeyCode> evdevToKeycode_;
    static constexpr unsigned int RELEVANT_MODIFIERS =
        ShiftMask | LockMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask;
#endif

    KeyCallback keyDownCallback_;
    KeyCallback keyUpCallback_;
    AnyKeyPressCallback anyKeyPressCallback_;
    MouseMovementCallback mouseMovementCallback_;
    std::function<void()> inputNotificationCallback_;
    std::function<void(const InputEvent &)> inputEventCallback_;
    std::function<bool(const InputEvent &)> inputBlockCallback_;

    // Signal-safe emergency ungrab (async-signal-safe: no locks, no logging)
    void SignalSafeUngrabAll();

public:
    // Allow emergency ungrab functions to access private method
    friend void EmergencyUngrabAllX11SignalSafe();
};

// Global active X11Adapter for signal-safe emergency ungrab
extern std::atomic<X11Adapter *> g_active_x11_adapter;

std::unique_ptr<InputBackend> CreateX11Adapter();

}