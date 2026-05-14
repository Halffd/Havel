#include "InputBackend.hpp"
#include "KeyMap.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <shared_mutex>

#ifdef __linux__
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace havel {

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

    int GetPollFd() const override;
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
    void ReleaseAllVirtualKeys() override {}
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
    void GrabKey(KeyCode keycode, unsigned int modifiers);
    void UngrabKey(KeyCode keycode, unsigned int modifiers);
    void GrabButton(unsigned int button, unsigned int modifiers);
    void UngrabButton(unsigned int button, unsigned int modifiers);
    void UpdateModifierStateInternal(uint32_t code, bool down);
#endif

    bool initialized_ = false;
    bool grabbed_ = false;
    std::string displayName_;

#ifdef __linux__
    Display *display_ = nullptr;
    Window root_ = 0;
    int xfd_ = -1;
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
};

#ifdef __linux__
constexpr int XKeyPress = 2;
constexpr int XKeyRelease = 3;
constexpr int XButtonPress = 4;
constexpr int XButtonRelease = 5;
constexpr int XMotionNotify = 6;
#endif

X11Adapter::X11Adapter() {
    shutdownFd_ = eventfd(0, EFD_NONBLOCK);
}

X11Adapter::~X11Adapter() {
    Shutdown();
    if (shutdownFd_ >= 0) close(shutdownFd_);
}

bool X11Adapter::Init() {
#ifdef __linux__
    if (initialized_) return true;

    if (!XInitThreads()) {
        error("X11Adapter: XInitThreads failed");
        return false;
    }

    const char *name = displayName_.empty() ? nullptr : displayName_.c_str();
    display_ = XOpenDisplay(name);
    if (!display_) {
        error("X11Adapter: Cannot open display '{}'", displayName_.empty() ? ":0" : displayName_);
        return false;
    }

    XSync(display_, False);
    root_ = DefaultRootWindow(display_);
    xfd_ = XConnectionNumber(display_);

    XSelectInput(display_, root_,
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | ButtonMotionMask);

    BuildKeycodeMap();

    initialized_ = true;
    debug("X11Adapter: Initialized on display {}", DisplayString(display_));
    return true;
#else
    return false;
#endif
}

void X11Adapter::Shutdown() {
#ifdef __linux__
    if (display_) {
        UngrabAllDevices();
        XSync(display_, False);
        XCloseDisplay(display_);
        display_ = nullptr;
    }
#endif
    initialized_ = false;
}

std::vector<DeviceInfo> X11Adapter::EnumerateDevices() {
    std::vector<DeviceInfo> result;
#ifdef __linux__
    if (display_) {
        DeviceInfo info;
        info.path = DisplayString(display_) ? DisplayString(display_) : ":0";
        info.name = "X11 Display";
        info.fd = xfd_;
        info.backend = InputBackendType::X11;
        result.push_back(info);
    }
#endif
    return result;
}

bool X11Adapter::OpenDevice(const std::string &path) {
#ifdef __linux__
    displayName_ = path;
    return Init();
#else
    (void)path;
    return false;
#endif
}

void X11Adapter::CloseDevice(const std::string &path) {
    (void)path;
    Shutdown();
}

bool X11Adapter::GrabDevice(const std::string &path) {
#ifdef __linux__
    (void)path;
    if (!display_) return false;

    int result = XGrabKeyboard(display_, root_, True,
        GrabModeAsync, GrabModeAsync, CurrentTime);
    if (result != GrabSuccess) {
        error("X11Adapter: XGrabKeyboard failed ({})", result);
        return false;
    }

    XGrabPointer(display_, root_, True,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    XSync(display_, False);
    grabbed_ = true;
    debug("X11Adapter: Grabbed input");
    return true;
#else
    (void)path;
    return false;
#endif
}

void X11Adapter::UngrabDevice(const std::string &path) {
    (void)path;
    UngrabAllDevices();
}

void X11Adapter::UngrabAllDevices() {
#ifdef __linux__
    if (display_ && grabbed_) {
        XUngrabKeyboard(display_, CurrentTime);
        XUngrabPointer(display_, CurrentTime);
        XSync(display_, False);
        grabbed_ = false;
    }
#endif
}

int X11Adapter::GetPollFd() const {
#ifdef __linux__
    return xfd_ >= 0 ? xfd_ : shutdownFd_;
#else
    return shutdownFd_;
#endif
}

bool X11Adapter::PollEvents(int timeoutMs) {
#ifdef __linux__
    if (!display_) return false;

    struct pollfd pfd = {.fd = xfd_, .events = POLLIN, .revents = 0};
    int ret = poll(&pfd, 1, timeoutMs);
    if (ret <= 0) return false;

    if (XPending(display_) <= 0) return false;

    XEvent event;
    while (XPending(display_) > 0) {
        XNextEvent(display_, &event);
        ProcessX11Event(event);
    }
    return true;
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    return false;
#endif
}

std::pair<int, int> X11Adapter::GetMousePosition() const {
#ifdef __linux__
    if (!display_) return {0, 0};

    int x, y, dummy;
    Window rootReturn, childReturn;
    unsigned int mask;

    XQueryPointer(display_, root_, &rootReturn, &childReturn,
        &x, &y, &dummy, &dummy, &mask);

    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    mouseX_ = x;
    mouseY_ = y;
    return {x, y};
#else
    return {0, 0};
#endif
}

bool X11Adapter::GetKeyState(uint32_t code) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = keyStates_.find(code);
    return it != keyStates_.end() && it->second;
}

uint32_t X11Adapter::GetModifiers() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return modifiers_;
}

bool X11Adapter::SendKeyEvent(uint32_t code, bool down) {
#ifdef __linux__
    if (!display_) return false;

    KeyCode xk = EvdevToX11Keycode(code);
    if (xk == 0) return false;

    XKeyEvent event = {};
    event.display = display_;
    event.window = root_;
    event.root = root_;
    event.same_screen = True;
    event.keycode = xk;
    event.state = HavelModifiersToX11(modifiers_);
    event.type = down ? KeyPress : KeyRelease;

    XSendEvent(display_, root_, True, KeyPressMask | KeyReleaseMask,
        reinterpret_cast<XEvent*>(&event));
    XFlush(display_);
    return true;
#else
    (void)code;
    (void)down;
    return false;
#endif
}

bool X11Adapter::SendMouseEvent(const MouseEvent &event) {
#ifdef __linux__
    if (!display_) return false;

    switch (event.type) {
        case MouseEvent::Type::Move:
            XWarpPointer(display_, None, root_, 0, 0, 0, 0,
                mouseX_ + event.dx, mouseY_ + event.dy);
            XFlush(display_);
            {
                std::unique_lock<std::shared_mutex> lock(stateMutex_);
                mouseX_ += event.dx;
                mouseY_ += event.dy;
            }
            return true;

        case MouseEvent::Type::Button: {
            unsigned int xbutton = Button1;
            switch (event.button) {
                case BTN_LEFT: xbutton = Button1; break;
                case BTN_MIDDLE: xbutton = Button2; break;
                case BTN_RIGHT: xbutton = Button3; break;
                case BTN_SIDE: xbutton = Button4; break;
                case BTN_EXTRA: xbutton = Button5; break;
                default: return false;
            }
            XEvent xev = {};
            xev.type = event.down ? ButtonPress : ButtonRelease;
            xev.xbutton.button = xbutton;
            xev.xbutton.same_screen = True;
            xev.xbutton.root = root_;
            xev.xbutton.window = root_;
            xev.xbutton.state = HavelModifiersToX11(modifiers_);
            XSendEvent(display_, root_, True,
                ButtonPressMask | ButtonReleaseMask, &xev);
            XFlush(display_);
            return true;
        }

        case MouseEvent::Type::Wheel: {
            unsigned int button = event.wheel > 0 ? Button4 : Button5;
            XEvent xev = {};
            xev.type = ButtonPress;
            xev.xbutton.button = button;
            xev.xbutton.same_screen = True;
            xev.xbutton.root = root_;
            XSendEvent(display_, root_, True, ButtonPressMask, &xev);
            xev.type = ButtonRelease;
            XSendEvent(display_, root_, True, ButtonReleaseMask, &xev);
            XFlush(display_);
            return true;
        }

        case MouseEvent::Type::Absolute:
            XWarpPointer(display_, None, root_, 0, 0, 0, 0,
                event.absoluteX, event.absoluteY);
            XFlush(display_);
            {
                std::unique_lock<std::shared_mutex> lock(stateMutex_);
                mouseX_ = event.absoluteX;
                mouseY_ = event.absoluteY;
            }
            return true;
    }
#endif
    return false;
}

void X11Adapter::SetKeyRemap(uint32_t from, uint32_t to) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    keyRemaps_[from] = to;
}

void X11Adapter::RemoveKeyRemap(uint32_t from) {
    std::lock_guard<std::mutex> lock(remapMutex_);
    keyRemaps_.erase(from);
}

void X11Adapter::ReleaseAllKeys() {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (auto &[code, pressed] : keyStates_) {
        if (pressed) SendKeyEvent(code, false);
    }
}

std::string X11Adapter::GetActiveInputsString() const {
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

havel::ModifierState X11Adapter::GetModifierState() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    return modifierState_;
}

int X11Adapter::GetCurrentModifiersMask() const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    int mask = 0;
    if (modifierState_.IsCtrlPressed()) mask |= 1;
    if (modifierState_.IsShiftPressed()) mask |= 2;
    if (modifierState_.IsAltPressed()) mask |= 4;
    if (modifierState_.IsMetaPressed()) mask |= 8;
    return mask;
}

bool X11Adapter::ArePhysicalKeysPressed(const std::vector<uint32_t> &keys) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (uint32_t code : keys) {
        auto it = keyStates_.find(code);
        if (it == keyStates_.end() || !it->second) return false;
    }
    return true;
}

bool X11Adapter::GetMouseButtonState(uint32_t button) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = buttonStates_.find(button);
    return it != buttonStates_.end() && it->second;
}

std::chrono::steady_clock::time_point X11Adapter::GetKeyDownTime(uint32_t code) const {
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = keyDownTime_.find(code);
    return it != keyDownTime_.end() ? it->second : std::chrono::steady_clock::time_point{};
}

#ifdef __linux__
void X11Adapter::ProcessX11Event(XEvent &event) {
    auto now = std::chrono::steady_clock::now();

    switch (event.type) {
        case KeyPress:
        case KeyRelease:
            ProcessKeyEvent(event.xkey, event.type == KeyPress);
            break;

        case ButtonPress:
        case ButtonRelease:
            ProcessButtonEvent(event.xbutton, event.type == ButtonPress);
            break;

        case MotionNotify:
            ProcessMotionEvent(event.xmotion);
            break;
    }
}

void X11Adapter::ProcessKeyEvent(const XKeyEvent &ke, bool down) {
    auto now = std::chrono::steady_clock::now();
    uint32_t evdevCode = X11KeycodeToEvdev(ke.keycode);

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
        UpdateModifierStateInternal(mappedCode, down);
        modifiers_ = X11ModifiersToHavel(ke.state);

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

void X11Adapter::ProcessButtonEvent(const XButtonEvent &be, bool down) {
    auto now = std::chrono::steady_clock::now();
    uint32_t evdevButton = X11ButtonToEvdev(be.button);

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
        MouseEvent event;
        event.type = MouseEvent::Type::Button;
        event.button = evdevButton;
        event.down = down;
        event.modifiers = modifiers_;
        event.timestamp = now;
        mouseCallback_(event);
    }
}

void X11Adapter::ProcessMotionEvent(const XMotionEvent &me) {
    auto now = std::chrono::steady_clock::now();
    int dx = me.x - mouseX_;
    int dy = me.y - mouseY_;

    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        mouseX_ = me.x;
        mouseY_ = me.y;
    }

    if (mouseMovementCallback_) {
        mouseMovementCallback_(dx, dy);
    }

    if (mouseCallback_ && (dx != 0 || dy != 0)) {
        MouseEvent event;
        event.type = MouseEvent::Type::Move;
        event.dx = dx;
        event.dy = dy;
        event.modifiers = modifiers_;
        event.timestamp = now;
        mouseCallback_(event);
    }
}

void X11Adapter::UpdateModifierStateInternal(uint32_t code, bool down) {
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

void X11Adapter::BuildKeycodeMap() {
    int minCode, maxCode;
    XDisplayKeycodes(display_, &minCode, &maxCode);

    for (int kc = minCode; kc <= maxCode; ++kc) {
        KeySym ks = XKeycodeToKeysym(display_, kc, 0);
        if (ks == NoSymbol) continue;

        uint32_t evdev = 0;

        if (ks >= XK_a && ks <= XK_z) evdev = KEY_A + (ks - XK_a);
        else if (ks >= XK_A && ks <= XK_Z) evdev = KEY_A + (ks - XK_A);
        else if (ks >= XK_0 && ks <= XK_9) evdev = KEY_0 + (ks - XK_0);
        else if (ks >= XK_F1 && ks <= XK_F12) evdev = KEY_F1 + (ks - XK_F1);
        else {
            if (ks == XK_Return) evdev = KEY_ENTER;
            else if (ks == XK_Tab) evdev = KEY_TAB;
            else if (ks == XK_space) evdev = KEY_SPACE;
            else if (ks == XK_BackSpace) evdev = KEY_BACKSPACE;
            else if (ks == XK_Escape) evdev = KEY_ESC;
            else if (ks == XK_Control_L) evdev = KEY_LEFTCTRL;
            else if (ks == XK_Control_R) evdev = KEY_RIGHTCTRL;
            else if (ks == XK_Shift_L) evdev = KEY_LEFTSHIFT;
            else if (ks == XK_Shift_R) evdev = KEY_RIGHTSHIFT;
            else if (ks == XK_Alt_L) evdev = KEY_LEFTALT;
            else if (ks == XK_Alt_R) evdev = KEY_RIGHTALT;
            else if (ks == XK_Super_L) evdev = KEY_LEFTMETA;
            else if (ks == XK_Super_R) evdev = KEY_RIGHTMETA;
            else if (ks == XK_Up) evdev = KEY_UP;
            else if (ks == XK_Down) evdev = KEY_DOWN;
            else if (ks == XK_Left) evdev = KEY_LEFT;
            else if (ks == XK_Right) evdev = KEY_RIGHT;
            else if (ks == XK_Insert) evdev = KEY_INSERT;
            else if (ks == XK_Delete) evdev = KEY_DELETE;
            else if (ks == XK_Home) evdev = KEY_HOME;
            else if (ks == XK_End) evdev = KEY_END;
            else if (ks == XK_Page_Up) evdev = KEY_PAGEUP;
            else if (ks == XK_Page_Down) evdev = KEY_PAGEDOWN;
            else if (ks == XK_Caps_Lock) evdev = KEY_CAPSLOCK;
            else if (ks == XK_Num_Lock) evdev = KEY_NUMLOCK;
            else if (ks == XK_Scroll_Lock) evdev = KEY_SCROLLLOCK;
            else if (ks == XK_grave) evdev = KEY_GRAVE;
            else if (ks == XK_minus) evdev = KEY_MINUS;
            else if (ks == XK_equal) evdev = KEY_EQUAL;
            else if (ks == XK_bracketleft) evdev = KEY_LEFTBRACE;
            else if (ks == XK_bracketright) evdev = KEY_RIGHTBRACE;
            else if (ks == XK_backslash) evdev = KEY_BACKSLASH;
            else if (ks == XK_semicolon) evdev = KEY_SEMICOLON;
            else if (ks == XK_apostrophe) evdev = KEY_APOSTROPHE;
            else if (ks == XK_comma) evdev = KEY_COMMA;
            else if (ks == XK_period) evdev = KEY_DOT;
            else if (ks == XK_slash) evdev = KEY_SLASH;
        }

        if (evdev > 0) {
            keycodeToEvdev_[kc] = evdev;
            evdevToKeycode_[evdev] = kc;
        }
    }
}

uint32_t X11Adapter::X11KeycodeToEvdev(KeyCode keycode) const {
    auto it = keycodeToEvdev_.find(keycode);
    return it != keycodeToEvdev_.end() ? it->second : 0;
}

KeyCode X11Adapter::EvdevToX11Keycode(uint32_t evdev) const {
    auto it = evdevToKeycode_.find(evdev);
    return it != evdevToKeycode_.end() ? it->second : 0;
}

uint32_t X11Adapter::X11ButtonToEvdev(unsigned int button) const {
    switch (button) {
        case Button1: return BTN_LEFT;
        case Button2: return BTN_MIDDLE;
        case Button3: return BTN_RIGHT;
        case Button4: return BTN_SIDE;
        case Button5: return BTN_EXTRA;
        default: return 0;
    }
}

uint32_t X11Adapter::X11ModifiersToHavel(unsigned int state) const {
    uint32_t mods = 0;
    if (state & ControlMask) mods |= (1 << 0);
    if (state & ShiftMask) mods |= (1 << 1);
    if (state & Mod1Mask) mods |= (1 << 2);
    if (state & Mod4Mask) mods |= (1 << 3);
    return mods;
}

unsigned int X11Adapter::HavelModifiersToX11(uint32_t modifiers) const {
    unsigned int state = 0;
    if (modifiers & (1 << 0)) state |= ControlMask;
    if (modifiers & (1 << 1)) state |= ShiftMask;
    if (modifiers & (1 << 2)) state |= Mod1Mask;
    if (modifiers & (1 << 3)) state |= Mod4Mask;
    return state;
}

unsigned int X11Adapter::CleanMask(unsigned int mask) const {
    return mask & RELEVANT_MODIFIERS;
}

void X11Adapter::GrabKey(KeyCode keycode, unsigned int modifiers) {
    if (!display_) return;
    XGrabKey(display_, keycode, modifiers, root_, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display_, keycode, modifiers | Mod2Mask, root_, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display_, keycode, modifiers | LockMask, root_, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display_, keycode, modifiers | Mod2Mask | LockMask, root_, True, GrabModeAsync, GrabModeAsync);
}

void X11Adapter::UngrabKey(KeyCode keycode, unsigned int modifiers) {
    if (!display_) return;
    XUngrabKey(display_, keycode, modifiers, root_);
    XUngrabKey(display_, keycode, modifiers | Mod2Mask, root_);
    XUngrabKey(display_, keycode, modifiers | LockMask, root_);
    XUngrabKey(display_, keycode, modifiers | Mod2Mask | LockMask, root_);
}

void X11Adapter::GrabButton(unsigned int button, unsigned int modifiers) {
    if (!display_) return;
    XGrabButton(display_, button, modifiers, root_, True,
        ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
}

void X11Adapter::UngrabButton(unsigned int button, unsigned int modifiers) {
    if (!display_) return;
    XUngrabButton(display_, button, modifiers, root_);
}

std::unique_ptr<InputBackend> havel::CreateX11Adapter() {
    return std::make_unique<X11Adapter>();
}
#endif

}
