#include "InputBackend.hpp"
#include "KeyMap.hpp"
#include "UinputDevice.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <mutex>
#include <poll.h>
#include <shared_mutex>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <thread>
#include <unistd.h>

namespace havel {

class EvdevAdapter : public InputBackend {
public:
    EvdevAdapter();
    ~EvdevAdapter() override;

    InputBackendType GetType() const override { return InputBackendType::Evdev; }
    std::string GetName() const override { return "evdev"; }

    bool Init() override;
    void Shutdown() override;

    bool IsInitialized() const override { return initialized_; }

    std::vector<DeviceInfo> EnumerateDevices() override;
    bool OpenDevice(const std::string &path) override;
    void CloseDevice(const std::string &path) override;

    bool GrabDevice(const std::string &path) override;
    void UngrabDevice(const std::string &path) override;
    void UngrabAllDevices() override;

    int GetPollFd() const override;
    bool PollEvents(int timeoutMs) override;

    std::pair<int, int> GetMousePosition() const override;
    bool GetKeyState(uint32_t code) const override;
    uint32_t GetModifiers() const override;

    bool SupportsGrab() const override { return true; }
    bool SupportsSynthesis() const override { return true; }

    bool SendKeyEvent(uint32_t code, bool down) override;
    bool SendMouseEvent(const MouseEvent &event) override;

    // Sensitivity settings
    void SetMouseSensitivity(double sens) override;
    double GetMouseSensitivity() const override { return mouseSensitivity_; }
    void SetScrollSpeed(double speed) override;
    double GetScrollSpeed() const override { return scrollSpeed_; }

    // Key remapping (passes through to EventListener)
    void SetKeyRemap(uint32_t from, uint32_t to) override;
    void RemoveKeyRemap(uint32_t from) override;

    // Event batching for reduced syscall overhead
    void BeginBatch() override;
    void QueueEvent(int type, int code, int value) override;
    void EndBatch() override;

    // Key management
    void ReleaseAllKeys() override;
    void ReleaseAllVirtualKeys() override;
    void EmergencyReleaseAllKeys() override;

    // Input blocking
    void SetBlockInput(bool block) override;
    bool IsInputBlocked() const override { return blockInput_.load(); }

    // Emergency shutdown
    void SetEmergencyShutdownKey(uint32_t code) override;
    uint32_t GetEmergencyShutdownKey() const override { return emergencyShutdownKey_; }

    // Active input tracking
    const std::unordered_map<uint32_t, ActiveInput>& GetActiveInputs() const override { return activeInputs_; }
    std::string GetActiveInputsString() const override;

    // Modifier state
    havel::ModifierState GetModifierState() const override;
    int GetCurrentModifiersMask() const override;

    // Physical key state checking
    bool ArePhysicalKeysPressed(const std::vector<uint32_t> &keys) const override;

    // Mouse button state
    bool GetMouseButtonState(uint32_t button) const override;

    // Key down time tracking
    std::chrono::steady_clock::time_point GetKeyDownTime(uint32_t code) const override;

    // Callbacks - these feed into EventListener
    void SetKeyDownCallback(KeyCallback cb) override { keyDownCallback_ = std::move(cb); }
    void SetKeyUpCallback(KeyCallback cb) override { keyUpCallback_ = std::move(cb); }
    void SetAnyKeyPressCallback(AnyKeyPressCallback cb) override { anyKeyPressCallback_ = std::move(cb); }
    void SetMouseMovementCallback(MouseMovementCallback cb) override { mouseMovementCallback_ = std::move(cb); }
    void SetInputNotificationCallback(std::function<void()> cb) override { inputNotificationCallback_ = std::move(cb); }

    void SetInputEventCallback(std::function<void(const InputEvent &)> cb) override { inputEventCallback_ = std::move(cb); }
    void SetInputBlockCallback(std::function<bool(const InputEvent &)> cb) override { inputBlockCallback_ = std::move(cb); }

    // Combo hotkey time window
    void SetComboTimeWindow(int ms) override { comboTimeWindow_ = ms; }
    int GetComboTimeWindow() const override { return comboTimeWindow_; }

    // Wheel event tracking
    std::chrono::steady_clock::time_point GetLastWheelUpTime() const override { return lastWheelUpTime_; }
    std::chrono::steady_clock::time_point GetLastWheelDownTime() const override { return lastWheelDownTime_; }

    // Stats
    size_t GetDeviceCount() const override { return devices_.size(); }
    size_t GetGrabbedDeviceCount() const override { return grabbedFds_.size(); }

private:
    struct Device {
        std::string path;
        std::string name;
        int fd = -1;
        bool grabbed = false;
        uint32_t capabilities = 0;
    };

    enum Capability : uint32_t {
        CAP_KEYBOARD = 1 << 0,
        CAP_MOUSE = 1 << 1,
        CAP_TOUCHPAD = 1 << 2,
        CAP_JOYSTICK = 1 << 3,
        CAP_TABLET = 1 << 4,
    };

    void ProcessEvent(Device &dev, const input_event &ev);
    void ProcessKeyEvent(Device &dev, const input_event &ev);
    void ProcessRelativeEvent(Device &dev, const input_event &ev);
    void ProcessAbsoluteEvent(Device &dev, const input_event &ev);
    void ProcessMouseButtonEvent(Device &dev, const input_event &ev);

    void UpdateModifiers(uint32_t code, bool down);
    void UpdateModifierState(uint32_t code, bool down);
    uint32_t RemapKey(uint32_t code, bool down);

    void QueryDeviceCapabilities(Device &dev);
    void ReleasePressedKeys(Device &dev);
    void DrainDeviceEvents(Device &dev);

    void SendUinputEvent(int type, int code, int value);
    void SendSync();

    bool SetupUinput();
    void DestroyUinput();

    bool initialized_ = false;
    std::vector<Device> devices_;
    std::unordered_set<int> grabbedFds_;
    mutable std::mutex devicesMutex_;
    mutable std::shared_mutex stateMutex_;

    // Key state tracking (raw state, no hotkey logic)
    std::unordered_map<uint32_t, bool> keyStates_;
    std::unordered_map<uint32_t, bool> buttonStates_;
    std::unordered_map<uint32_t, bool> physicalKeyStates_;
    std::unordered_map<uint32_t, ActiveInput> activeInputs_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> keyDownTime_;
    havel::ModifierState modifierState_;
    uint32_t modifiers_ = 0;

    // Mouse position
    int32_t mouseX_ = 0;
    int32_t mouseY_ = 0;

    // Key remapping (for event synthesis)
    std::unordered_map<uint32_t, uint32_t> keyRemaps_;
    std::unordered_map<uint32_t, uint32_t> activeRemaps_;
    mutable std::mutex remapMutex_;

    // Sensitivity
    double mouseSensitivity_ = 1.0;
    double scrollSpeed_ = 1.0;

    // Input blocking
    std::atomic<bool> blockInput_{false};

    // Emergency shutdown
    uint32_t emergencyShutdownKey_ = 0;

    // Uinput for event synthesis
    std::unique_ptr<UinputDevice> uinput_;
    std::vector<input_event> batchBuffer_;
    bool batching_ = false;
    static constexpr size_t MAX_BATCH_SIZE = 16;

    // Track pressed virtual keys
    std::unordered_set<uint32_t> pressedVirtualKeys_;

    // Wheel tracking
    std::chrono::steady_clock::time_point lastWheelUpTime_{};
    std::chrono::steady_clock::time_point lastWheelDownTime_{};

    // Combo time window (passed through)
    int comboTimeWindow_ = 0;

    // Shutdown coordination
    int shutdownFd_ = -1;
    std::atomic<bool> running_{false};

    // Callbacks - feed to EventListener
    KeyCallback keyDownCallback_;
    KeyCallback keyUpCallback_;
    AnyKeyPressCallback anyKeyPressCallback_;
    MouseMovementCallback mouseMovementCallback_;
    std::function<void()> inputNotificationCallback_;
    std::function<void(const InputEvent &)> inputEventCallback_;
    std::function<bool(const InputEvent &)> inputBlockCallback_;
};

EvdevAdapter::EvdevAdapter() {
    shutdownFd_ = eventfd(0, EFD_NONBLOCK);
}

EvdevAdapter::~EvdevAdapter() {
    Shutdown();
    if (shutdownFd_ >= 0) {
        close(shutdownFd_);
    }
}

bool EvdevAdapter::Init() {
    if (initialized_) return true;

    if (!SetupUinput()) {
        warn("EvdevAdapter: uinput not available, event synthesis disabled");
    }

    running_ = true;
    initialized_ = true;
    debug("EvdevAdapter: Initialized");
    return true;
}

void EvdevAdapter::Shutdown() {
    running_ = false;

    ReleaseAllVirtualKeys();
    UngrabAllDevices();

    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        for (auto &dev : devices_) {
            if (dev.fd >= 0) {
                close(dev.fd);
                dev.fd = -1;
            }
        }
        devices_.clear();
    }

    DestroyUinput();
    initialized_ = false;
}

std::vector<DeviceInfo> EvdevAdapter::EnumerateDevices() {
    std::vector<DeviceInfo> result;
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        error("EvdevAdapter: Cannot open /dev/input: {}", strerror(errno));
        return result;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        close(fd);

        DeviceInfo info;
        info.path = path;
        info.name = name;
        info.backend = InputBackendType::Evdev;
        result.push_back(info);
    }

    closedir(dir);
    return result;
}

bool EvdevAdapter::OpenDevice(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        error("EvdevAdapter: Cannot open {}: {}", path, strerror(errno));
        return false;
    }

    char name[256] = "Unknown";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);

    Device dev;
    dev.path = path;
    dev.name = name;
    dev.fd = fd;
    QueryDeviceCapabilities(dev);
    DrainDeviceEvents(dev);

    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        devices_.push_back(std::move(dev));
    }

    debug("EvdevAdapter: Opened device {} ({})", name, path);
    return true;
}

void EvdevAdapter::CloseDevice(const std::string &path) {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    auto it = std::find_if(devices_.begin(), devices_.end(),
        [&](const Device &d) { return d.path == path; });
    if (it != devices_.end()) {
        if (it->grabbed) {
            ioctl(it->fd, EVIOCGRAB, 0);
            grabbedFds_.erase(it->fd);
        }
        if (it->fd >= 0) close(it->fd);
        devices_.erase(it);
    }
}

bool EvdevAdapter::GrabDevice(const std::string &path) {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    auto it = std::find_if(devices_.begin(), devices_.end(),
        [&](const Device &d) { return d.path == path; });
    if (it == devices_.end() || it->fd < 0) return false;

    if (ioctl(it->fd, EVIOCGRAB, 1) < 0) {
        error("EvdevAdapter: Failed to grab {}: {}", path, strerror(errno));
        return false;
    }

    it->grabbed = true;
    grabbedFds_.insert(it->fd);

    ReleasePressedKeys(*it);
    DrainDeviceEvents(*it);

    debug("EvdevAdapter: Grabbed device {}", path);
    return true;
}

void EvdevAdapter::UngrabDevice(const std::string &path) {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    auto it = std::find_if(devices_.begin(), devices_.end(),
        [&](const Device &d) { return d.path == path; });
    if (it != devices_.end() && it->grabbed) {
        ioctl(it->fd, EVIOCGRAB, 0);
        it->grabbed = false;
        grabbedFds_.erase(it->fd);
    }
}

void EvdevAdapter::UngrabAllDevices() {
    std::lock_guard<std::mutex> lock(devicesMutex_);
    for (auto &dev : devices_) {
        if (dev.grabbed && dev.fd >= 0) {
            ioctl(dev.fd, EVIOCGRAB, 0);
            dev.grabbed = false;
        }
    }
    grabbedFds_.clear();
}

int EvdevAdapter::GetPollFd() const {
    return shutdownFd_;
}

bool EvdevAdapter::PollEvents(int timeoutMs) {
    std::vector<struct pollfd> pfds;

    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        pfds.reserve(devices_.size() + 1);
        for (const auto &dev : devices_) {
            if (dev.fd >= 0) {
                pfds.push_back({.fd = dev.fd, .events = POLLIN, .revents = 0});
            }
        }
    }

    if (shutdownFd_ >= 0) {
        pfds.push_back({.fd = shutdownFd_, .events = POLLIN, .revents = 0});
    }

    if (pfds.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return false;
    }

    int ret = poll(pfds.data(), pfds.size(), timeoutMs);
    if (ret <= 0) return false;

    std::vector<std::pair<size_t, input_event>> events;

    {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        for (size_t i = 0; i < pfds.size() && i < devices_.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            if (devices_[i].fd < 0) continue;

            input_event ev;
            while (read(devices_[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                events.emplace_back(i, ev);
            }
        }
    }

    for (auto &[idx, ev] : events) {
        std::lock_guard<std::mutex> lock(devicesMutex_);
        if (idx < devices_.size()) {
            ProcessEvent(devices_[idx], ev);
        }
    }

    return true;
}

std::pair<int, int> EvdevAdapter::GetMousePosition() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return {mouseX_, mouseY_};
}

bool EvdevAdapter::GetKeyState(uint32_t code) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = keyStates_.find(code);
    return it != keyStates_.end() && it->second;
}

uint32_t EvdevAdapter::GetModifiers() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return modifiers_;
}

bool EvdevAdapter::SendKeyEvent(uint32_t code, bool down) {
    if (!uinput_) return false;

    uinput_->SendEvent(EV_KEY, code, down ? 1 : 0);
    uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);

    if (down) {
        pressedVirtualKeys_.insert(code);
    } else {
        pressedVirtualKeys_.erase(code);
    }

    return true;
}

bool EvdevAdapter::SendMouseEvent(const MouseEvent &event) {
    if (!uinput_) return false;

    switch (event.type) {
        case MouseEvent::Type::Move:
            uinput_->SendEvent(EV_REL, REL_X, event.dx);
            uinput_->SendEvent(EV_REL, REL_Y, event.dy);
            break;
        case MouseEvent::Type::Wheel:
            uinput_->SendEvent(EV_REL, REL_WHEEL, event.wheel);
            break;
        case MouseEvent::Type::Button:
            uinput_->SendEvent(EV_KEY, event.button, event.down ? 1 : 0);
            break;
        case MouseEvent::Type::Absolute:
            uinput_->SendEvent(EV_ABS, ABS_X, event.absoluteX);
            uinput_->SendEvent(EV_ABS, ABS_Y, event.absoluteY);
            break;
    }
    uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
    return true;
}

void EvdevAdapter::SetMouseSensitivity(double sens) {
    mouseSensitivity_ = sens;
}

void EvdevAdapter::SetScrollSpeed(double speed) {
    scrollSpeed_ = speed;
}

void EvdevAdapter::SetKeyRemap(uint32_t from, uint32_t to) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    keyRemaps_[from] = to;
}

void EvdevAdapter::RemoveKeyRemap(uint32_t from) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    keyRemaps_.erase(from);
}

void EvdevAdapter::BeginBatch() {
    batching_ = true;
    batchBuffer_.clear();
}

void EvdevAdapter::QueueEvent(int type, int code, int value) {
    if (!batching_ || !uinput_) return;
    input_event ev = {};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    batchBuffer_.push_back(ev);

    if (batchBuffer_.size() >= MAX_BATCH_SIZE) {
        EndBatch();
    }
}

void EvdevAdapter::EndBatch() {
    if (!batching_ || !uinput_) return;
    batchBuffer_.push_back({.type = EV_SYN, .code = SYN_REPORT, .value = 0});
    for (const auto &ev : batchBuffer_) {
        uinput_->SendEvent(ev.type, ev.code, ev.value);
    }
    batching_ = false;
    batchBuffer_.clear();
}

void EvdevAdapter::ReleaseAllKeys() {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (auto &[code, pressed] : keyStates_) {
        if (pressed && uinput_) {
            uinput_->SendEvent(EV_KEY, code, 0);
        }
    }
    if (uinput_) {
        uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
    }
}

void EvdevAdapter::ReleaseAllVirtualKeys() {
    if (!uinput_) return;

    for (uint32_t code : pressedVirtualKeys_) {
        uinput_->SendEvent(EV_KEY, code, 0);
    }
    uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
    pressedVirtualKeys_.clear();
}

void EvdevAdapter::EmergencyReleaseAllKeys() {
    if (uinput_) {
        uinput_->ReleaseAllKeys();
    }
    pressedVirtualKeys_.clear();
}

void EvdevAdapter::SetBlockInput(bool block) {
    blockInput_.store(block);
}

void EvdevAdapter::SetEmergencyShutdownKey(uint32_t code) {
    emergencyShutdownKey_ = code;
}

std::string EvdevAdapter::GetActiveInputsString() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    if (activeInputs_.empty()) return "[none]";

    std::string result;
    for (const auto &[code, input] : activeInputs_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - input.timestamp).count();
        result += std::to_string(code) + "(mods:0x" +
            std::to_string(input.modifiers) + ", " +
            std::to_string(elapsed) + "ms) ";
    }
    return result;
}

havel::ModifierState EvdevAdapter::GetModifierState() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return modifierState_;
}

int EvdevAdapter::GetCurrentModifiersMask() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    int mask = 0;
    if (modifierState_.IsCtrlPressed()) mask |= 1;
    if (modifierState_.IsShiftPressed()) mask |= 2;
    if (modifierState_.IsAltPressed()) mask |= 4;
    if (modifierState_.IsMetaPressed()) mask |= 8;
    return mask;
}

bool EvdevAdapter::ArePhysicalKeysPressed(const std::vector<uint32_t> &keys) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (uint32_t code : keys) {
        auto it = physicalKeyStates_.find(code);
        if (it == physicalKeyStates_.end() || !it->second) {
            return false;
        }
    }
    return true;
}

bool EvdevAdapter::GetMouseButtonState(uint32_t button) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = buttonStates_.find(button);
    return it != buttonStates_.end() && it->second;
}

std::chrono::steady_clock::time_point EvdevAdapter::GetKeyDownTime(uint32_t code) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = keyDownTime_.find(code);
    return it != keyDownTime_.end() ? it->second : std::chrono::steady_clock::time_point{};
}

void EvdevAdapter::ProcessEvent(Device &dev, const input_event &ev) {
    switch (ev.type) {
        case EV_KEY:
            if (dev.capabilities & CAP_MOUSE && ev.code >= BTN_MOUSE && ev.code < BTN_JOYSTICK) {
                ProcessMouseButtonEvent(dev, ev);
            } else if (dev.capabilities & CAP_KEYBOARD || ev.code < BTN_MOUSE) {
                ProcessKeyEvent(dev, ev);
            }
            break;
        case EV_REL:
            ProcessRelativeEvent(dev, ev);
            break;
        case EV_ABS:
            ProcessAbsoluteEvent(dev, ev);
            break;
    }
}

void EvdevAdapter::ProcessKeyEvent(Device &dev, const input_event &ev) {
    auto now = std::chrono::steady_clock::now();
    bool down = (ev.value == 1);
    bool repeat = (ev.value == 2);
    uint32_t originalCode = ev.code;
    uint32_t mappedCode = RemapKey(originalCode, down);

    // Emergency shutdown check
    if (down && emergencyShutdownKey_ != 0 && originalCode == emergencyShutdownKey_) {
        error("EMERGENCY SHUTDOWN KEY TRIGGERED!");
        running_ = false;
        if (shutdownFd_ >= 0) {
            uint64_t val = 1;
            write(shutdownFd_, &val, sizeof(val));
        }
        EmergencyReleaseAllKeys();
        return;
    }

    // Update state
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        keyStates_[originalCode] = down;
        physicalKeyStates_[originalCode] = down;
        UpdateModifiers(mappedCode, down);
        UpdateModifierState(mappedCode, down);

        if (down) {
            keyDownTime_[originalCode] = now;
            activeInputs_[originalCode] = ActiveInput(modifiers_, now);
        } else {
            keyDownTime_.erase(originalCode);
            activeInputs_.erase(originalCode);
        }
    }

    // Fire callbacks - EventListener will handle hotkey logic
    if (keyDownCallback_ && down) {
        keyDownCallback_(originalCode);
    } else if (keyUpCallback_ && !down) {
        keyUpCallback_(originalCode);
    }

    if (anyKeyPressCallback_ && down && !repeat) {
        anyKeyPressCallback_(KeyMap::EvdevToString(originalCode));
    }

    // Build and emit input event for EventListener
    if (keyCallback_) {
        KeyEvent event;
        event.code = originalCode;
        event.down = down;
        event.repeat = repeat;
        event.modifiers = modifiers_;
        event.keyName = KeyMap::EvdevToString(originalCode);
        event.timestamp = now;
        keyCallback_(event);
    }

    // Build InputEvent for block checking
    InputEvent inputEvent;
    inputEvent.kind = InputEventKind::Key;
    inputEvent.code = originalCode;
    inputEvent.value = ev.value;
    inputEvent.down = down;
    inputEvent.repeat = repeat;
    inputEvent.originalCode = originalCode;
    inputEvent.mappedCode = mappedCode;
    inputEvent.keyName = KeyMap::EvdevToString(originalCode);
    inputEvent.modifiers = modifiers_;

    if (inputEventCallback_) {
        inputEventCallback_(inputEvent);
    }

    // Forward to uinput unless blocked
    bool shouldBlock = blockInput_.load() || 
        (inputBlockCallback_ && inputBlockCallback_(inputEvent));

    if (!shouldBlock) {
        SendUinputEvent(EV_KEY, mappedCode, ev.value);
        SendSync();
    }
}

void EvdevAdapter::ProcessRelativeEvent(Device &dev, const input_event &ev) {
    auto now = std::chrono::steady_clock::now();

    if (ev.code == REL_X || ev.code == REL_Y) {
        int32_t scaled = static_cast<int32_t>(ev.value * mouseSensitivity_);

        {
            std::unique_lock<std::shared_mutex> lock(stateMutex_);
            if (ev.code == REL_X) mouseX_ += scaled;
            else mouseY_ += scaled;
        }

        if (mouseMovementCallback_) {
            mouseMovementCallback_(
                ev.code == REL_X ? scaled : 0,
                ev.code == REL_Y ? scaled : 0
            );
        }

        if (mouseCallback_) {
            MouseEvent event;
            event.type = MouseEvent::Type::Move;
            event.modifiers = modifiers_;
            event.timestamp = now;
            if (ev.code == REL_X) event.dx = scaled;
            else event.dy = scaled;
            mouseCallback_(event);
        }

        if (!blockInput_.load()) {
            SendUinputEvent(EV_REL, ev.code, scaled);
            SendSync();
        }

    } else if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
        int32_t scaled = static_cast<int32_t>(ev.value * scrollSpeed_);
        if (scaled == 0 && ev.value != 0) scaled = (ev.value > 0) ? 1 : -1;

        {
            std::unique_lock<std::shared_mutex> lock(stateMutex_);
            if (ev.value > 0) lastWheelUpTime_ = now;
            else lastWheelDownTime_ = now;
        }

        if (mouseCallback_) {
            MouseEvent event;
            event.type = MouseEvent::Type::Wheel;
            event.wheel = scaled;
            event.modifiers = modifiers_;
            event.timestamp = now;
            mouseCallback_(event);
        }

        if (!blockInput_.load()) {
            SendUinputEvent(EV_REL, ev.code, scaled);
            SendSync();
        }
    } else {
        if (!blockInput_.load()) {
            SendUinputEvent(EV_REL, ev.code, ev.value);
            SendSync();
        }
    }
}

void EvdevAdapter::ProcessAbsoluteEvent(Device &dev, const input_event &ev) {
    auto now = std::chrono::steady_clock::now();

    if (ev.code == ABS_X || ev.code == ABS_Y) {
        {
            std::unique_lock<std::shared_mutex> lock(stateMutex_);
            if (ev.code == ABS_X) mouseX_ = ev.value;
            else mouseY_ = ev.value;
        }

        if (mouseCallback_) {
            MouseEvent event;
            event.type = MouseEvent::Type::Absolute;
            event.modifiers = modifiers_;
            event.timestamp = now;
            if (ev.code == ABS_X) event.absoluteX = ev.value;
            else event.absoluteY = ev.value;
            mouseCallback_(event);
        }

        if (!blockInput_.load()) {
            SendUinputEvent(EV_ABS, ev.code, ev.value);
            SendSync();
        }
    }
}

void EvdevAdapter::ProcessMouseButtonEvent(Device &dev, const input_event &ev) {
    auto now = std::chrono::steady_clock::now();
    bool down = (ev.value == 1 || ev.value == 2);

    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        buttonStates_[ev.code] = down;
        physicalKeyStates_[ev.code] = down;

        if (down) {
            activeInputs_[ev.code] = ActiveInput(modifiers_, now);
        } else {
            activeInputs_.erase(ev.code);
        }
    }

    if (mouseCallback_) {
        MouseEvent event;
        event.type = MouseEvent::Type::Button;
        event.button = ev.code;
        event.down = down;
        event.modifiers = modifiers_;
        event.timestamp = now;
        mouseCallback_(event);
    }

    if (!blockInput_.load()) {
        SendUinputEvent(EV_KEY, ev.code, ev.value);
        SendSync();
    } else if (!down) {
        // Always release to prevent stuck buttons
        SendUinputEvent(EV_KEY, ev.code, 0);
        SendSync();
    }
}

void EvdevAdapter::UpdateModifiers(uint32_t code, bool down) {
    uint32_t mask = 0;
    switch (code) {
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL: mask = 1 << 0; break;
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT: mask = 1 << 1; break;
        case KEY_LEFTALT:
        case KEY_RIGHTALT: mask = 1 << 2; break;
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA: mask = 1 << 3; break;
        default: return;
    }

    if (down) modifiers_ |= mask;
    else modifiers_ &= ~mask;
}

void EvdevAdapter::UpdateModifierState(uint32_t code, bool down) {
    switch (code) {
        case KEY_LEFTCTRL: modifierState_.leftCtrl = down; break;
        case KEY_RIGHTCTRL: modifierState_.rightCtrl = down; break;
        case KEY_LEFTSHIFT: modifierState_.leftShift = down; break;
        case KEY_RIGHTSHIFT: modifierState_.rightShift = down; break;
        case KEY_LEFTALT: modifierState_.leftAlt = down; break;
        case KEY_RIGHTALT: modifierState_.rightAlt = down; break;
        case KEY_LEFTMETA: modifierState_.leftMeta = down; break;
        case KEY_RIGHTMETA: modifierState_.rightMeta = down; break;
    }
}

uint32_t EvdevAdapter::RemapKey(uint32_t code, bool down) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    if (down) {
        auto it = keyRemaps_.find(code);
        if (it != keyRemaps_.end()) {
            activeRemaps_[code] = it->second;
            return it->second;
        }
        activeRemaps_[code] = code;
    } else {
        auto it = activeRemaps_.find(code);
        if (it != activeRemaps_.end()) {
            uint32_t mapped = it->second;
            activeRemaps_.erase(it);
            return mapped;
        }
    }
    return code;
}

void EvdevAdapter::QueryDeviceCapabilities(Device &dev) {
    unsigned long evBits[(EV_MAX + 31) / 32] = {};
    if (ioctl(dev.fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0) return;

    if (evBits[EV_KEY / 32] & (1 << (EV_KEY % 32))) {
        unsigned long keyBits[(KEY_MAX + 31) / 32] = {};
        if (ioctl(dev.fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0) {
            bool hasAlpha = false, hasMouse = false;
            for (int k = KEY_A; k <= KEY_Z; ++k) {
                if (keyBits[k / 32] & (1 << (k % 32))) hasAlpha = true;
            }
            if (keyBits[BTN_LEFT / 32] & (1 << (BTN_LEFT % 32))) hasMouse = true;

            if (hasAlpha) dev.capabilities |= CAP_KEYBOARD;
            if (hasMouse) dev.capabilities |= CAP_MOUSE;
        }
    }

    if (evBits[EV_REL / 32] & (1 << (EV_REL % 32))) {
        dev.capabilities |= CAP_MOUSE;
    }

    if (evBits[EV_ABS / 32] & (1 << (EV_ABS % 32))) {
        dev.capabilities |= CAP_JOYSTICK;
    }
}

void EvdevAdapter::ReleasePressedKeys(Device &dev) {
    uint8_t keyBits[(KEY_MAX + 7) / 8] = {};
    if (ioctl(dev.fd, EVIOCGKEY(sizeof(keyBits)), keyBits) < 0) return;

    for (int key = 0; key < KEY_MAX; ++key) {
        if (keyBits[key / 8] & (1 << (key % 8))) {
            input_event ev = {};
            ev.type = EV_KEY;
            ev.code = key;
            ev.value = 0;
            write(dev.fd, &ev, sizeof(ev));
        }
    }

    input_event sync = {};
    sync.type = EV_SYN;
    sync.code = SYN_REPORT;
    write(dev.fd, &sync, sizeof(sync));
}

void EvdevAdapter::DrainDeviceEvents(Device &dev) {
    input_event ev;
    while (read(dev.fd, &ev, sizeof(ev)) == sizeof(ev)) {}
}

bool EvdevAdapter::SetupUinput() {
    uinput_ = std::make_unique<UinputDevice>();
    if (!uinput_->Setup()) {
        uinput_.reset();
        return false;
    }
    return true;
}

void EvdevAdapter::DestroyUinput() {
    if (uinput_) {
        uinput_->ReleaseAllKeys();
        uinput_.reset();
    }
}

void EvdevAdapter::SendUinputEvent(int type, int code, int value) {
    if (uinput_) {
        uinput_->SendEvent(type, code, value);
    }
}

void EvdevAdapter::SendSync() {
    if (uinput_) {
        uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
    }
}

std::unique_ptr<InputBackend> havel::CreateEvdevAdapter() {
    return std::make_unique<EvdevAdapter>();
}

}
