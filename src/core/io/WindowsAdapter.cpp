#include "InputBackend.hpp"
#include "KeyMap.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <shared_mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace havel {

class WindowsAdapter : public InputBackend {
public:
    WindowsAdapter();
    ~WindowsAdapter() override;

    InputBackendType GetType() const override { return InputBackendType::Windows; }
    std::string GetName() const override { return "windows"; }

    bool Init() override;
    void Shutdown() override;

    bool IsInitialized() const override { return initialized_; }

    std::vector<DeviceInfo> EnumerateDevices() override;
    bool OpenDevice(const std::string &path) override;
    void CloseDevice(const std::string &path) override;

    bool GrabDevice(const std::string &path) override;
    void UngrabDevice(const std::string &path) override;
    void UngrabAllDevices() override;

    int GetPollFd() const override { return -1; }
    bool PollEvents(int timeoutMs) override;

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
    void ReleaseAllVirtualKeys() override;
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
    size_t GetGrabbedDeviceCount() const override { return grabbed_ ? 1 : 0; }

private:
#ifdef _WIN32
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    void ProcessKeyEvent(KBDLLHOOKSTRUCT *kb, bool down);
    void ProcessMouseEvent(MSLLHOOKSTRUCT *ms, unsigned int message);
    uint32_t VkToEvdev(unsigned int vk) const;
    unsigned int EvdevToVk(uint32_t evdev) const;
#endif

    bool initialized_ = false;
    bool grabbed_ = false;

#ifdef _WIN32
    static WindowsAdapter *instance_;
    HHOOK keyboardHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
#endif

    int shutdownFd_ = -1;
    bool blockInput_ = false;
    uint32_t emergencyShutdownKey_ = 0;

    mutable std::shared_mutex stateMutex_;

    std::unordered_map<uint32_t, bool> keyStates_;
    std::unordered_map<uint32_t, bool> buttonStates_;
    std::unordered_map<uint32_t, ActiveInput> activeInputs_;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> keyDownTime_;
    havel::ModifierState modifierState_;
    uint32_t modifiers_ = 0;
    int32_t mouseX_ = 0;
    int32_t mouseY_ = 0;

    std::unordered_map<uint32_t, uint32_t> keyRemaps_;
    std::unordered_map<uint32_t, uint32_t> activeRemaps_;
    mutable std::mutex remapMutex_;

    double mouseSensitivity_ = 1.0;
    double scrollSpeed_ = 1.0;
    int comboTimeWindow_ = 0;
    std::chrono::steady_clock::time_point lastWheelUpTime_{};
    std::chrono::steady_clock::time_point lastWheelDownTime_{};

    KeyCallback keyDownCallback_;
    KeyCallback keyUpCallback_;
    AnyKeyPressCallback anyKeyPressCallback_;
    MouseMovementCallback mouseMovementCallback_;
    std::function<void()> inputNotificationCallback_;
    std::function<void(const InputEvent &)> inputEventCallback_;
    std::function<bool(const InputEvent &)> inputBlockCallback_;
};

#ifdef _WIN32
WindowsAdapter *WindowsAdapter::instance_ = nullptr;
#endif

WindowsAdapter::WindowsAdapter() {
#ifdef _WIN32
    instance_ = this;
#endif
}

WindowsAdapter::~WindowsAdapter() {
    Shutdown();
#ifdef _WIN32
    instance_ = nullptr;
#endif
}

bool WindowsAdapter::Init() {
#ifdef _WIN32
    if (initialized_) return true;

    keyboardHook_ = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
        GetModuleHandle(nullptr), 0);
    if (!keyboardHook_) {
        error("WindowsAdapter: Failed to install keyboard hook");
        return false;
    }

    mouseHook_ = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc,
        GetModuleHandle(nullptr), 0);
    if (!mouseHook_) {
        error("WindowsAdapter: Failed to install mouse hook");
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
        return false;
    }

    initialized_ = true;
    debug("WindowsAdapter: Initialized");
    return true;
#else
    return false;
#endif
}

void WindowsAdapter::Shutdown() {
#ifdef _WIN32
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
    }
#endif
    initialized_ = false;
}

std::vector<DeviceInfo> WindowsAdapter::EnumerateDevices() {
    std::vector<DeviceInfo> result;
#ifdef _WIN32
    DeviceInfo info;
    info.path = "windows";
    info.name = "Windows Input System";
    info.backend = InputBackendType::Windows;
    result.push_back(info);
#endif
    return result;
}

bool WindowsAdapter::OpenDevice(const std::string &path) {
    (void)path;
    return Init();
}

void WindowsAdapter::CloseDevice(const std::string &path) {
    (void)path;
    Shutdown();
}

bool WindowsAdapter::GrabDevice(const std::string &path) {
#ifdef _WIN32
    (void)path;
    grabbed_ = true;
    debug("WindowsAdapter: Input grabbed");
    return true;
#else
    (void)path;
    return false;
#endif
}

void WindowsAdapter::UngrabDevice(const std::string &path) {
    (void)path;
    grabbed_ = false;
}

void WindowsAdapter::UngrabAllDevices() {
    grabbed_ = false;
}

bool WindowsAdapter::PollEvents(int timeoutMs) {
#ifdef _WIN32
    MSG msg;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    return false;
#endif
}

std::pair<int, int> WindowsAdapter::GetMousePosition() const {
#ifdef _WIN32
    POINT pt;
    GetCursorPos(&pt);
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    mouseX_ = pt.x;
    mouseY_ = pt.y;
    return {pt.x, pt.y};
#else
    return {0, 0};
#endif
}

bool WindowsAdapter::GetKeyState(uint32_t code) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = keyStates_.find(code);
    return it != keyStates_.end() && it->second;
}

uint32_t WindowsAdapter::GetModifiers() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return modifiers_;
}

bool WindowsAdapter::SendKeyEvent(uint32_t code, bool down) {
#ifdef _WIN32
    unsigned int vk = EvdevToVk(code);
    if (vk == 0) return false;

    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));
    return true;
#else
    (void)code;
    (void)down;
    return false;
#endif
}

bool WindowsAdapter::SendMouseEvent(const MouseEvent &event) {
#ifdef _WIN32
    INPUT input = {};

    switch (event.type) {
        case MouseEvent::Type::Move: {
            POINT pt;
            GetCursorPos(&pt);
            pt.x += event.dx;
            pt.y += event.dy;
            SetCursorPos(pt.x, pt.y);
            return true;
        }

        case MouseEvent::Type::Button: {
            input.type = INPUT_MOUSE;
            DWORD flag = 0;
            switch (event.button) {
                case BTN_LEFT: flag = event.down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
                case BTN_RIGHT: flag = event.down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
                case BTN_MIDDLE: flag = event.down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
                default: return false;
            }
            input.mi.dwFlags = flag;
            SendInput(1, &input, sizeof(INPUT));
            return true;
        }

        case MouseEvent::Type::Wheel: {
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = event.wheel * WHEEL_DELTA;
            SendInput(1, &input, sizeof(INPUT));
            return true;
        }

        case MouseEvent::Type::Absolute:
            SetCursorPos(event.absoluteX, event.absoluteY);
            return true;
    }
#endif
    return false;
}

void WindowsAdapter::SetKeyRemap(uint32_t from, uint32_t to) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    keyRemaps_[from] = to;
}

void WindowsAdapter::RemoveKeyRemap(uint32_t from) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    keyRemaps_.erase(from);
}

void WindowsAdapter::ReleaseAllKeys() {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (auto &[code, pressed] : keyStates_) {
        if (pressed) SendKeyEvent(code, false);
    }
}

void WindowsAdapter::ReleaseAllVirtualKeys() {
    ReleaseAllKeys();
}

std::string WindowsAdapter::GetActiveInputsString() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    if (activeInputs_.empty()) return "[none]";
    std::string result;
    for (auto &[code, input] : activeInputs_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - input.timestamp).count();
        result += std::to_string(code) + "(" + std::to_string(elapsed) + "ms) ";
    }
    return result;
}

havel::ModifierState WindowsAdapter::GetModifierState() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return modifierState_;
}

int WindowsAdapter::GetCurrentModifiersMask() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    int mask = 0;
    if (modifierState_.IsCtrlPressed()) mask |= 1;
    if (modifierState_.IsShiftPressed()) mask |= 2;
    if (modifierState_.IsAltPressed()) mask |= 4;
    if (modifierState_.IsMetaPressed()) mask |= 8;
    return mask;
}

bool WindowsAdapter::ArePhysicalKeysPressed(const std::vector<uint32_t> &keys) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (uint32_t code : keys) {
        auto it = keyStates_.find(code);
        if (it == keyStates_.end() || !it->second) return false;
    }
    return true;
}

bool WindowsAdapter::GetMouseButtonState(uint32_t button) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = buttonStates_.find(button);
    return it != buttonStates_.end() && it->second;
}

std::chrono::steady_clock::time_point WindowsAdapter::GetKeyDownTime(uint32_t code) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = keyDownTime_.find(code);
    return it != keyDownTime_.end() ? it->second : std::chrono::steady_clock::time_point{};
}

#ifdef _WIN32
LRESULT WindowsAdapter::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && instance_) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        instance_->ProcessKeyEvent(kb, down);

        if (instance_->grabbed_ || instance_->blockInput_) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT WindowsAdapter::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && instance_) {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;
        instance_->ProcessMouseEvent(ms, wParam);

        if (instance_->grabbed_ || instance_->blockInput_) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void WindowsAdapter::ProcessKeyEvent(KBDLLHOOKSTRUCT *kb, bool down) {
    auto now = std::chrono::steady_clock::now();
    uint32_t evdevCode = VkToEvdev(kb->vkCode);

    if (evdevCode == 0) return;

    // Apply remapping
    uint32_t mappedCode = evdevCode;
    {
        std::lock_guard<std::mutex> lock(remapMutex_);
        if (down) {
            auto it = keyRemaps_.find(evdevCode);
            if (it != keyRemaps_.end()) {
                activeRemaps_[evdevCode] = it->second;
                mappedCode = it->second;
            }
        } else {
            auto it = activeRemaps_.find(evdevCode);
            if (it != activeRemaps_.end()) {
                mappedCode = it->second;
                activeRemaps_.erase(it);
            }
        }
    }

    // Update state
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        keyStates_[evdevCode] = down;
        modifiers_ = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers_ |= (1 << 0);
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers_ |= (1 << 1);
        if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers_ |= (1 << 2);
        if (GetAsyncKeyState(VK_LWIN) || GetAsyncKeyState(VK_RWIN)) modifiers_ |= (1 << 3);

        UpdateModifierStateInternal(mappedCode, down);

        if (down) {
            keyDownTime_[evdevCode] = now;
            activeInputs_[evdevCode] = ActiveInput(modifiers_, now);
        } else {
            keyDownTime_.erase(evdevCode);
            activeInputs_.erase(evdevCode);
        }
    }

    // Fire callbacks
    if (keyDownCallback_ && down) keyDownCallback_(evdevCode);
    else if (keyUpCallback_ && !down) keyUpCallback_(evdevCode);

    if (anyKeyPressCallback_ && down) {
        anyKeyPressCallback_(KeyMap::EvdevToString(evdevCode));
    }

    if (keyCallback_) {
        KeyEvent evt;
        evt.code = evdevCode;
        evt.down = down;
        evt.repeat = false;
        evt.modifiers = modifiers_;
        evt.keyName = KeyMap::EvdevToString(evdevCode);
        evt.timestamp = now;
        keyCallback_(evt);
    }
}

void WindowsAdapter::ProcessMouseEvent(MSLLHOOKSTRUCT *ms, unsigned int message) {
    auto now = std::chrono::steady_clock::now();

    int dx = ms->pt.x - mouseX_;
    int dy = ms->pt.y - mouseY_;
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        mouseX_ = ms->pt.x;
        mouseY_ = ms->pt.y;
    }

    uint32_t evdevButton = 0;
    bool down = false;

    switch (message) {
        case WM_LBUTTONDOWN: evdevButton = BTN_LEFT; down = true; break;
        case WM_LBUTTONUP: evdevButton = BTN_LEFT; down = false; break;
        case WM_RBUTTONDOWN: evdevButton = BTN_RIGHT; down = true; break;
        case WM_RBUTTONUP: evdevButton = BTN_RIGHT; down = false; break;
        case WM_MBUTTONDOWN: evdevButton = BTN_MIDDLE; down = true; break;
        case WM_MBUTTONUP: evdevButton = BTN_MIDDLE; down = false; break;
        case WM_XBUTTONDOWN: evdevButton = BTN_SIDE; down = true; break;
        case WM_XBUTTONUP: evdevButton = BTN_SIDE; down = false; break;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(message);
            {
                std::unique_lock<std::shared_mutex> lock(stateMutex_);
                if (delta > 0) lastWheelUpTime_ = now;
                else lastWheelDownTime_ = now;
            }
            if (mouseCallback_) {
                MouseEvent evt;
                evt.type = MouseEvent::Type::Wheel;
                evt.wheel = delta / WHEEL_DELTA;
                evt.modifiers = modifiers_;
                evt.timestamp = now;
                mouseCallback_(evt);
            }
            return;
        }
        case WM_MOUSEMOVE:
            if (mouseMovementCallback_) {
                mouseMovementCallback_(dx, dy);
            }
            if (mouseCallback_) {
                MouseEvent evt;
                evt.type = MouseEvent::Type::Move;
                evt.dx = dx;
                evt.dy = dy;
                evt.modifiers = modifiers_;
                evt.timestamp = now;
                mouseCallback_(evt);
            }
            return;
        default:
            return;
    }

    if (evdevButton != 0) {
        {
            std::unique_lock<std::shared_mutex> lock(stateMutex_);
            buttonStates_[evdevButton] = down;
            if (down) {
                activeInputs_[evdevButton] = ActiveInput(modifiers_, now);
            } else {
                activeInputs_.erase(evdevButton);
            }
        }

        if (mouseCallback_) {
            MouseEvent evt;
            evt.type = MouseEvent::Type::Button;
            evt.button = evdevButton;
            evt.down = down;
            evt.modifiers = modifiers_;
            evt.timestamp = now;
            mouseCallback_(evt);
        }
    }
}

void WindowsAdapter::UpdateModifierStateInternal(uint32_t code, bool down) {
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

uint32_t WindowsAdapter::VkToEvdev(unsigned int vk) const {
    static const std::unordered_map<unsigned int, uint32_t> map = {
        {'A', KEY_A}, {'B', KEY_B}, {'C', KEY_C}, {'D', KEY_D}, {'E', KEY_E},
        {'F', KEY_F}, {'G', KEY_G}, {'H', KEY_H}, {'I', KEY_I}, {'J', KEY_J},
        {'K', KEY_K}, {'L', KEY_L}, {'M', KEY_M}, {'N', KEY_N}, {'O', KEY_O},
        {'P', KEY_P}, {'Q', KEY_Q}, {'R', KEY_R}, {'S', KEY_S}, {'T', KEY_T},
        {'U', KEY_U}, {'V', KEY_V}, {'W', KEY_W}, {'X', KEY_X}, {'Y', KEY_Y}, {'Z', KEY_Z},
        {'0', KEY_0}, {'1', KEY_1}, {'2', KEY_2}, {'3', KEY_3}, {'4', KEY_4},
        {'5', KEY_5}, {'6', KEY_6}, {'7', KEY_7}, {'8', KEY_8}, {'9', KEY_9},
        {VK_RETURN, KEY_ENTER}, {VK_TAB, KEY_TAB}, {VK_SPACE, KEY_SPACE},
        {VK_BACK, KEY_BACKSPACE}, {VK_ESCAPE, KEY_ESC},
        {VK_CONTROL, KEY_LEFTCTRL}, {VK_SHIFT, KEY_LEFTSHIFT},
        {VK_MENU, KEY_LEFTALT}, {VK_LWIN, KEY_LEFTMETA}, {VK_RWIN, KEY_RIGHTMETA},
        {VK_UP, KEY_UP}, {VK_DOWN, KEY_DOWN}, {VK_LEFT, KEY_LEFT}, {VK_RIGHT, KEY_RIGHT},
        {VK_F1, KEY_F1}, {VK_F2, KEY_F2}, {VK_F3, KEY_F3}, {VK_F4, KEY_F4},
        {VK_F5, KEY_F5}, {VK_F6, KEY_F6}, {VK_F7, KEY_F7}, {VK_F8, KEY_F8},
        {VK_F9, KEY_F9}, {VK_F10, KEY_F10}, {VK_F11, KEY_F11}, {VK_F12, KEY_F12},
        {VK_INSERT, KEY_INSERT}, {VK_DELETE, KEY_DELETE},
        {VK_HOME, KEY_HOME}, {VK_END, KEY_END},
        {VK_PRIOR, KEY_PAGEUP}, {VK_NEXT, KEY_PAGEDOWN},
        {VK_CAPITAL, KEY_CAPSLOCK}, {VK_NUMLOCK, KEY_NUMLOCK},
        {VK_SCROLL, KEY_SCROLLLOCK},
        {VK_OEM_3, KEY_GRAVE}, {VK_OEM_MINUS, KEY_MINUS}, {VK_OEM_PLUS, KEY_EQUAL},
        {VK_OEM_4, KEY_LEFTBRACE}, {VK_OEM_6, KEY_RIGHTBRACE},
        {VK_OEM_5, KEY_BACKSLASH}, {VK_OEM_1, KEY_SEMICOLON},
        {VK_OEM_7, KEY_APOSTROPHE}, {VK_OEM_COMMA, KEY_COMMA},
        {VK_OEM_PERIOD, KEY_DOT}, {VK_OEM_2, KEY_SLASH},
    };

    auto it = map.find(vk);
    return it != map.end() ? it->second : 0;
}

unsigned int WindowsAdapter::EvdevToVk(uint32_t evdev) const {
    static const std::unordered_map<uint32_t, unsigned int> map = {
        {KEY_A, 'A'}, {KEY_B, 'B'}, {KEY_C, 'C'}, {KEY_D, 'D'}, {KEY_E, 'E'},
        {KEY_F, 'F'}, {KEY_G, 'G'}, {KEY_H, 'H'}, {KEY_I, 'I'}, {KEY_J, 'J'},
        {KEY_K, 'K'}, {KEY_L, 'L'}, {KEY_M, 'M'}, {KEY_N, 'N'}, {KEY_O, 'O'},
        {KEY_P, 'P'}, {KEY_Q, 'Q'}, {KEY_R, 'R'}, {KEY_S, 'S'}, {KEY_T, 'T'},
        {KEY_U, 'U'}, {KEY_V, 'V'}, {KEY_W, 'W'}, {KEY_X, 'X'}, {KEY_Y, 'Y'}, {KEY_Z, 'Z'},
        {KEY_0, '0'}, {KEY_1, '1'}, {KEY_2, '2'}, {KEY_3, '3'}, {KEY_4, '4'},
        {KEY_5, '5'}, {KEY_6, '6'}, {KEY_7, '7'}, {KEY_8, '8'}, {KEY_9, '9'},
        {KEY_ENTER, VK_RETURN}, {KEY_TAB, VK_TAB}, {KEY_SPACE, VK_SPACE},
        {KEY_BACKSPACE, VK_BACK}, {KEY_ESC, VK_ESCAPE},
        {KEY_LEFTCTRL, VK_CONTROL}, {KEY_RIGHTCTRL, VK_CONTROL},
        {KEY_LEFTSHIFT, VK_SHIFT}, {KEY_RIGHTSHIFT, VK_SHIFT},
        {KEY_LEFTALT, VK_MENU}, {KEY_RIGHTALT, VK_MENU},
        {KEY_LEFTMETA, VK_LWIN}, {KEY_RIGHTMETA, VK_RWIN},
        {KEY_UP, VK_UP}, {KEY_DOWN, VK_DOWN}, {KEY_LEFT, VK_LEFT}, {KEY_RIGHT, VK_RIGHT},
        {KEY_F1, VK_F1}, {KEY_F2, VK_F2}, {KEY_F3, VK_F3}, {KEY_F4, VK_F4},
        {KEY_F5, VK_F5}, {KEY_F6, VK_F6}, {KEY_F7, VK_F7}, {KEY_F8, VK_F8},
        {KEY_F9, VK_F9}, {KEY_F10, VK_F10}, {KEY_F11, VK_F11}, {KEY_F12, VK_F12},
        {KEY_INSERT, VK_INSERT}, {KEY_DELETE, VK_DELETE},
        {KEY_HOME, VK_HOME}, {KEY_END, VK_END},
        {KEY_PAGEUP, VK_PRIOR}, {KEY_PAGEDOWN, VK_NEXT},
        {KEY_CAPSLOCK, VK_CAPITAL}, {KEY_NUMLOCK, VK_NUMLOCK},
        {KEY_SCROLLLOCK, VK_SCROLL},
        {KEY_GRAVE, VK_OEM_3}, {KEY_MINUS, VK_OEM_MINUS}, {KEY_EQUAL, VK_OEM_PLUS},
        {KEY_LEFTBRACE, VK_OEM_4}, {KEY_RIGHTBRACE, VK_OEM_6},
        {KEY_BACKSLASH, VK_OEM_5}, {KEY_SEMICOLON, VK_OEM_1},
        {KEY_APOSTROPHE, VK_OEM_7}, {KEY_COMMA, VK_OEM_COMMA},
        {KEY_DOT, VK_OEM_PERIOD}, {KEY_SLASH, VK_OEM_2},
    };

    auto it = map.find(evdev);
    return it != map.end() ? it->second : 0;
}
#endif

std::unique_ptr<InputBackend> havel::CreateWindowsAdapter() {
    return std::make_unique<WindowsAdapter>();
}

}
#endif

}
