#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace havel {

class EventListener;

struct XkbCharMapping {
  int keycode = -1;
  bool needsShift = false;
};

// The platform-neutral modifier bits.
//
// These are the SAME bits the evdev input path uses (EventListener's
// Modifier enum and the evdev modifier mask in EvdevAdapter):
//
//   Ctrl = bit 0, Shift = bit 1, Alt = bit 2, Meta = bit 3
//
// One layout for every backend. It used to differ: the non-evdev branch of
// ParseModifiersAndFlags() produced SHIFT=bit 0 / CONTROL=bit 1, while the
// hotkey matchers (EventListener::CheckModifierMatch and
// HotkeyManager::checkModifierMatch) read the evdev layout - Ctrl=bit 0,
// Shift=bit 1. On the X11 path the two disagreed, so Ctrl and Shift were
// swapped in the match: a hotkey registered as ^A (Ctrl) was matched as if
// it required Shift.
//
// Every backend's ToAbstractMask()/ToPlatformMask() uses these constants,
// so aligning them with the evdev layout fixes the X11 path without
// touching the conversion functions.
namespace ModifierMasks {
constexpr int CONTROL = (1 << 0);
constexpr int SHIFT = (1 << 1);
constexpr int ALT = (1 << 2);
constexpr int META = (1 << 3);
constexpr int LOCK = (1 << 4);
constexpr int NUMLOCK = (1 << 5);
}

/**
 * One key transition, in platform-neutral terms.
 *
 * This exists because the concrete event types do not cross the boundary:
 * evdev has `struct input_event`, X11 has XEvent + keysym, Windows has either
 * a KBDLLHOOKSTRUCT or a SendInput INPUT. Callers used to reach for
 * `input_event` directly, which is why core/io/IO.hpp could not compile
 * anywhere but Linux. A backend translates a KeyEventRequest into whatever
 * its platform actually wants.
 *
 * `keycode` is a platform keycode as produced by CharToKeycode() or the
 * backend's own parse step - not an evdev code.
 */
struct KeyEventRequest {
  enum class Action : uint8_t { Press, Release };

  int keycode = 0;
  Action action = Action::Press;

  static KeyEventRequest Press(int keycode) { return {keycode, Action::Press}; }
  static KeyEventRequest Release(int keycode) { return {keycode, Action::Release}; }
};

class IOBackend {
public:
  virtual ~IOBackend() = default;

  virtual bool Initialize() = 0;
  virtual void Cleanup() = 0;
  virtual bool IsAvailable() const = 0;
  virtual std::string GetName() const = 0;

  // Key send (platform API - XTest, uinput, SendInput)
  virtual void PressKey(int keycode) = 0;
  virtual void ReleaseKey(int keycode) = 0;

  /**
   * Replay a queued run of key transitions in order.
   *
   * Present as its own hook because callers batch: parsing a whole dsl key
   * string first and injecting afterwards keeps the injection burst tight,
   * which matters because each transition is a separate OS call. Backends
   * that can do better than one call per event (XInput2, RawInput batching)
   * override this; the default replays through PressKey/ReleaseKey, which is
   * correct everywhere.
   */
  virtual void SendBatchedKeyEvents(const std::vector<KeyEventRequest> &events) {
    for (const KeyEventRequest &e : events) {
      if (e.action == KeyEventRequest::Action::Press)
        PressKey(e.keycode);
      else
        ReleaseKey(e.keycode);
    }
  }

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
  virtual void UngrabKeyboard() {}

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

  // Platform-to-abstract mask conversion
  virtual int ToAbstractMask(int platformMask) const = 0;
  virtual int ToPlatformMask(int abstractMask) const = 0;

  // Character-to-keycode mapping (xkbcommon on X11, stub on others)
  virtual XkbCharMapping CharToKeycode(char32_t cp) { return {}; }

  // Factory
  static std::unique_ptr<IOBackend> Create(EventListener *eventListener);
};

} // namespace havel
