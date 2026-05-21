#pragma once
#include "IOBackend.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

namespace havel {

class X11IOBackend : public IOBackend {
public:
    explicit X11IOBackend(Display *display);
    ~X11IOBackend() override;

    bool Initialize() override;
    void Cleanup() override;
    bool IsAvailable() const override;
    std::string GetName() const override;

    void PressKey(int keycode) override;
    void ReleaseKey(int keycode) override;

    bool MovePointer(int dx, int dy) override;
    bool MovePointerTo(int x, int y) override;
    std::pair<int, int> GetCursorPosition() override;
    void SendButton(int button, bool down) override;

    bool RegisterHotkey(int keycode, int modifiers, bool isButton) override;
    bool UnregisterHotkey(int keycode, int modifiers, bool isButton) override;
    void UnregisterAll() override;
    bool GrabKeyboard() override;

    bool IsKeyDown(int keycode) override;
    bool IsAnyKeyDown() override;

    bool SetupXInput2() override;
    bool SetHardwareSensitivity(double sensitivity) override;

    void TypeText(const std::string &text) override;

    int GetShiftMask() const override { return ShiftMask; }
    int GetControlMask() const override { return ControlMask; }
    int GetAltMask() const override { return Mod1Mask; }
    int GetMetaMask() const override { return Mod4Mask; }
    int GetLockMask() const override { return LockMask; }
    int GetNumLockMask() override;

private:
    Display *display_ = nullptr;
    int xinput2DeviceId_ = -1;
    bool xinput2Available_ = false;
    unsigned int numlockMask_ = 0;

    void UpdateNumLockMask();
    static int XErrorHandler(Display *dpy, XErrorEvent *ee);
};

} // namespace havel
