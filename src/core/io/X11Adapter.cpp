#include "InputBackend.hpp"
#include "KeyMap.hpp"
#include "UinputDevice.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <shared_mutex>
#include <unordered_map>

#ifdef __linux__
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <poll.h>
#include <sys/eventfd.h>
#endif

namespace havel {

#ifdef __linux__
namespace x11 {
constexpr int XKeyPress = 2;
constexpr int XKeyRelease = 3;
constexpr int XButtonPress = 4;
constexpr int XButtonRelease = 5;
constexpr int XMotionNotify = 6;
}
#endif

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

    void SetDisplayName(const std::string &name) { displayName_ = name; }
    void SetMouseSensitivity(double sens) { mouseSensitivity_ = sens; }
    void SetScrollSpeed(double speed) { scrollSpeed_ = speed; }

    void RegisterHotkey(int id, uint32_t keycode, uint32_t modifiers);
    void UnregisterHotkey(int id);
    void ClearHotkeys();

    void SetEventQueue(void *queue);

private:
    struct HotkeyInfo {
        uint32_t keycode = 0;
        uint32_t modifiers = 0;
        bool onRelease = false;
    };

    void ProcessX11Event(int type, const XEvent &event);
    void ProcessKeyEvent(const XKeyEvent &ke, bool down);
    void ProcessButtonEvent(const XButtonEvent &be, bool down);
    void ProcessMotionEvent(const XMotionEvent &me);

    uint32_t X11KeycodeToEvdev(KeyCode keycode) const;
    uint32_t X11ButtonToEvdev(unsigned int button) const;
    uint32_t X11ModifiersToHavel(unsigned int state) const;
    unsigned int HavelModifiersToX11(uint32_t modifiers) const;
    unsigned int CleanMask(unsigned int mask) const;

    bool SetupXInputExtension();
    void QueryXInputDevices();

    void GrabKey(uint32_t keycode, uint32_t modifiers);
    void UngrabKey(uint32_t keycode, uint32_t modifiers);
    void GrabButton(uint32_t button, uint32_t modifiers);
    void UngrabButton(uint32_t button, uint32_t modifiers);

    void UpdateKeyState(uint32_t evdevCode, bool down);
    void UpdateModifiers(uint32_t evdevCode, bool down);

#ifdef __linux__
    static constexpr unsigned int RELEVANT_MODIFIERS =
        ShiftMask | LockMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask;
#endif

    bool initialized_ = false;
    bool grabbed_ = false;
    std::string displayName_;

#ifdef __linux__
    Display *display_ = nullptr;
    Window root_ = 0;
    int xfd_ = -1;
    XIM im_ = nullptr;
    XIC ic_ = nullptr;
#endif

    int shutdownFd_ = -1;

    mutable std::shared_mutex stateMutex_;
    std::unordered_map<uint32_t, bool> keyStates_;
    std::unordered_map<uint32_t, bool> buttonStates_;
    uint32_t modifiers_ = 0;
    int32_t mouseX_ = 0;
    int32_t mouseY_ = 0;

    std::unordered_map<int, HotkeyInfo> hotkeys_;
    mutable std::mutex hotkeyMutex_;

    double mouseSensitivity_ = 1.0;
    double scrollSpeed_ = 1.0;

    std::unordered_map<KeyCode, uint32_t> keycodeMap_;
    std::unordered_map<uint32_t, KeyCode> evdevToX11_;

    void *eventQueue_ = nullptr;
};

X11Adapter::X11Adapter() {
    shutdownFd_ = eventfd(0, EFD_NONBLOCK);
}

X11Adapter::~X11Adapter() {
    Shutdown();
    if (shutdownFd_ >= 0) {
        close(shutdownFd_);
    }
}

bool X11Adapter::Init() {
#ifdef __linux__
    if (initialized_) return true;

    if (!XInitThreads()) {
        error("X11Adapter: Failed to initialize X11 threading");
        return false;
    }

    const char *name = displayName_.empty() ? nullptr : displayName_.c_str();
    display_ = XOpenDisplay(name);
    if (!display_) {
        error("X11Adapter: Failed to open display '{}'", displayName_.empty() ? ":0" : displayName_);
        return false;
    }

    XSync(display_, False);

    root_ = DefaultRootWindow(display_);
    xfd_ = XConnectionNumber(display_);

    im_ = XOpenIM(display_, nullptr, nullptr, nullptr);
    if (im_) {
        ic_ = XCreateIC(im_, XNInputStyle, 0, nullptr);
    }

    XSelectInput(display_, root_,
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | ButtonMotionMask |
        EnterWindowMask | LeaveWindowMask |
        FocusChangeMask | PropertyChangeMask);

    SetupXInputExtension();
    QueryXInputDevices();

    int minKeycode, maxKeycode;
    XDisplayKeycodes(display_, &minKeycode, &maxKeycode);

    for (int kc = minKeycode; kc <= maxKeycode; ++kc) {
        KeySym ks = XKeycodeToKeysym(display_, kc, 0);
        if (ks != NoSymbol) {
            uint32_t evdevCode = 0;

            if (ks >= XK_a && ks <= XK_z) evdevCode = KEY_A + (ks - XK_a);
            else if (ks >= XK_A && ks <= XK_Z) evdevCode = KEY_A + (ks - XK_A);
            else if (ks >= XK_0 && ks <= XK_9) evdevCode = KEY_0 + (ks - XK_0);
            else if (ks >= XK_F1 && ks <= XK_F12) evdevCode = KEY_F1 + (ks - XK_F1);
            else {
                switch (ks) {
                    case XK_Return: evdevCode = KEY_ENTER; break;
                    case XK_Tab: evdevCode = KEY_TAB; break;
                    case XK_space: evdevCode = KEY_SPACE; break;
                    case XK_BackSpace: evdevCode = KEY_BACKSPACE; break;
                    case XK_Escape: evdevCode = KEY_ESC; break;
                    case XK_Control_L: evdevCode = KEY_LEFTCTRL; break;
                    case XK_Control_R: evdevCode = KEY_RIGHTCTRL; break;
                    case XK_Shift_L: evdevCode = KEY_LEFTSHIFT; break;
                    case XK_Shift_R: evdevCode = KEY_RIGHTSHIFT; break;
                    case XK_Alt_L: evdevCode = KEY_LEFTALT; break;
                    case XK_Alt_R: evdevCode = KEY_RIGHTALT; break;
                    case XK_Super_L: evdevCode = KEY_LEFTMETA; break;
                    case XK_Super_R: evdevCode = KEY_RIGHTMETA; break;
                    case XK_Up: evdevCode = KEY_UP; break;
                    case XK_Down: evdevCode = KEY_DOWN; break;
                    case XK_Left: evdevCode = KEY_LEFT; break;
                    case XK_Right: evdevCode = KEY_RIGHT; break;
                    case XK_Insert: evdevCode = KEY_INSERT; break;
                    case XK_Delete: evdevCode = KEY_DELETE; break;
                    case XK_Home: evdevCode = KEY_HOME; break;
                    case XK_End: evdevCode = KEY_END; break;
                    case XK_Page_Up: evdevCode = KEY_PAGEUP; break;
                    case XK_Page_Down: evdevCode = KEY_PAGEDOWN; break;
                    case XK_Caps_Lock: evdevCode = KEY_CAPSLOCK; break;
                    case XK_Num_Lock: evdevCode = KEY_NUMLOCK; break;
                    case XK_Scroll_Lock: evdevCode = KEY_SCROLLLOCK; break;
                    case XK_grave: evdevCode = KEY_GRAVE; break;
                    case XK_minus: evdevCode = KEY_MINUS; break;
                    case XK_equal: evdevCode = KEY_EQUAL; break;
                    case XK_bracketleft: evdevCode = KEY_LEFTBRACE; break;
                    case XK_bracketright: evdevCode = KEY_RIGHTBRACE; break;
                    case XK_backslash: evdevCode = KEY_BACKSLASH; break;
                    case XK_semicolon: evdevCode = KEY_SEMICOLON; break;
                    case XK_apostrophe: evdevCode = KEY_APOSTROPHE; break;
                    case XK_comma: evdevCode = KEY_COMMA; break;
                    case XK_period: evdevCode = KEY_DOT; break;
                    case XK_slash: evdevCode = KEY_SLASH; break;
                    default: break;
                }
            }

            if (evdevCode > 0) {
                keycodeMap_[kc] = evdevCode;
                evdevToX11_[evdevCode] = kc;
            }
        }
    }

    initialized_ = true;
    debug("X11Adapter: Initialized on display {}", DisplayString(display_));
    return true;
#else
    return false;
#endif
}

void X11Adapter::Shutdown() {
#ifdef __linux__
    UngrabAllDevices();

    if (ic_) { XDestroyIC(ic_); ic_ = nullptr; }
    if (im_) { XCloseIM(im_); im_ = nullptr; }

    if (display_) {
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
    if (!display_) return result;

    DeviceInfo info;
    info.path = DisplayString(display_) ? DisplayString(display_) : ":0";
    info.name = "X11 Display";
    info.fd = xfd_;
    info.backend = InputBackendType::X11;
    result.push_back(info);
#endif
    return result;
}

bool X11Adapter::OpenDevice(const std::string &path) {
    displayName_ = path;
    return Init();
}

void X11Adapter::CloseDevice(const std::string &path) {
    Shutdown();
}

bool X11Adapter::GrabDevice(const std::string &path) {
#ifdef __linux__
    if (!display_) return false;

    int result = XGrabKeyboard(display_, root_, True,
        GrabModeAsync, GrabModeAsync, CurrentTime);
    if (result != GrabSuccess) {
        error("X11Adapter: Failed to grab keyboard (error {})", result);
        return false;
    }

    XGrabPointer(display_, root_, True,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    XSync(display_, False);
    grabbed_ = true;

    {
        std::lock_guard<std::mutex> lock(hotkeyMutex_);
        for (const auto &[id, info] : hotkeys_) {
            GrabKey(info.keycode, info.modifiers);
        }
    }

    debug("X11Adapter: Grabbed input");
    return true;
#else
    return false;
#endif
}

void X11Adapter::UngrabDevice(const std::string &path) {
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

    struct pollfd pfds[2];
    int nfd = 0;

    if (xfd_ >= 0) {
        pfds[nfd].fd = xfd_;
        pfds[nfd].events = POLLIN;
        nfd++;
    }

    if (shutdownFd_ >= 0) {
        pfds[nfd].fd = shutdownFd_;
        pfds[nfd].events = POLLIN;
        nfd++;
    }

    if (nfd == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return false;
    }

    int ret = poll(pfds, nfd, timeoutMs);
    if (ret <= 0) return false;

    if (XPending(display_) <= 0) return false;

    XEvent event;
    while (XPending(display_) > 0) {
        XNextEvent(display_, &event);
        ProcessX11Event(event.type, event);
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

    auto it = evdevToX11_.find(code);
    if (it == evdevToX11_.end()) return false;

    KeyCode xk = it->second;
    XKeyEvent event = {};
    event.display = display_;
    event.window = root_;
    event.root = root_;
    event.subwindow = None;
    event.time = CurrentTime;
    event.x = 1;
    event.y = 1;
    event.x_root = 1;
    event.y_root = 1;
    event.same_screen = True;
    event.keycode = xk;
    event.state = HavelModifiersToX11(modifiers_);
    event.type = down ? KeyPress : KeyRelease;

    XSendEvent(display_, root_, True, KeyPressMask | KeyReleaseMask,
        reinterpret_cast<XEvent*>(&event));
    XFlush(display_);
    return true;
#else
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
            uint32_t xbutton = 0;
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
            xev.xbutton.time = CurrentTime;
            XSendEvent(display_, root_, True,
                ButtonPressMask | ButtonReleaseMask, &xev);
            XFlush(display_);
            return true;
        }

        case MouseEvent::Type::Wheel: {
            int button = event.wheel > 0 ? Button4 : Button5;
            XEvent xev = {};
            xev.type = ButtonPress;
            xev.xbutton.button = button;
            xev.xbutton.same_screen = True;
            xev.xbutton.root = root_;
            xev.xbutton.window = root_;
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

void X11Adapter::RegisterHotkey(int id, uint32_t keycode, uint32_t modifiers) {
#ifdef __linux__
    std::lock_guard<std::mutex> lock(hotkeyMutex_);

    HotkeyInfo info;
    info.keycode = keycode;
    info.modifiers = modifiers;
    hotkeys_[id] = info;

    if (grabbed_ && display_) {
        GrabKey(keycode, modifiers);
    }
#endif
}

void X11Adapter::UnregisterHotkey(int id) {
#ifdef __linux__
    std::lock_guard<std::mutex> lock(hotkeyMutex_);

    auto it = hotkeys_.find(id);
    if (it != hotkeys_.end()) {
        if (grabbed_ && display_) {
            UngrabKey(it->second.keycode, it->second.modifiers);
        }
        hotkeys_.erase(it);
    }
#endif
}

void X11Adapter::ClearHotkeys() {
    std::lock_guard<std::mutex> lock(hotkeyMutex_);

#ifdef __linux__
    if (grabbed_ && display_) {
        for (const auto &[id, info] : hotkeys_) {
            UngrabKey(info.keycode, info.modifiers);
        }
    }
#endif
    hotkeys_.clear();
}

void X11Adapter::SetEventQueue(void *queue) {
    eventQueue_ = queue;
}

#ifdef __linux__
void X11Adapter::ProcessX11Event(int type, const XEvent &event) {
    auto now = std::chrono::steady_clock::now();

    switch (type) {
        case KeyPress:
        case KeyRelease:
            ProcessKeyEvent(event.xkey, type == KeyPress);
            break;

        case ButtonPress:
        case ButtonRelease:
            ProcessButtonEvent(event.xbutton, type == ButtonPress);
            break;

        case MotionNotify:
            ProcessMotionEvent(event.xmotion);
            break;
    }
}

void X11Adapter::ProcessKeyEvent(const XKeyEvent &ke, bool down) {
    auto now = std::chrono::steady_clock::now();
    uint32_t evdevCode = X11KeycodeToEvdev(ke.keycode);

    if (evdevCode == 0) {
        KeySym ks = XKeycodeToKeysym(display_, ke.keycode, 0);
        if (ks != NoSymbol) {
            const char *name = XKeysymToString(ks);
            debug("X11Adapter: Unknown keycode {} -> keysym {} ({})",
                ke.keycode, ks, name ? name : "?");
        }
        return;
    }

    UpdateKeyState(evdevCode, down);
    UpdateModifiers(evdevCode, down);

    if (keyCallback_) {
        KeyEvent event;
        event.code = evdevCode;
        event.down = down;
        event.repeat = false;
        event.modifiers = modifiers_;
        event.keyName = KeyMap::EvdevToString(evdevCode);
        event.timestamp = now;
        keyCallback_(event);
    }

    std::vector<int> triggeredIds;
    {
        std::lock_guard<std::mutex> lock(hotkeyMutex_);
        unsigned int cleanedState = CleanMask(ke.state);

        for (const auto &[id, info] : hotkeys_) {
            if (info.keycode == ke.keycode &&
                info.modifiers == cleanedState) {
                triggeredIds.push_back(id);
            }
        }
    }
}

void X11Adapter::ProcessButtonEvent(const XButtonEvent &be, bool down) {
    auto now = std::chrono::steady_clock::now();
    uint32_t evdevButton = X11ButtonToEvdev(be.button);

    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        buttonStates_[evdevButton] = down;
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

uint32_t X11Adapter::X11KeycodeToEvdev(KeyCode keycode) const {
    auto it = keycodeMap_.find(keycode);
    return it != keycodeMap_.end() ? it->second : 0;
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

bool X11Adapter::SetupXInputExtension() {
    int major = 2, minor = 0;
    int opcode, event, error;

    if (!XQueryExtension(display_, "XInputExtension", &opcode, &event, &error)) {
        debug("X11Adapter: XInput extension not available");
        return false;
    }

    XISetMask(nullptr, XI_RawKeyPress);
    XISetMask(nullptr, XI_RawKeyRelease);
    XISetMask(nullptr, XI_RawButtonPress);
    XISetMask(nullptr, XI_RawButtonRelease);
    XISetMask(nullptr, XI_RawMotion);

    return true;
}

void X11Adapter::QueryXInputDevices() {
}

void X11Adapter::GrabKey(uint32_t keycode, uint32_t modifiers) {
    unsigned int xmods = HavelModifiersToX11(modifiers);
    XGrabKey(display_, keycode, xmods, root_, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display_, keycode, xmods | Mod2Mask, root_, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display_, keycode, xmods | LockMask, root_, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display_, keycode, xmods | Mod2Mask | LockMask, root_, True, GrabModeAsync, GrabModeAsync);
}

void X11Adapter::UngrabKey(uint32_t keycode, uint32_t modifiers) {
    unsigned int xmods = HavelModifiersToX11(modifiers);
    XUngrabKey(display_, keycode, xmods, root_);
    XUngrabKey(display_, keycode, xmods | Mod2Mask, root_);
    XUngrabKey(display_, keycode, xmods | LockMask, root_);
    XUngrabKey(display_, keycode, xmods | Mod2Mask | LockMask, root_);
}

void X11Adapter::GrabButton(uint32_t button, uint32_t modifiers) {
    unsigned int xmods = HavelModifiersToX11(modifiers);
    unsigned int xbutton = 0;
    switch (button) {
        case BTN_LEFT: xbutton = Button1; break;
        case BTN_MIDDLE: xbutton = Button2; break;
        case BTN_RIGHT: xbutton = Button3; break;
        default: return;
    }
    XGrabButton(display_, xbutton, xmods, root_, True,
        ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
}

void X11Adapter::UngrabButton(uint32_t button, uint32_t modifiers) {
    unsigned int xmods = HavelModifiersToX11(modifiers);
    unsigned int xbutton = 0;
    switch (button) {
        case BTN_LEFT: xbutton = Button1; break;
        case BTN_MIDDLE: xbutton = Button2; break;
        case BTN_RIGHT: xbutton = Button3; break;
        default: return;
    }
    XUngrabButton(display_, xbutton, xmods, root_);
}

void X11Adapter::UpdateKeyState(uint32_t evdevCode, bool down) {
    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    keyStates_[evdevCode] = down;
}

void X11Adapter::UpdateModifiers(uint32_t evdevCode, bool down) {
    uint32_t mask = 0;
    switch (evdevCode) {
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

    std::unique_lock<std::shared_mutex> lock(stateMutex_);
    if (down) modifiers_ |= mask;
    else modifiers_ &= ~mask;
}
#endif

}
