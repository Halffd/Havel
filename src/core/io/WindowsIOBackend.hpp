#pragma once
#include "IOBackend.hpp"
#ifdef WINDOWS
#include <windows.h>
#endif
#include <map>

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
    void UngrabKeyboard() override;

    bool IsKeyDown(int keycode) override;
    bool IsAnyKeyDown() override;

    bool SetupXInput2() override;
    bool SetHardwareSensitivity(double sensitivity) override;

    void TypeText(const std::string &text) override;

    XkbCharMapping CharToKeycode(char32_t cp) override;

  int GetShiftMask() const override { return MOD_SHIFT; }
  int GetControlMask() const override { return MOD_CONTROL; }
  int GetAltMask() const override { return MOD_ALT; }
  int GetMetaMask() const override { return MOD_WIN; }
  int GetLockMask() const override { return 0; }
  int GetNumLockMask() override { return 0; }

  int ToAbstractMask(int platformMask) const override {
    int result = 0;
    if (platformMask & MOD_SHIFT)   result |= ModifierMasks::SHIFT;
    if (platformMask & MOD_CONTROL) result |= ModifierMasks::CONTROL;
    if (platformMask & MOD_ALT)     result |= ModifierMasks::ALT;
    if (platformMask & MOD_WIN)     result |= ModifierMasks::META;
    return result;
  }
  int ToPlatformMask(int abstractMask) const override {
    int result = 0;
    if (abstractMask & ModifierMasks::SHIFT)   result |= MOD_SHIFT;
    if (abstractMask & ModifierMasks::CONTROL) result |= MOD_CONTROL;
    if (abstractMask & ModifierMasks::ALT)     result |= MOD_ALT;
    if (abstractMask & ModifierMasks::META)    result |= MOD_WIN;
    return result;
  }

private:
  // RegisterHotKey takes a caller-chosen id, but the IOBackend interface does
  // not expose one, so ids are allocated here and kept so that unregistering
  // by keycode can find them again. Windows allows hotkey ids in 0x0000-0xBFFF.
  std::map<int, int> registeredHotkeys_;
  int nextHotkeyId_ = 0;
  bool keyboardBlocked_ = false;
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
  void UngrabKeyboard() override {}
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
  int ToAbstractMask(int) const override { return 0; }
  int ToPlatformMask(int) const override { return 0; }
};
#endif

} // namespace havel
