#include "VirtualPointer.hpp"
#include "WaylandProtocolClient.hpp"
#include "core/io/UinputDevice.hpp"
#include "utils/Logger.hpp"

#include <wayland-client.h>
#include <linux/input.h>
#include <chrono>
#include <thread>
#include <cstring>

extern "C" {
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
}

namespace havel {

VirtualPointer::VirtualPointer(WaylandProtocolClient &client)
    : client_(client) {}

VirtualPointer::~VirtualPointer() {
    cleanup();
}

bool VirtualPointer::initialize() {
    if (vptr_) return true;

    if (!client_.hasVirtualPointer()) {
        debug("VirtualPointer: compositor does not support virtual-pointer protocol");
        return false;
    }

    vptr_ = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
        client_.virtualPointerManager(), client_.seat());

    if (!vptr_) {
        error("VirtualPointer: failed to create virtual pointer");
        return false;
    }

    client_.roundtrip();
    info("VirtualPointer: initialized successfully");
    return true;
}

void VirtualPointer::cleanup() {
    if (vptr_) {
        zwlr_virtual_pointer_v1_destroy(vptr_);
        vptr_ = nullptr;
    }
}

uint32_t VirtualPointer::timestamp() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint32_t>(ms & 0xFFFFFFFF);
}

bool VirtualPointer::moveRelative(double dx, double dy) {
    if (vptr_) {
        zwlr_virtual_pointer_v1_motion(vptr_, timestamp(),
                                        wl_fixed_from_double(dx),
                                        wl_fixed_from_double(dy));
        frame();
        return true;
    }

    if (uinput_) {
        if (dx != 0) uinput_->SendEvent(EV_REL, REL_X, static_cast<int>(dx));
        if (dy != 0) uinput_->SendEvent(EV_REL, REL_Y, static_cast<int>(dy));
        uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
        return true;
    }

    return false;
}

bool VirtualPointer::moveAbsolute(uint32_t x, uint32_t y, uint32_t xExtent, uint32_t yExtent) {
    if (vptr_) {
        zwlr_virtual_pointer_v1_motion_absolute(vptr_, timestamp(),
                                                  x, y, xExtent, yExtent);
        frame();
        return true;
    }

    return false;
}

bool VirtualPointer::clickButton(uint32_t button, bool down) {
    if (vptr_) {
        zwlr_virtual_pointer_v1_button(vptr_, timestamp(), button,
                                        down ? WL_POINTER_BUTTON_STATE_PRESSED
                                             : WL_POINTER_BUTTON_STATE_RELEASED);
        frame();
        return true;
    }

    if (uinput_) {
        uinput_->SendEvent(EV_KEY, button, down ? 1 : 0);
        uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
        return true;
    }

    return false;
}

bool VirtualPointer::tapButton(uint32_t button, uint32_t delayMs) {
    if (!clickButton(button, true)) return false;
    if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    return clickButton(button, false);
}

bool VirtualPointer::scrollAxis(uint32_t axis, double value, int32_t discrete) {
    if (vptr_) {
        if (discrete != 0) {
            zwlr_virtual_pointer_v1_axis_discrete(vptr_, timestamp(), axis,
                                                    wl_fixed_from_double(value), discrete);
        } else {
            zwlr_virtual_pointer_v1_axis(vptr_, timestamp(), axis,
                                          wl_fixed_from_double(value));
        }
        frame();
        return true;
    }

    if (uinput_) {
        int code = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? REL_WHEEL : REL_HWHEEL;
        uinput_->SendEvent(EV_REL, code, discrete != 0 ? discrete : static_cast<int>(value));
        uinput_->SendEvent(EV_SYN, SYN_REPORT, 0);
        return true;
    }

    return false;
}

bool VirtualPointer::scrollDiscrete(int32_t dy, int32_t dx) {
    bool ok = true;
    if (dy != 0) {
        ok = scrollAxis(WL_POINTER_AXIS_VERTICAL_SCROLL, static_cast<double>(dy) * 120.0, dy) && ok;
    }
    if (dx != 0) {
        ok = scrollAxis(WL_POINTER_AXIS_HORIZONTAL_SCROLL, static_cast<double>(dx) * 120.0, dx) && ok;
    }
    return ok;
}

bool VirtualPointer::scrollSmooth(double dy, double dx) {
    bool ok = true;
    if (dy != 0.0) {
        ok = scrollAxis(WL_POINTER_AXIS_VERTICAL_SCROLL, dy) && ok;
    }
    if (dx != 0.0) {
        ok = scrollAxis(WL_POINTER_AXIS_HORIZONTAL_SCROLL, dx) && ok;
    }
    return ok;
}

void VirtualPointer::frame() {
    if (vptr_) {
        zwlr_virtual_pointer_v1_frame(vptr_);
        wl_display_flush(client_.display());
    }
}

uint32_t VirtualPointer::buttonNameToLinux(const std::string &name) {
    if (name == "left" || name == "1") return BTN_LEFT;
    if (name == "middle" || name == "2") return BTN_MIDDLE;
    if (name == "right" || name == "3") return BTN_RIGHT;
    if (name == "side" || name == "4" || name == "back") return BTN_SIDE;
    if (name == "extra" || name == "5" || name == "forward") return BTN_EXTRA;
    if (name == "task" || name == "6") return BTN_TASK;
    return BTN_LEFT;
}

} // namespace havel
