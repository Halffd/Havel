#pragma once
#include "IOBackend.hpp"
#include "core/wayland/VirtualKeyboard.hpp"
#include "core/wayland/VirtualPointer.hpp"
#include "core/wayland/WaylandClipboardBackend.hpp"
#include "core/wayland/ForeignToplevel.hpp"
#include <memory>

namespace havel {

class EventListener;
class WaylandProtocolClient;

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

  int ToAbstractMask(int platformMask) const override {
    int result = 0;
    if (platformMask & GetShiftMask())   result |= ModifierMasks::SHIFT;
    if (platformMask & GetControlMask()) result |= ModifierMasks::CONTROL;
    if (platformMask & GetAltMask())     result |= ModifierMasks::ALT;
    if (platformMask & GetMetaMask())    result |= ModifierMasks::META;
    if (platformMask & GetLockMask())    result |= ModifierMasks::LOCK;
    return result;
  }
  int ToPlatformMask(int abstractMask) const override {
    int result = 0;
    if (abstractMask & ModifierMasks::SHIFT)   result |= GetShiftMask();
    if (abstractMask & ModifierMasks::CONTROL) result |= GetControlMask();
    if (abstractMask & ModifierMasks::ALT)     result |= GetAltMask();
    if (abstractMask & ModifierMasks::META)    result |= GetMetaMask();
    if (abstractMask & ModifierMasks::LOCK)    result |= GetLockMask();
    return result;
  }

    VirtualKeyboard& virtualKeyboard() { return *vkb_; }
    VirtualPointer& virtualPointer() { return *vptr_; }
    WaylandClipboardBackend& clipboard() { return *clipboard_; }
    ForeignToplevel& foreignToplevel() { return *toplevel_; }

private:
    EventListener *eventListener_ = nullptr;
    WaylandProtocolClient *protoClient_ = nullptr;

    std::unique_ptr<VirtualKeyboard> vkb_;
    std::unique_ptr<VirtualPointer> vptr_;
    std::unique_ptr<WaylandClipboardBackend> clipboard_;
    std::unique_ptr<ForeignToplevel> toplevel_;

    bool useProtocolKb_ = false;
    bool useProtocolPtr_ = false;
};

} // namespace havel
