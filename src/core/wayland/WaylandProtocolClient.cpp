#include "WaylandProtocolClient.hpp"
#include "utils/Logger.hpp"

#include <wayland-client.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <unistd.h>

extern "C" {
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
}

#ifdef HAS_EXT_DATA_CONTROL
#include "ext-data-control-v1-client-protocol.h"
#endif

namespace havel {

const struct wl_registry_listener WaylandProtocolClient::registryListener_ = {
    .global = registryHandleGlobal,
    .global_remove = registryHandleGlobalRemove,
};

const struct wl_seat_listener WaylandProtocolClient::seatListener_ = {
    .capabilities = seatHandleCapabilities,
    .name = seatHandleName,
};

const struct wl_output_listener WaylandProtocolClient::outputListener_ = {
    .geometry = outputHandleGeometry,
    .mode = outputHandleMode,
    .done = outputHandleDone,
    .scale = outputHandleScale,
    .name = outputHandleName,
    .description = outputHandleDescription,
};

WaylandProtocolClient& WaylandProtocolClient::instance() {
    static WaylandProtocolClient inst;
    return inst;
}

WaylandProtocolClient::~WaylandProtocolClient() {
    disconnect();
}

bool WaylandProtocolClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) return true;

    display_ = wl_display_connect(nullptr);
    if (!display_) {
        error("WaylandProtocolClient: failed to connect to display");
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    if (!registry_) {
        error("WaylandProtocolClient: failed to get registry");
        wl_display_disconnect(display_);
        display_ = nullptr;
        return false;
    }

    wl_registry_add_listener(registry_, &registryListener_, this);

    if (wl_display_roundtrip(display_) < 0) {
        error("WaylandProtocolClient: roundtrip failed");
        disconnect();
        return false;
    }

    if (!seat_) {
        debug("WaylandProtocolClient: no seat found, trying another roundtrip");
        if (wl_display_roundtrip(display_) < 0) {
            error("WaylandProtocolClient: second roundtrip failed");
            disconnect();
            return false;
        }
    }

    connected_ = true;
    initialized_ = true;

    info("WaylandProtocolClient: connected, compositor={}, seat={}, vkb={}, vptr={}, ftl={}, dc={}, edc={}",
         compositorName_.empty() ? "unknown" : compositorName_,
         seat_ ? "yes" : "no",
         hasVirtualKeyboard() ? "yes" : "no",
         hasVirtualPointer() ? "yes" : "no",
         hasForeignToplevel() ? "yes" : "no",
         hasWlrDataControl() ? "yes" : "no",
         hasExtDataControl() ? "yes" : "no");

    return true;
}

void WaylandProtocolClient::disconnect() {
    if (foreignToplevelManager_) {
        zwlr_foreign_toplevel_manager_v1_destroy(foreignToplevelManager_);
        foreignToplevelManager_ = nullptr;
    }
    if (dataControlManager_) {
        zwlr_data_control_manager_v1_destroy(dataControlManager_);
        dataControlManager_ = nullptr;
    }
    if (extDataControlManager_) {
#ifdef HAS_EXT_DATA_CONTROL
        ext_data_control_manager_v1_destroy(extDataControlManager_);
#endif
        extDataControlManager_ = nullptr;
    }
    if (virtualKeyboardManager_) {
        zwp_virtual_keyboard_manager_v1_destroy(virtualKeyboardManager_);
        virtualKeyboardManager_ = nullptr;
    }
    if (virtualPointerManager_) {
        zwlr_virtual_pointer_manager_v1_destroy(virtualPointerManager_);
        virtualPointerManager_ = nullptr;
    }
    if (seat_) {
        wl_seat_release(seat_);
        seat_ = nullptr;
    }
    for (auto &out : outputs_) {
        if (out.output) wl_output_release(out.output);
    }
    outputs_.clear();
    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    if (display_) {
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
    connected_ = false;
    initialized_ = false;
    protocolList_.clear();
    compositorName_.clear();
}

bool WaylandProtocolClient::roundtrip() {
    if (!display_) return false;
    return wl_display_roundtrip(display_) >= 0;
}

void WaylandProtocolClient::dispatch(int timeoutMs) {
    if (!display_) return;

    if (timeoutMs >= 0) {
        struct pollfd pfd;
        pfd.fd = wl_display_get_fd(display_);
        pfd.events = POLLIN;

        wl_display_dispatch_pending(display_);
        wl_display_flush(display_);

        while (poll(&pfd, 1, timeoutMs) > 0) {
            wl_display_dispatch_pending(display_);
            wl_display_flush(display_);
        }
    } else {
        wl_display_dispatch_pending(display_);
    }
}

int WaylandProtocolClient::pollFd() const {
    return display_ ? wl_display_get_fd(display_) : -1;
}

void WaylandProtocolClient::registryHandleGlobal(void *data, struct wl_registry *registry,
                                                   uint32_t name, const char *interface,
                                                   uint32_t version) {
    auto *self = static_cast<WaylandProtocolClient *>(data);

    WaylandProtocolInfo info{interface, version};
    self->protocolList_.push_back(info);

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min(version, 5u)));
        self->seatGlobalId_ = name;
        wl_seat_add_listener(self->seat_, &seatListener_, self);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto *output = static_cast<wl_output *>(
            wl_registry_bind(registry, name, &wl_output_interface,
                             std::min(version, 4u)));
        WaylandOutputInfo outInfo;
        outInfo.output = output;
        self->outputs_.push_back(outInfo);
        wl_output_add_listener(output, &outputListener_, &self->outputs_.back());
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        self->virtualKeyboardManager_ = static_cast<zwp_virtual_keyboard_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface,
                             std::min(version, 1u)));
    } else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        self->virtualPointerManager_ = static_cast<zwlr_virtual_pointer_manager_v1 *>(
            wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface,
                             std::min(version, 2u)));
    } else if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        self->foreignToplevelManager_ = static_cast<zwlr_foreign_toplevel_manager_v1 *>(
            wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
                             std::min(version, 3u)));
    } else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
        self->dataControlManager_ = static_cast<zwlr_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface,
                             std::min(version, 2u)));
    } else if (strcmp(interface,
#ifdef HAS_EXT_DATA_CONTROL
        ext_data_control_manager_v1_interface.name
#else
        "ext_data_control_manager_v1"
#endif
    ) == 0) {
#ifdef HAS_EXT_DATA_CONTROL
        self->extDataControlManager_ = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface,
                             std::min(version, 1u)));
#endif
    } else if (strcmp(interface, "wl_compositor") == 0 ||
               strcmp(interface, "zwp_tablet_manager_v2") == 0 ||
               strcmp(interface, "zwlr_layer_shell_v1") == 0 ||
               strcmp(interface, "zwlr_export_dmabuf_manager_v1") == 0) {
    }

    if (strcmp(interface, "zwlr_layer_shell_v1") == 0 && self->compositorName_.empty()) {
        self->compositorName_ = "wlroots-based";
    }
}

void WaylandProtocolClient::registryHandleGlobalRemove(void *data,
                                                        struct wl_registry *,
                                                        uint32_t name) {
    auto *self = static_cast<WaylandProtocolClient *>(data);
    if (name == self->seatGlobalId_) {
        self->seat_ = nullptr;
    }
    self->outputs_.erase(
        std::remove_if(self->outputs_.begin(), self->outputs_.end(),
                       [name](const WaylandOutputInfo &out) { (void)out; (void)name; return false; }),
        self->outputs_.end());
}

void WaylandProtocolClient::seatHandleCapabilities(void *, struct wl_seat *, uint32_t) {}
void WaylandProtocolClient::seatHandleName(void *data, struct wl_seat *, const char *name) {
    auto *self = static_cast<WaylandProtocolClient *>(data);
    if (!self->compositorName_.empty()) return;
    self->compositorName_ = name;
}

void WaylandProtocolClient::outputHandleGeometry(void *data, struct wl_output *,
                                                  int32_t x, int32_t y, int32_t, int32_t,
                                                  int32_t, const char *, const char *,
                                                  int32_t) {
    auto *out = static_cast<WaylandOutputInfo *>(data);
    out->x = x;
    out->y = y;
}

void WaylandProtocolClient::outputHandleMode(void *data, struct wl_output *,
                                              uint32_t, int32_t width, int32_t height,
                                              int32_t) {
    auto *out = static_cast<WaylandOutputInfo *>(data);
    out->width = width;
    out->height = height;
}

void WaylandProtocolClient::outputHandleScale(void *data, struct wl_output *, int32_t scale) {
    auto *out = static_cast<WaylandOutputInfo *>(data);
    out->scale = scale;
}

void WaylandProtocolClient::outputHandleDone(void *, struct wl_output *) {}

void WaylandProtocolClient::outputHandleName(void *data, struct wl_output *, const char *name) {
    auto *out = static_cast<WaylandOutputInfo *>(data);
    out->name = name ? name : "";
}

void WaylandProtocolClient::outputHandleDescription(void *, struct wl_output *, const char *) {}

} // namespace havel
