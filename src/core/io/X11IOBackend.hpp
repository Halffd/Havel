#pragma once
#include "IOBackend.hpp"
#include "x11.h"

namespace havel {

// The concrete X11 modifier masks a hotkey is grabbed under.
//
// A hotkey must fire whether CapsLock and NumLock are on or off, so the
// grab is installed for every combination of the two lock modifiers.
//
// The incoming `modifiers` may already carry LockMask / numlockMask_ -
// ToPlatformMask() OR-ed them in when the script asked for a CapsLock or
// NumLock modifier - so the lock bits are stripped from the base first, and
// the plain (no-lock) variant is added explicitly.
//
// Registering and unregistering MUST use the same list, or a grab
// installed under one mask set is never removed: the key stays grabbed on
// the X server after the hotkey was removed, and the binding keeps
// stealing the key from every other application.
//
// `numlockMask` is the modifier bit the X server maps Num_Lock to (0 on a
// keymap without NumLock). The result is deduplicated, so a keymap where
// Num_Lock is absent - or maps to the same bit as CapsLock - yields each
// distinct mask exactly once.
std::vector<unsigned int> GrabMaskVariants(unsigned int modifiers,
                                            unsigned int numlockMask);

// Converts an X11 event state (the `state` field of an XKeyEvent) into the
// platform-neutral ModifierMasks layout.
//
// The two layouts are NOT interchangeable: the X11 state uses the server's
// modifier bits (Shift=0, Lock=1, Control=2, Mod1=3, Mod2=4, Mod4=6) while
// ModifierMasks uses the evdev bits (Ctrl=0, Shift=1, Alt=2, Meta=3).
// Comparing them directly - as X11HotkeyMonitor used to - can never match a
// hotkey that carries a modifier.
//
// `numlockMask` is the modifier bit the server maps Num_Lock to. When it is
// 0 (a keymap without NumLock) the Num_Lock state cannot be told apart and
// is reported as absent.
//
// Pure function: no X server, no display, no timing side effects.
int X11StateToAbstractMask(unsigned int state, unsigned int numlockMask);

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
    void UngrabKeyboard() override;

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

  int ToAbstractMask(int platformMask) const override;
  int ToPlatformMask(int abstractMask) const override;

  XkbCharMapping CharToKeycode(char32_t cp) override;

private:
    Display *display_ = nullptr;
    int xinput2DeviceId_ = -1;
    bool xinput2Available_ = false;
    unsigned int numlockMask_ = 0;

    void UpdateNumLockMask();
    static int XErrorHandler(Display *dpy, XErrorEvent *ee);
};

} // namespace havel
