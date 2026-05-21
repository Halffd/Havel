#pragma once
#include "IOBackend.hpp"
#ifdef WINDOWS
#include <windows.h>
#endif

namespace havel {

#ifdef WINDOWS
class WindowsIOBackend : public IOBackend {
public:
    WindowsIOBackend();
    ~WindowsIOBackend() override;

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

    int GetShiftMask() const override { return MOD_SHIFT; }
    int GetControlMask() const override { return MOD_CONTROL; }
    int GetAltMask() const override { return MOD_ALT; }
    int GetMetaMask() const override { return MOD_WIN; }
    int GetLockMask() const override { return 0; }
    int GetNumLockMask() override { return 0; }
};
#else
class WindowsIOBackend : public IOBackend {
public:
    WindowsIOBackend() = default;
    ~WindowsIOBackend() override = default;
    bool Initialize() override { return false; }
    void Cleanup() override {}
    bool IsAvailable() const override { return false; }
    std::string GetName() const override { return "WindowsIOBackend"; }
    void PressKey(int) override {}
    void ReleaseKey(int) override {}
    bool MovePointer(int, int) override { return false; }
    bool MovePointerTo(int, int) override { return false; }
    std::pair<int, int> GetCursorPosition() override { return {0, 0}; }
    void SendButton(int, bool) override {}
    bool RegisterHotkey(int, int, bool) override { return false; }
    bool UnregisterHotkey(int, int, bool) override { return false; }
    void UnregisterAll() override {}
    bool GrabKeyboard() override { return false; }
    bool IsKeyDown(int) override { return false; }
    bool IsAnyKeyDown() override { return false; }
    bool SetupXInput2() override { return false; }
    bool SetHardwareSensitivity(double) override { return false; }
    void TypeText(const std::string &) override {}
    int GetShiftMask() const override { return 0; }
    int GetControlMask() const override { return 0; }
    int GetAltMask() const override { return 0; }
    int GetMetaMask() const override { return 0; }
    int GetLockMask() const override { return 0; }
    int GetNumLockMask() override { return 0; }
};
#endif

} // namespace havel
