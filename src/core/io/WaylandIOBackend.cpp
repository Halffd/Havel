#include "WaylandIOBackend.hpp"
#include "EventListener.hpp"
#include "core/wayland/WaylandProtocolClient.hpp"
#include "core/io/UinputDevice.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include <linux/input.h>

namespace havel {

WaylandIOBackend::WaylandIOBackend(EventListener *eventListener)
: eventListener_(eventListener) {}

WaylandIOBackend::~WaylandIOBackend() { Cleanup(); }

bool WaylandIOBackend::Initialize() {
    if (!eventListener_) return false;

    protoClient_ = &WaylandProtocolClient::instance();
    if (!protoClient_->isConnected() && !protoClient_->connect()) {
        info("WaylandIOBackend: Wayland connection unavailable, uinput only");
        return true;
    }

    vkb_ = std::make_unique<VirtualKeyboard>(*protoClient_);
    if (vkb_->initialize()) {
        useProtocolKb_ = true;
        info("WaylandIOBackend: using virtual-keyboard protocol");
    } else {
        info("WaylandIOBackend: virtual-keyboard unavailable, using uinput fallback");
    }

    vptr_ = std::make_unique<VirtualPointer>(*protoClient_);
    if (vptr_->initialize()) {
        useProtocolPtr_ = true;
        info("WaylandIOBackend: using virtual-pointer protocol");
    } else {
        info("WaylandIOBackend: virtual-pointer unavailable, using uinput fallback");
    }

    clipboard_ = std::make_unique<WaylandClipboardBackend>(*protoClient_);
    if (!clipboard_->initialize()) {
        debug("WaylandIOBackend: data-control protocol unavailable");
    }

    toplevel_ = std::make_unique<ForeignToplevel>(*protoClient_);
    if (!toplevel_->initialize()) {
        debug("WaylandIOBackend: foreign-toplevel protocol unavailable");
    }

    return true;
}

void WaylandIOBackend::Cleanup() {
    if (toplevel_) { toplevel_->cleanup(); toplevel_.reset(); }
    if (clipboard_) { clipboard_->cleanup(); clipboard_.reset(); }
    if (vptr_) { vptr_->cleanup(); vptr_.reset(); }
    if (vkb_) { vkb_->cleanup(); vkb_.reset(); }
    useProtocolKb_ = false;
    useProtocolPtr_ = false;
}

bool WaylandIOBackend::IsAvailable() const { return eventListener_ != nullptr; }
std::string WaylandIOBackend::GetName() const { return "wayland"; }

void WaylandIOBackend::PressKey(int keycode) {
    if (useProtocolKb_ && vkb_) {
        vkb_->pressKey(static_cast<uint32_t>(keycode));
        return;
    }
    if (!eventListener_) return;
    eventListener_->SendUinputEvent(EV_KEY, keycode, 1);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
}

void WaylandIOBackend::ReleaseKey(int keycode) {
    if (useProtocolKb_ && vkb_) {
        vkb_->releaseKey(static_cast<uint32_t>(keycode));
        return;
    }
    if (!eventListener_) return;
    eventListener_->SendUinputEvent(EV_KEY, keycode, 0);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
}

bool WaylandIOBackend::MovePointer(int dx, int dy) {
    if (useProtocolPtr_ && vptr_) {
        return vptr_->moveRelative(static_cast<double>(dx), static_cast<double>(dy));
    }
    if (!eventListener_) return false;
    if (dx != 0) eventListener_->SendUinputEvent(EV_REL, REL_X, dx);
    if (dy != 0) eventListener_->SendUinputEvent(EV_REL, REL_Y, dy);
    eventListener_->SendUinputEvent(EV_SYN, SYN_REPORT, 0);
    return true;
}

bool WaylandIOBackend::MovePointerTo(int x, int y) {
    if (useProtocolPtr_ && vptr_ && protoClient_) {
        auto &outputs = protoClient_->outputs();
        if (!outputs.empty()) {
            auto &out = outputs[0];
            return vptr_->moveAbsolute(static_cast<uint32_t>(x),
                                        static_cast<uint32_t>(y),
                                        static_cast<uint32_t>(out.width),
                                        static_cast<uint32_t>(out.height));
        }
    }
    return false;
}

std::pair<int, int> WaylandIOBackend::GetCursorPosition() {
    if (useProtocolPtr_ && vptr_ && protoClient_) {
        auto active = toplevel_ ? toplevel_->activeWindow() : ForeignToplevelWindow();
        if (active.handle) {
            return {active.x, active.y};
        }
    }
    return {0, 0};
}

void WaylandIOBackend::SendButton(int button, bool down) {
    if (useProtocolPtr_ && vptr_) {
        vptr_->clickButton(static_cast<uint32_t>(button), down);
        return;
    }
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
    if (useProtocolKb_ && vkb_) {
        return vkb_->isKeyDown(static_cast<uint32_t>(keycode));
    }
    if (eventListener_) return eventListener_->GetKeyState(keycode);
    return false;
}

bool WaylandIOBackend::IsAnyKeyDown() {
    return false;
}

bool WaylandIOBackend::SetupXInput2() { return false; }
bool WaylandIOBackend::SetHardwareSensitivity(double) { return false; }

void WaylandIOBackend::TypeText(const std::string &text) {
    if (useProtocolKb_ && vkb_) {
        vkb_->typeText(text);
        return;
    }
}

} // namespace havel
