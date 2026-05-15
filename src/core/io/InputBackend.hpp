#pragma once

#include "core/CallbackTypes.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/eventfd.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel {

enum class InputBackendType {
    Evdev,
    X11,
    Wayland,
    Windows,
    Unknown
};

struct KeyEvent {
    uint32_t code = 0;
    bool down = false;
    bool repeat = false;
    uint32_t modifiers = 0;
    std::string keyName;
    std::chrono::steady_clock::time_point timestamp;
};

struct MouseEvent {
    enum class Type { Button, Move, Wheel, Absolute };
    Type type = Type::Move;
    uint32_t button = 0;
    bool down = false;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t wheel = 0;
    int32_t absoluteX = 0;
    int32_t absoluteY = 0;
    uint32_t modifiers = 0;
    std::chrono::steady_clock::time_point timestamp;
};

struct DeviceInfo {
    std::string path;
    std::string name;
    int fd = -1;
    InputBackendType backend = InputBackendType::Unknown;
};

struct ActiveInput {
    std::chrono::steady_clock::time_point timestamp;
    uint32_t modifiers = 0;

    ActiveInput() = default;
    explicit ActiveInput(uint32_t mods)
        : timestamp(std::chrono::steady_clock::now()), modifiers(mods) {}
    ActiveInput(uint32_t mods, std::chrono::steady_clock::time_point time)
        : timestamp(time), modifiers(mods) {}
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

    bool IsCtrlPressed() const { return leftCtrl || rightCtrl; }
    bool IsShiftPressed() const { return leftShift || rightShift; }
    bool IsAltPressed() const { return leftAlt || rightAlt; }
    bool IsMetaPressed() const { return leftMeta || rightMeta; }
};

class InputBackend {
public:
    virtual ~InputBackend() = default;

    virtual InputBackendType GetType() const = 0;
    virtual std::string GetName() const = 0;

    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    virtual bool IsInitialized() const = 0;

    virtual std::vector<DeviceInfo> EnumerateDevices() = 0;
    virtual bool OpenDevice(const std::string &path) = 0;
    virtual void CloseDevice(const std::string &path) = 0;

    virtual bool GrabDevice(const std::string &path) = 0;
    virtual void UngrabDevice(const std::string &path) = 0;
    virtual void UngrabAllDevices() = 0;

    virtual int GetPollFd() const = 0;
    virtual bool PollEvents(int timeoutMs = 100) = 0;

    virtual std::pair<int, int> GetMousePosition() const = 0;
    virtual bool GetKeyState(uint32_t code) const = 0;
    virtual uint32_t GetModifiers() const = 0;

    virtual bool SupportsGrab() const { return false; }
    virtual bool SupportsSynthesis() const { return false; }
    virtual bool SendKeyEvent(uint32_t code, bool down) { return false; }
    virtual bool SendMouseEvent(const MouseEvent &event) { return false; }

    // Sensitivity settings
    virtual void SetMouseSensitivity(double sens) { (void)sens; }
    virtual double GetMouseSensitivity() const { return 1.0; }
    virtual void SetScrollSpeed(double speed) { (void)speed; }
    virtual double GetScrollSpeed() const { return 1.0; }

    // Key remapping
    virtual void SetKeyRemap(uint32_t from, uint32_t to) { (void)from; (void)to; }
    virtual void RemoveKeyRemap(uint32_t from) { (void)from; }

    // Event batching
    virtual void BeginBatch() {}
    virtual void QueueEvent(int type, int code, int value) { (void)type; (void)code; (void)value; }
    virtual void EndBatch() {}

    // Key management
    virtual void ReleaseAllKeys() {}
    virtual void ReleaseAllVirtualKeys() {}
    virtual void EmergencyReleaseAllKeys() {}

    // Input blocking
    virtual void SetBlockInput(bool block) { (void)block; }
    virtual bool IsInputBlocked() const { return false; }

    // Emergency shutdown
    virtual void SetEmergencyShutdownKey(uint32_t code) { (void)code; }
    virtual uint32_t GetEmergencyShutdownKey() const { return 0; }

    // Active input tracking
    virtual const std::unordered_map<uint32_t, ActiveInput>& GetActiveInputs() const {
        static std::unordered_map<uint32_t, ActiveInput> empty;
        return empty;
    }
    virtual std::string GetActiveInputsString() const { return "[none]"; }

    // Modifier state details
    virtual ModifierState GetModifierState() const { return ModifierState{}; }
    virtual int GetCurrentModifiersMask() const { return 0; }

    // Physical key state checking
    virtual bool ArePhysicalKeysPressed(const std::vector<uint32_t> &keys) const { (void)keys; return false; }

    // Mouse button state
    virtual bool GetMouseButtonState(uint32_t button) const { (void)button; return false; }

    // Key down time tracking
    virtual std::chrono::steady_clock::time_point GetKeyDownTime(uint32_t code) const {
        (void)code;
        return std::chrono::steady_clock::time_point{};
    }

    // Callbacks
    using KeyCallback = std::function<void(uint32_t)>;
    using MouseMovementCallback = std::function<void(int dx, int dy)>;
    using AnyKeyPressCallback = std::function<void(const std::string &key)>;

    void SetKeyCallback(std::function<void(const KeyEvent &)> cb) { keyCallback_ = std::move(cb); }
    void SetMouseCallback(std::function<void(const MouseEvent &)> cb) { mouseCallback_ = std::move(cb); }

    virtual void SetKeyDownCallback(KeyCallback cb) { (void)cb; }
    virtual void SetKeyUpCallback(KeyCallback cb) { (void)cb; }
    virtual void SetAnyKeyPressCallback(AnyKeyPressCallback cb) { (void)cb; }
    virtual void SetMouseMovementCallback(MouseMovementCallback cb) { (void)cb; }
    virtual void SetInputNotificationCallback(std::function<void()> cb) { (void)cb; }

    virtual void SetInputEventCallback(std::function<void(const InputEvent &)> cb) { (void)cb; }
    virtual void SetInputBlockCallback(std::function<bool(const InputEvent &)> cb) { (void)cb; }

    // Combo hotkey time window
    virtual void SetComboTimeWindow(int ms) { (void)ms; }
    virtual int GetComboTimeWindow() const { return 0; }

    // Wheel event tracking
    virtual std::chrono::steady_clock::time_point GetLastWheelUpTime() const {
        return std::chrono::steady_clock::time_point{};
    }
    virtual std::chrono::steady_clock::time_point GetLastWheelDownTime() const {
        return std::chrono::steady_clock::time_point{};
    }

    // Stats
    virtual size_t GetDeviceCount() const { return 0; }
    virtual size_t GetGrabbedDeviceCount() const { return 0; }

    // Factory
    static std::unique_ptr<InputBackend> Create(InputBackendType type);
    static InputBackendType DetectBestBackend();

protected:
    std::function<void(const KeyEvent &)> keyCallback_;
    std::function<void(const MouseEvent &)> mouseCallback_;
};

class EvdevAdapter;
class X11Adapter;
class WaylandAdapter;
class WindowsAdapter;

}
