#include "WindowsIOBackend.hpp"
#include "utils/Logger.hpp"

#ifdef WINDOWS
#include <windows.h>
#endif

namespace havel {

#ifdef WINDOWS

WindowsIOBackend::WindowsIOBackend() {}
WindowsIOBackend::~WindowsIOBackend() { Cleanup(); }

bool WindowsIOBackend::Initialize() { return true; }
void WindowsIOBackend::Cleanup() {}
bool WindowsIOBackend::IsAvailable() const { return true; }
std::string WindowsIOBackend::GetName() const { return "windows"; }

void WindowsIOBackend::PressKey(int keycode) {
    keybd_event((BYTE)keycode, 0, 0, 0);
}

void WindowsIOBackend::ReleaseKey(int keycode) {
    keybd_event((BYTE)keycode, 0, KEYEVENTF_KEYUP, 0);
}

bool WindowsIOBackend::MovePointer(int dx, int dy) {
    POINT p;
    GetCursorPos(&p);
    SetCursorPos(p.x + dx, p.y + dy);
    return true;
}

bool WindowsIOBackend::MovePointerTo(int x, int y) {
    SetCursorPos(x, y);
    return true;
}

std::pair<int, int> WindowsIOBackend::GetCursorPosition() {
    POINT p;
    GetCursorPos(&p);
    return {p.x, p.y};
}

void WindowsIOBackend::SendButton(int button, bool down) {
    DWORD dwFlags = down ? 0 : MOUSEEVENTF_KEYUP;
    switch (button) {
    case 1: dwFlags |= MOUSEEVENTF_LEFTDOWN; break;
    case 2: dwFlags |= MOUSEEVENTF_RIGHTDOWN; break;
    case 3: dwFlags |= MOUSEEVENTF_MIDDLEDOWN; break;
    default: break;
    }
    if (dwFlags) mouse_event(dwFlags, 0, 0, 0, 0);
}

bool WindowsIOBackend::RegisterHotkey(int, int, bool) { return false; }
bool WindowsIOBackend::UnregisterHotkey(int, int, bool) { return false; }
void WindowsIOBackend::UnregisterAll() {}
bool WindowsIOBackend::GrabKeyboard() { return false; }

bool WindowsIOBackend::IsKeyDown(int keycode) {
    return (GetAsyncKeyState(keycode) & 0x8000) != 0;
}

bool WindowsIOBackend::IsAnyKeyDown() {
    for (int i = 0; i < 256; i++) {
        if (GetAsyncKeyState(i) & 0x8000) return true;
    }
    return false;
}

bool WindowsIOBackend::SetupXInput2() { return false; }
bool WindowsIOBackend::SetHardwareSensitivity(double) { return false; }

void WindowsIOBackend::TypeText(const std::string &text) {
    if (text.empty()) return;
    if (OpenClipboard(nullptr)) {
        HANDLE hOldClip = GetClipboardData(CF_TEXT);
        std::string oldText;
        if (hOldClip) {
            char *pOld = static_cast<char*>(GlobalLock(hOldClip));
            if (pOld) { oldText = pOld; GlobalUnlock(hOldClip); }
        }
        EmptyClipboard();
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hGlobal) {
            char *pBuffer = static_cast<char*>(GlobalLock(hGlobal));
            if (pBuffer) {
                memcpy(pBuffer, text.c_str(), text.size() + 1);
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_TEXT, hGlobal);
            }
        }
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('V', 0, 0, 0);
        keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        if (!oldText.empty()) {
            EmptyClipboard();
            hGlobal = GlobalAlloc(GMEM_MOVEABLE, oldText.size() + 1);
            if (hGlobal) {
                char *pBuffer = static_cast<char*>(GlobalLock(hGlobal));
                if (pBuffer) {
                    memcpy(pBuffer, oldText.c_str(), oldText.size() + 1);
                    GlobalUnlock(hGlobal);
                    SetClipboardData(CF_TEXT, hGlobal);
                }
            }
        }
        CloseClipboard();
    }
}

#endif // WINDOWS

} // namespace havel
