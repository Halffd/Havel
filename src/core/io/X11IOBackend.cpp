#include "X11IOBackend.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "utils/DebugFlags.hpp"
#include "../DisplayManager.hpp"
#include "../io/X11Adapter.hpp"
#include <unistd.h>
#include <cstring>

namespace havel {

static auto &g_keycodeCache = *new std::unordered_map<std::string, int>();
static std::mutex g_keycodeCacheMutex;

X11IOBackend::X11IOBackend(Display *display)
    : display_(display) {}

X11IOBackend::~X11IOBackend() { Cleanup(); }

int X11IOBackend::XErrorHandler(Display *dpy, XErrorEvent *ee) {
    return 0;
}

bool X11IOBackend::Initialize() {
    if (!display_) return false;
    XSetErrorHandler(XErrorHandler);
    UpdateNumLockMask();
    return true;
}

void X11IOBackend::Cleanup() {
    display_ = nullptr;
}

bool X11IOBackend::IsAvailable() const { return display_ != nullptr; }
std::string X11IOBackend::GetName() const { return "x11"; }

void X11IOBackend::PressKey(int keycode) {
    if (!display_) return;
    XTestFakeKeyEvent(display_, keycode, x11::XTrue, CurrentTime);
    XFlush(display_);
}

void X11IOBackend::ReleaseKey(int keycode) {
    if (!display_) return;
    XTestFakeKeyEvent(display_, keycode, x11::XFalse, CurrentTime);
    XFlush(display_);
}

bool X11IOBackend::MovePointer(int dx, int dy) {
    if (!display_) return false;
    auto pos = GetCursorPosition();
    XTestFakeMotionEvent(display_, -1, pos.first + dx, pos.second + dy, CurrentTime);
    XFlush(display_);
    return true;
}

bool X11IOBackend::MovePointerTo(int x, int y) {
    if (!display_) return false;
    XTestFakeMotionEvent(display_, -1, x, y, CurrentTime);
    XFlush(display_);
    return true;
}

std::pair<int, int> X11IOBackend::GetCursorPosition() {
    if (!display_) return {0, 0};
    if (xinput2Available_ && xinput2DeviceId_ != -1) {
        ::Window root_return, child_return;
        double root_x, root_y, win_x, win_y;
        XIButtonState buttons;
        XIModifierState mods;
        XIGroupState group;
        if (XIQueryPointer(display_, xinput2DeviceId_, DefaultRootWindow(display_),
                           &root_return, &child_return, &root_x, &root_y, &win_x,
                           &win_y, &buttons, &mods, &group) == x11::XTrue) {
            return {(int)root_x, (int)root_y};
        }
    }
    ::Window root = DefaultRootWindow(display_);
    ::Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    if (XQueryPointer(display_, root, &root_return, &child_return, &root_x,
                      &root_y, &win_x, &win_y, &mask)) {
        return {root_x, root_y};
    }
    return {0, 0};
}

void X11IOBackend::SendButton(int button, bool down) {
    if (!display_) return;
    XTestFakeButtonEvent(display_, button, down ? x11::XTrue : x11::XFalse, CurrentTime);
    XFlush(display_);
}

bool X11IOBackend::RegisterHotkey(int keycode, int modifiers, bool isButton) {
    if (!display_) return false;
    bool success = true;
    ::Window root = DefaultRootWindow(display_);
    unsigned int modVariants[] = {0, LockMask, numlockMask_, numlockMask_ | LockMask};
    for (unsigned int variant : modVariants) {
        unsigned int finalMods = modifiers | variant;
        int result;
        if (isButton) {
            result = XGrabButton(display_, keycode, finalMods, root, x11::XTrue,
                                 ButtonPressMask | ButtonReleaseMask,
                                 GrabModeAsync, GrabModeAsync, x11::XNone, x11::XNone);
        } else {
            result = XGrabKey(display_, keycode, finalMods, root, x11::XTrue,
                              GrabModeAsync, GrabModeAsync);
        }
        if (result != x11::XSuccess) success = false;
    }
    XSync(display_, x11::XFalse);
    return success;
}

bool X11IOBackend::UnregisterHotkey(int keycode, int modifiers, bool isButton) {
    if (!display_) return false;
    ::Window root = DefaultRootWindow(display_);
    unsigned int modVariants[] = {0, LockMask, numlockMask_, numlockMask_ | LockMask};
    for (unsigned int variant : modVariants) {
        unsigned int finalMods = modifiers | variant;
        if (isButton) {
            XUngrabButton(display_, keycode, finalMods, root);
        } else {
            XUngrabKey(display_, keycode, finalMods, root);
        }
    }
    XSync(display_, x11::XFalse);
    return true;
}

void X11IOBackend::UnregisterAll() {
    if (!display_) return;
    XUngrabKey(display_, AnyKey, AnyModifier, DefaultRootWindow(display_));
    XUngrabButton(display_, AnyButton, AnyModifier, DefaultRootWindow(display_));
    XSync(display_, x11::XFalse);
}

bool X11IOBackend::GrabKeyboard() {
    if (!display_) return false;
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
    for (int i = 0; i < 1000; i++) {
        int result = XGrabKeyboard(display_, DefaultRootWindow(display_), x11::XTrue,
                                   GrabModeAsync, GrabModeAsync, CurrentTime);
        if (result == x11::XSuccess) {
            if (debugging::debug_io) debug("Successfully grabbed entire keyboard after {} attempts", i + 1);
            return true;
        }
        nanosleep(&ts, nullptr);
    }
    error("Cannot grab keyboard after 1000 attempts");
    return false;
}

bool X11IOBackend::IsKeyDown(int keycode) {
    if (!display_) return false;
    char keymap[32];
    XQueryKeymap(display_, keymap);
    return (keymap[keycode / 8] & (1 << (keycode % 8))) != 0;
}

bool X11IOBackend::IsAnyKeyDown() {
    if (!display_) return false;
    char keymap[32];
    XQueryKeymap(display_, keymap);
    for (int i = 0; i < 32; i++) {
        if (keymap[i] != 0) return true;
    }
    return false;
}

bool X11IOBackend::SetupXInput2() {
    if (!display_) return false;
    int xi_opcode, event, xi_error;
    if (!XQueryExtension(display_, "XInputExtension", &xi_opcode, &event, &xi_error)) {
        error("X Input extension not available");
        return false;
    }
    int major = 2, minor = 0;
    if (XIQueryVersion(display_, &major, &minor) != x11::XSuccess) {
        error("XInput2 not supported by server");
        return false;
    }
    XIDeviceInfo *devices;
    int ndevices;
    devices = XIQueryDevice(display_, XIAllDevices, &ndevices);
    for (int i = 0; i < ndevices; i++) {
        XIDeviceInfo *dev = &devices[i];
        if (dev->use == XISlavePointer || dev->use == XIFloatingSlave) {
            xinput2DeviceId_ = dev->deviceid;
            break;
        }
    }
    XIFreeDeviceInfo(devices);
    if (xinput2DeviceId_ == -1) {
        error("No suitable XInput2 pointer device found");
        return false;
    }
    xinput2Available_ = true;
    return true;
}

bool X11IOBackend::SetHardwareSensitivity(double sensitivity) {
    if (!display_ || xinput2DeviceId_ == -1) return false;
    sensitivity = std::max(0.1, std::min(10.0, sensitivity));
    double accel_numerator = sensitivity * sensitivity * 10.0;
    double accel_denominator = 10.0;
    double threshold = 0.0;
    XDevice *dev = XOpenDevice(display_, xinput2DeviceId_);
    if (!dev) {
        error("Failed to open XInput2 device");
        return false;
    }
    XDeviceControl *control = (XDeviceControl *)XGetDeviceControl(display_, dev, DEVICE_RESOLUTION);
    if (!control) {
        XCloseDevice(display_, dev);
        return false;
    }
    XChangePointerControl(display_, x11::XTrue, x11::XTrue,
                          (int)(accel_numerator * 10), (int)(accel_denominator * 10), (int)threshold);
    XFree(control);
    XCloseDevice(display_, dev);
    if (debugging::debug_io) debug("Set hardware mouse sensitivity to: {}", sensitivity);
    return true;
}

void X11IOBackend::TypeText(const std::string &text) {
    // X11 clipboard-based text input not easily implemented without a clipboard manager.
    // Fallback: will be handled by IO sending individual keystrokes.
}

void X11IOBackend::UpdateNumLockMask() {
    if (!display_) return;
    XModifierKeymap *modmap = XGetModifierMapping(display_);
    if (!modmap) return;
    KeyCode nlock = XKeysymToKeycode(display_, XK_Num_Lock);
    KeyCode slock = XKeysymToKeycode(display_, XK_Scroll_Lock);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < modmap->max_keypermod; j++) {
            KeyCode code = modmap->modifiermap[i * modmap->max_keypermod + j];
            if (code == nlock) numlockMask_ = 1 << i;
        }
    }
    XFreeModifiermap(modmap);
}

int X11IOBackend::GetNumLockMask() {
    UpdateNumLockMask();
    return numlockMask_;
}

} // namespace havel
