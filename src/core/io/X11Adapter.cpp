#include "InputBackend.hpp"
// For ModifierMasks: the platform-neutral modifier bit layout.
#include "IOBackend.hpp"
#include "KeyMap.hpp"
#include "utils/Logger.hpp"
#include "utils/ExitHandler.hpp"
#include "X11Adapter.hpp"
#ifdef __linux__
#include "X11ErrorProbe.hpp"
#endif
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

bool ShouldRetrySelectInput(bool subscribed, bool hasDisplay,
                             std::chrono::steady_clock::time_point now,
                             std::chrono::steady_clock::time_point nextRetry) {
    // Nothing to do when the subscription is already in place.
    if (subscribed)
        return false;
    // Nothing to do without a connection to ask on.
    if (!hasDisplay)
        return false;
    // The poll loop runs every millisecond. The X server would answer every
    // one of those requests with BadAccess, so the retry is rate-limited.
    return now >= nextRetry;
}

X11Adapter::X11Adapter() = default;

X11Adapter::~X11Adapter() {
    Shutdown();
}

bool X11Adapter::Init() {
#ifdef __linux__
    if (initialized_) return true;

    const char *name = displayName_.empty() ? nullptr : displayName_.c_str();
    display_ = XOpenDisplay(name);
    if (!display_) {
        error("X11Adapter: Cannot open display '{}'", displayName_.empty() ? ":0" : displayName_);
        return false;
    }

    XSync(display_, False);
    root_ = DefaultRootWindow(display_);
    xfd_ = XConnectionNumber(display_);

    // The first retry of a refused subscription happens one second from
    // now, not immediately: the poll loop runs every millisecond.
    nextSelectRetry_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    SelectRootInputEvents();

    BuildKeycodeMap();

    initialized_ = true;
    debug("X11Adapter: Initialized on display {}", DisplayString(display_));
    g_active_x11_adapter.store(this, std::memory_order_release);
    // Exit cleanups run after the Havel instance is destroyed (main runs
    // runExitCleanups after launcher.run returns), so a raw `this` capture
    // here is a use-after-free at shutdown: ~X11Adapter already freed the
    // object by then (observed SIGSEGV at process exit under --io x11).
    // Route through the atomic pointer instead — Shutdown() nulls it, so
    // the cleanup no-ops once the adapter is gone.
    registerExitCleanup([]() {
        if (auto *adapter = g_active_x11_adapter.load(std::memory_order_acquire)) {
            adapter->UngrabAllDevices();
        }
    });
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
    // The subscription died with the connection: a later Init() has to ask
    // for it again.
    rootInputSelected_ = false;
    nextSelectRetry_ = {};
#endif
    g_active_x11_adapter.store(nullptr, std::memory_order_release);
    initialized_ = false;
}

#ifdef __linux__
bool X11Adapter::SelectRootInputEvents() {
    if (!display_) return false;
    if (rootInputSelected_) return true;

    // The root-window subscription is made in TWO calls, on purpose.
    //
    // XSelectInput REPLACES the event mask of the window, and the X server
    // refuses the whole request - BadAccess - when it selects
    // ButtonPress/ButtonRelease on the root window while another client
    // holds an active pointer grab. A refused request leaves the PREVIOUS
    // mask in place; it does not partially apply.
    //
    // Asking for everything in one call therefore had a much wider blast
    // radius than the actual refusal: the ButtonPress/ButtonRelease bits
    // were refused, and the key events went down with them - so the client
    // received NO key events at all, even though a key-only subscription
    // would have been accepted.
    //
    // Splitting the request keeps the blast radius at the bits that were
    // actually refused:
    //
    //   1. key events - must succeed. Without them no hotkey can fire, so
    //      a refusal here is a real failure and is retried.
    //   2. pointer events - best effort. A refusal only costs pointer
    //      events, which the pointer grab already takes away.
    {
        ScopedX11ErrorProbe probe(display_);
        XSelectInput(display_, root_, KeyPressMask | KeyReleaseMask);
        XSync(display_, False);
        if (probe.Failed()) {
            rootInputSelected_ = false;
            error("X11Adapter: XSelectInput for key events on the root window "
                  "was refused (error code {}); key events are not delivered "
                  "until the other client releases its grab",
                  probe.ErrorCode());
            return false;
        }
    }

    // The key subscription is live now: mark it BEFORE the pointer call so
    // a pointer refusal cannot take the key subscription down with it.
    rootInputSelected_ = true;

    // The second call asks for the key bits AGAIN, together with the
    // pointer bits. XSelectInput REPLACES the whole mask, so a pointer-only
    // request would WIPE the key subscription that the call above just
    // installed. Asking for both means:
    //
    //   - the call succeeds -> the mask holds key AND pointer events;
    //   - the call is refused -> the mask keeps the key-only value from the
    //     call above, because a refused request changes nothing.
    {
        ScopedX11ErrorProbe probe(display_);
        XSelectInput(display_, root_,
            KeyPressMask | KeyReleaseMask |
            ButtonPressMask | ButtonReleaseMask |
            PointerMotionMask | ButtonMotionMask);
        XSync(display_, False);
        if (probe.Failed()) {
            // Not fatal: the key-only subscription from the call above is
            // still in effect. Only pointer events are missing.
            warning("X11Adapter: XSelectInput for pointer events on the root "
                    "window was refused (error code {}); pointer events are "
                    "not delivered, key events are",
                    probe.ErrorCode());
        }
    }

    debug("X11Adapter: subscribed to root window key events (pointer events "
          "best effort)");
    return true;
}

void X11Adapter::RetrySelectInput() {
    const auto now = std::chrono::steady_clock::now();
    if (!ShouldRetrySelectInput(rootInputSelected_, display_ != nullptr, now,
                                nextSelectRetry_))
        return;
    // Move the deadline before the attempt: the attempt itself talks to
    // the X server, and the next one must not be due before it is done.
    nextSelectRetry_ = now + std::chrono::seconds(1);
    SelectRootInputEvents();
}
#endif

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

    if (keyboardGrabbed_ || pointerGrabbed_) {
        debug("X11Adapter: GrabDevice already active");
        return true;
    }

    int kResult = XGrabKeyboard(display_, root_, True,
        GrabModeAsync, GrabModeAsync, CurrentTime);
    XSync(display_, False);
    debug("[X11 GRAB] keyboardResult={} display={}", kResult, static_cast<void*>(display_));
    if (kResult != GrabSuccess) {
        error("X11Adapter: XGrabKeyboard failed ({})", kResult);
        return false;
    }
    keyboardGrabbed_ = true;

    int pResult = XGrabPointer(display_, root_, True,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XSync(display_, False);
    debug("[X11 GRAB] pointerResult={} display={}", pResult, static_cast<void*>(display_));
    if (pResult != GrabSuccess) {
        error("X11Adapter: XGrabPointer failed ({})", pResult);
        XUngrabKeyboard(display_, CurrentTime);
        XSync(display_, False);
        keyboardGrabbed_ = false;
        return false;
    }
    pointerGrabbed_ = true;

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
    if (display_) {
        if (keyboardGrabbed_) {
            XUngrabKeyboard(display_, CurrentTime);
            keyboardGrabbed_ = false;
        }
        if (pointerGrabbed_) {
            XUngrabPointer(display_, CurrentTime);
            pointerGrabbed_ = false;
        }
        XSync(display_, False);
        debug("[X11 UNGRAB] keyboard/pointer display={}", static_cast<void*>(display_));
    }
#endif
}

void X11Adapter::SignalSafeUngrabAll() {
#ifdef __linux__
    if (display_) {
        if (keyboardGrabbed_) {
            XUngrabKeyboard(display_, CurrentTime);
            keyboardGrabbed_ = false;
        }
        if (pointerGrabbed_) {
            XUngrabPointer(display_, CurrentTime);
            pointerGrabbed_ = false;
        }
        XSync(display_, False);
    }
#endif
}

std::vector<int> X11Adapter::GetInputFds() const {
#ifdef __linux__
    if (xfd_ >= 0) return {xfd_};
#endif
    return {};
}

void X11Adapter::OnFdsReady(const std::vector<std::pair<int, short>> &ready) {
#ifdef __linux__
    if (!display_) return;
    bool ours = false;
    for (const auto &[fd, revents] : ready) {
        (void)revents;
        if (fd == xfd_) { ours = true; break; }
    }
    if (!ours) return;

    XEvent event;
    while (XPending(display_) > 0) {
        XNextEvent(display_, &event);
        ProcessX11Event(event);
    }
#endif
}

void X11Adapter::Poll() {
#ifdef __linux__
    RetrySelectInput();
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
    // ModifierMasks layout - see IOBackend.hpp.
    if (modifierState_.IsCtrlPressed()) mask |= ModifierMasks::CONTROL;
    if (modifierState_.IsShiftPressed()) mask |= ModifierMasks::SHIFT;
    if (modifierState_.IsAltPressed()) mask |= ModifierMasks::ALT;
    if (modifierState_.IsMetaPressed()) mask |= ModifierMasks::META;
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
        KeySym ks = XkbKeycodeToKeysym(display_, kc, 0, 0);
        if (ks == NoSymbol) continue;

        uint32_t evdev = 0;

        if (ks >= XK_a && ks <= XK_z) evdev = KEY_A + (ks - XK_a);
        else if (ks >= XK_A && ks <= XK_Z) evdev = KEY_A + (ks - XK_A);
        else if (ks >= XK_0 && ks <= XK_9) evdev = KEY_0 + (ks - XK_0);
        else if (ks >= XK_F1 && ks <= XK_F12) evdev = KEY_F1 + (ks - XK_F1);
        // F13-F24 live in a second, separate evdev block: KEY_F12 is 68
        // but KEY_F13 is 183, so they cannot extend the range above.
        // Without this branch every F13-F24 keypress was silently dropped
        // in ProcessKeyEvent (X11KeycodeToEvdev returned 0) - and havel's
        // own hotkey convention standardizes on F24 as the
        // non-conflicting hotkey, so the X11 backend could never see it.
        else if (ks >= XK_F13 && ks <= XK_F24) evdev = KEY_F13 + (ks - XK_F13);
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
    // The platform-neutral ModifierMasks layout - the SAME layout the
    // hotkey matchers read. This used to spell the bits out inline as
    // (1 << 0) for ctrl and (1 << 1) for shift, which happened to match
    // the evdev layout but drifted from ModifierMasks (which had Ctrl and
    // Shift on the opposite bits) - so the X11 path and the grab path
    // disagreed about which bit meant Ctrl.
    uint32_t mods = 0;
    if (state & ControlMask) mods |= ModifierMasks::CONTROL;
    if (state & ShiftMask) mods |= ModifierMasks::SHIFT;
    if (state & Mod1Mask) mods |= ModifierMasks::ALT;
    if (state & Mod4Mask) mods |= ModifierMasks::META;
    return mods;
}

unsigned int X11Adapter::HavelModifiersToX11(uint32_t modifiers) const {
    unsigned int state = 0;
    if (modifiers & ModifierMasks::CONTROL) state |= ControlMask;
    if (modifiers & ModifierMasks::SHIFT) state |= ShiftMask;
    if (modifiers & ModifierMasks::ALT) state |= Mod1Mask;
    if (modifiers & ModifierMasks::META) state |= Mod4Mask;
    return state;
}

unsigned int X11Adapter::CleanMask(unsigned int mask) const {
    return mask & RELEVANT_MODIFIERS;
}

std::unique_ptr<InputBackend> CreateX11Adapter() {
    return std::make_unique<X11Adapter>();
}
#endif

}
