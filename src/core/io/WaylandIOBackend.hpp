#pragma once
#include "IOBackend.hpp"

namespace havel {

class EventListener;

class WaylandIOBackend : public IOBackend {
public:
    explicit WaylandIOBackend(EventListener *eventListener);
    ~WaylandIOBackend() override;

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

    int GetShiftMask() const override { return 1; }
    int GetControlMask() const override { return 2; }
    int GetAltMask() const override { return 4; }
    int GetMetaMask() const override { return 8; }
    int GetLockMask() const override { return 16; }
    int GetNumLockMask() override { return 0; }

private:
    EventListener *eventListener_ = nullptr;
};

} // namespace havel
