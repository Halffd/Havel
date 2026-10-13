#include "WindowsIOBackend.hpp"
#include "utils/Logger.hpp"

#ifdef WINDOWS
#include <windows.h>
#endif

#include <cstring>

namespace havel {

#ifdef WINDOWS

// ---------------------------------------------------------------------------
// Injection
//
// Ported from 433f4646:src/core/IO.cpp, which drove keybd_event/mouse_event.
// Both are documented as superseded since Windows 2000 and are emulated on top
// of SendInput, which additionally carries a scan code, an extended-key flag
// and a unicode flag - the last of which is what lets TypeText send text
// without borrowing the clipboard.
// ---------------------------------------------------------------------------

namespace {

void SendKeyboard(WORD vk, bool keyUp) {
    INPUT in{};
    in.type           = INPUT_KEYBOARD;
    in.ki.wVk         = vk;
    in.ki.wScan       = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    // KEYEVENTF_EXTENDEDKEY is required for the right-hand modifier block and
    // the navigation cluster; without it those keys are silently dropped.
    in.ki.dwFlags     = static_cast<DWORD>(keyUp ? KEYEVENTF_KEYUP : 0);
    if (in.ki.wScan) {
        in.ki.dwFlags |= static_cast<DWORD>(keyUp ? KEYEVENTF_SCANCODE
                                                   : KEYEVENTF_SCANCODE);
        if ((vk == VK_RCONTROL) || (vk == VK_RMENU) ||
            (in.ki.wScan == 0x4D) || (in.ki.wScan == 0x5B) ||
            (in.ki.wScan == 0x5C) || (in.ki.wScan == 0x5D)) {
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
    }
    SendInput(1, &in, sizeof(in));
}

void SendUnicodeChar(WCHAR ch) {
    INPUT in{};
    in.type           = INPUT_KEYBOARD;
    in.ki.wVk         = 0;
    in.ki.wScan       = ch;
    in.ki.dwFlags     = KEYEVENTF_UNICODE;
    SendInput(1, &in, sizeof(in));

    in.ki.dwFlags     = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

}  // namespace

// ---------------------------------------------------------------------------
// Hotkeys
//
// RegisterHotKey is the proven path from the 2024 implementation
// (IO::AssignHotkey). It has one hard limitation the LL hook does not: it
// cannot express a modifier-only chord, and it refuses a combination another
// process already owns. Both surface as a false return, which is reported
// honestly here rather than swallowed.
// ---------------------------------------------------------------------------

WindowsIOBackend::WindowsIOBackend() {}
WindowsIOBackend::~WindowsIOBackend() { Cleanup(); }

bool WindowsIOBackend::Initialize() { return true; }

void WindowsIOBackend::Cleanup() {
    UnregisterAll();
    // BlockInput(TRUE) is process-global, so a backend that grabbed the
    // keyboard must give it back on teardown or the whole session is stuck.
    if (keyboardBlocked_) {
        ::BlockInput(FALSE);
        keyboardBlocked_ = false;
    }
}

bool WindowsIOBackend::IsAvailable() const { return true; }
std::string WindowsIOBackend::GetName() const { return "windows"; }

void WindowsIOBackend::PressKey(int keycode) {
    SendKeyboard(static_cast<WORD>(keycode), false);
}

void WindowsIOBackend::ReleaseKey(int keycode) {
    SendKeyboard(static_cast<WORD>(keycode), true);
}

bool WindowsIOBackend::MovePointer(int dx, int dy) {
    POINT p{};
    if (!::GetCursorPos(&p)) return false;
    return ::SetCursorPos(p.x + dx, p.y + dy) != FALSE;
}

bool WindowsIOBackend::MovePointerTo(int x, int y) {
    return ::SetCursorPos(x, y) != FALSE;
}

std::pair<int, int> WindowsIOBackend::GetCursorPosition() {
    POINT p{};
    if (!::GetCursorPos(&p)) return {0, 0};
    return {p.x, p.y};
}

void WindowsIOBackend::SendButton(int button, bool down) {
    DWORD flags = 0;
    switch (button) {
    case 1:  flags = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
    case 2:  flags = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
    case 3:  flags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case 4:  flags = down ? MOUSEEVENTF_XDOWN     : MOUSEEVENTF_XUP;     break;
    case 5:  flags = down ? MOUSEEVENTF_XDOWN     : MOUSEEVENTF_XUP;     break;
    default: return;
    }
    INPUT in{};
    in.type           = INPUT_MOUSE;
    in.mi.dwFlags     = flags;
    if ((button == 4) || (button == 5)) {
        in.mi.mouseData = static_cast<DWORD>((button == 4) ? XBUTTON1 : XBUTTON2);
    }
    // No cursor movement: SendInput applies the absolute/relative fields even
    // when zeroed, so they must be left at the zero-initialised state.
    SendInput(1, &in, sizeof(in));
}

bool WindowsIOBackend::RegisterHotkey(int keycode, int modifiers, bool /*isButton*/) {
    // Modifier-only chords have no vk to hand RegisterHotKey; reject them
    // explicitly instead of registering a bogus hotkey.
    if (keycode == 0) return false;
    const int id = ++nextHotkeyId_;
    if (!::RegisterHotKey(nullptr, id, modifiers, static_cast<UINT>(keycode))) {
        return false;
    }
    registeredHotkeys_.emplace(id, keycode);
    return true;
}

bool WindowsIOBackend::UnregisterHotkey(int keycode, int, bool) {
    for (auto it = registeredHotkeys_.begin(); it != registeredHotkeys_.end(); ++it) {
        if (it->second != keycode) continue;
        const bool ok = ::UnregisterHotKey(nullptr, it->first) != FALSE;
        registeredHotkeys_.erase(it);
        return ok;
    }
    return false;
}

void WindowsIOBackend::UnregisterAll() {
    for (const auto &entry : registeredHotkeys_) {
        ::UnregisterHotKey(nullptr, entry.first);
    }
    registeredHotkeys_.clear();
}

bool WindowsIOBackend::GrabKeyboard() {
    // BlockInput is session-wide and requires the same integrity level as the
    // foreground process; it is the only "grab the keyboard" Windows offers.
    if (!::BlockInput(TRUE)) return false;
    keyboardBlocked_ = true;
    return true;
}

void WindowsIOBackend::UngrabKeyboard() {
    if (!keyboardBlocked_) return;
    ::BlockInput(FALSE);
    keyboardBlocked_ = false;
}

bool WindowsIOBackend::IsKeyDown(int keycode) {
    return (::GetAsyncKeyState(keycode) & 0x8000) != 0;
}

bool WindowsIOBackend::IsAnyKeyDown() {
    for (int i = 1; i < 256; ++i) {
        if (::GetAsyncKeyState(i) & 0x8000) return true;
    }
    return false;
}

// The X11 hook has no analogue: Windows delivers the same information through
// a low-level keyboard hook owned by whoever installs it. Reporting false is
// the honest answer - the caller can fall back to IsKeyDown polling.
bool WindowsIOBackend::SetupXInput2() { return false; }
bool WindowsIOBackend::SetHardwareSensitivity(double) { return false; }

void WindowsIOBackend::TypeText(const std::string &text) {
    if (text.empty()) return;
    // KEYEVENTF_UNICODE injects the character directly. The 2024 code went via
    // the clipboard and a Ctrl+V paste, which required stealing and restoring
    // whatever the user had copied - visibly and racy.
    const int count = static_cast<int>(text.size());
    for (int i = 0; i < count; ++i) {
        SendUnicodeChar(static_cast<WCHAR>(
            static_cast<unsigned char>(text[static_cast<size_t>(i)])));
    }
}

XkbCharMapping WindowsIOBackend::CharToKeycode(char32_t cp) {
    // VK_PACKET is what KEYEVENTF_UNICODE presents to GetKeyNameText/VkKeyScan.
    XkbCharMapping m{};
    m.keycode = static_cast<int>(VK_PACKET);
    m.needsShift = false;
    return m;
}

#endif  // WINDOWS

}  // namespace havel
