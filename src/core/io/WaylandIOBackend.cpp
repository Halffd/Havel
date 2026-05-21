#include "WaylandIOBackend.hpp"
#include "EventListener.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include <linux/input.h>

namespace havel {

WaylandIOBackend::WaylandIOBackend(EventListener *eventListener)
    : eventListener_(eventListener) {}

WaylandIOBackend::~WaylandIOBackend() { Cleanup(); }

bool WaylandIOBackend::Initialize() { return eventListener_ != nullptr; }
void WaylandIOBackend::Cleanup() {}
bool WaylandIOBackend::IsAvailable() const { return eventListener_ != nullptr; }
std::string WaylandIOBackend::GetName() const { return "wayland"; }

void WaylandIOBackend::PressKey(int keycode) {
    if (!eventListener_) return;
    eventListener_->SendUinputEvent(EV_KEY, keycode, 1);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
}

void WaylandIOBackend::ReleaseKey(int keycode) {
    if (!eventListener_) return;
    eventListener_->SendUinputEvent(EV_KEY, keycode, 0);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
}

bool WaylandIOBackend::MovePointer(int dx, int dy) {
    if (!eventListener_) return false;
    if (dx != 0) eventListener_->SendUinputEvent(EV_REL, REL_X, dx);
    if (dy != 0) eventListener_->SendUinputEvent(EV_REL, REL_Y, dy);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    return true;
}

bool WaylandIOBackend::MovePointerTo(int x, int y) {
    // Wayland doesn't support absolute positioning directly.
    // IO handles smooth animation with feedback loop.
    return false;
}

std::pair<int, int> WaylandIOBackend::GetCursorPosition() {
    return {0, 0};
}

void WaylandIOBackend::SendButton(int button, bool down) {
    if (!eventListener_) return;
    eventListener_->SendUinputEvent(EV_KEY, button, down ? 1 : 0);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
}

bool WaylandIOBackend::RegisterHotkey(int keycode, int modifiers, bool isButton) {
    return false;
}

bool WaylandIOBackend::UnregisterHotkey(int keycode, int modifiers, bool isButton) {
    return false;
}

void WaylandIOBackend::UnregisterAll() {}

bool WaylandIOBackend::GrabKeyboard() { return false; }

bool WaylandIOBackend::IsKeyDown(int keycode) {
    if (eventListener_) return eventListener_->GetKeyState(keycode);
    return false;
}

bool WaylandIOBackend::IsAnyKeyDown() {
    return false;
}

bool WaylandIOBackend::SetupXInput2() { return false; }
bool WaylandIOBackend::SetHardwareSensitivity(double) { return false; }

void WaylandIOBackend::TypeText(const std::string &text) {
    // Will be handled by IO sending individual keystrokes via Send()
}

} // namespace havel
