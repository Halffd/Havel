#pragma once
#include <memory>
#include <string>
#include <utility>

namespace havel {

class EventListener;

class IOBackend {
public:
    virtual ~IOBackend() = default;

    virtual bool Initialize() = 0;
    virtual void Cleanup() = 0;
    virtual bool IsAvailable() const = 0;
    virtual std::string GetName() const = 0;

    // Key send (platform API — XTest, uinput, keybd_event)
    virtual void PressKey(int keycode) = 0;
    virtual void ReleaseKey(int keycode) = 0;

    // Mouse
    virtual bool MovePointer(int dx, int dy) = 0;
    virtual bool MovePointerTo(int x, int y) = 0;
    virtual std::pair<int, int> GetCursorPosition() = 0;
    virtual void SendButton(int button, bool down) = 0;

    // Hotkey registration (X11 XGrabKey)
    virtual bool RegisterHotkey(int keycode, int modifiers, bool isButton) = 0;
    virtual bool UnregisterHotkey(int keycode, int modifiers, bool isButton) = 0;
    virtual void UnregisterAll() = 0;
    virtual bool GrabKeyboard() = 0;

    // State queries
    virtual bool IsKeyDown(int keycode) = 0;
    virtual bool IsAnyKeyDown() = 0;

    // XInput2
    virtual bool SetupXInput2() = 0;
    virtual bool SetHardwareSensitivity(double sensitivity) = 0;

    // Text
    virtual void TypeText(const std::string &text) = 0;

    // Modifier masks (platform-specific constants)
    virtual int GetShiftMask() const = 0;
    virtual int GetControlMask() const = 0;
    virtual int GetAltMask() const = 0;
    virtual int GetMetaMask() const = 0;
    virtual int GetLockMask() const = 0;
    virtual int GetNumLockMask() = 0;

    // Factory
    static std::unique_ptr<IOBackend> Create(EventListener *eventListener);
};

} // namespace havel
