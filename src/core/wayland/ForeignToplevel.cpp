#include "ForeignToplevel.hpp"
#include "WaylandProtocolClient.hpp"
#include "utils/Logger.hpp"

#include <wayland-client.h>
#include <algorithm>
#include <cstring>

extern "C" {
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
}

namespace havel {

const struct zwlr_foreign_toplevel_handle_v1_listener ForeignToplevel::toplevelListener_ = {
    .title = ForeignToplevel::handleTitle,
    .app_id = ForeignToplevel::handleAppId,
    .output_enter = ForeignToplevel::handleOutputEnter,
    .output_leave = ForeignToplevel::handleOutputLeave,
    .state = ForeignToplevel::handleState,
    .done = ForeignToplevel::handleDone,
    .closed = ForeignToplevel::handleClosed,
    .parent = ForeignToplevel::handleParent,
};

const struct zwlr_foreign_toplevel_manager_v1_listener ForeignToplevel::managerListener_ = {
    .toplevel = ForeignToplevel::handleToplevel,
    .finished = ForeignToplevel::managerFinished,
};

ForeignToplevel::ForeignToplevel(WaylandProtocolClient &client)
    : client_(client) {}

ForeignToplevel::~ForeignToplevel() {
    cleanup();
}

bool ForeignToplevel::initialize() {
    if (available_) return true;

    if (!client_.hasForeignToplevel()) {
        debug("ForeignToplevel: compositor does not support foreign-toplevel protocol");
        return false;
    }

    manager_ = client_.foreignToplevelManager();
    if (!manager_) return false;

    zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &managerListener_, this);
    client_.roundtrip();

    available_ = true;
    info("ForeignToplevel: initialized, found {} windows", windows_.size());
    return true;
}

void ForeignToplevel::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : windows_) {
        if (w.handle) {
            zwlr_foreign_toplevel_handle_v1_destroy(w.handle);
            w.handle = nullptr;
        }
    }
    windows_.clear();
    if (manager_) {
        zwlr_foreign_toplevel_manager_v1_stop(manager_);
        manager_ = nullptr;
    }
    available_ = false;
}

std::vector<ForeignToplevelWindow> ForeignToplevel::windows() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return windows_;
}

ForeignToplevelWindow ForeignToplevel::activeWindow() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &w : windows_) {
        if (w.active) return w;
    }
    return {};
}

bool ForeignToplevel::focusWindow(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : windows_) {
        if (w.id == id && w.handle) {
            zwlr_foreign_toplevel_handle_v1_activate(w.handle, client_.seat());
            client_.roundtrip();
            return true;
        }
    }
    return false;
}

bool ForeignToplevel::closeWindow(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : windows_) {
        if (w.id == id && w.handle) {
            zwlr_foreign_toplevel_handle_v1_close(w.handle);
            client_.roundtrip();
            return true;
        }
    }
    return false;
}

bool ForeignToplevel::minimizeWindow(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : windows_) {
        if (w.id == id && w.handle) {
            zwlr_foreign_toplevel_handle_v1_set_minimized(w.handle);
            client_.roundtrip();
            return true;
        }
    }
    return false;
}

bool ForeignToplevel::maximizeWindow(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : windows_) {
        if (w.id == id && w.handle) {
            zwlr_foreign_toplevel_handle_v1_set_maximized(w.handle);
            client_.roundtrip();
            return true;
        }
    }
    return false;
}

bool ForeignToplevel::setFullscreen(uint32_t id, bool fullscreen) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &w : windows_) {
        if (w.id == id && w.handle) {
            if (fullscreen) {
                zwlr_foreign_toplevel_handle_v1_set_fullscreen(w.handle, nullptr);
            } else {
                zwlr_foreign_toplevel_handle_v1_unset_fullscreen(w.handle);
            }
            client_.roundtrip();
            return true;
        }
    }
    return false;
}

bool ForeignToplevel::pollEvents(int timeoutMs) {
    client_.dispatch(timeoutMs);
    return client_.isConnected();
}

void ForeignToplevel::removeHandle(struct zwlr_foreign_toplevel_handle_v1 *handle) {
    auto it = std::find_if(windows_.begin(), windows_.end(),
                            [handle](const ForeignToplevelWindow &w) { return w.handle == handle; });
    if (it != windows_.end()) {
        if (removeCallback_) removeCallback_(*it);
        if (it->handle) zwlr_foreign_toplevel_handle_v1_destroy(it->handle);
        windows_.erase(it);
    }
}

void ForeignToplevel::handleToplevel(void *data,
                                      struct zwlr_foreign_toplevel_manager_v1 *,
                                      struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    ForeignToplevelWindow w;
    w.id = self->nextId_++;
    w.handle = toplevel;
    self->windows_.push_back(w);

    zwlr_foreign_toplevel_handle_v1_add_listener(toplevel, &toplevelListener_, data);
}

void ForeignToplevel::handleTitle(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   const char *title) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    for (auto &w : self->windows_) {
        if (w.handle == toplevel) {
            w.title = title ? title : "";
            break;
        }
    }
}

void ForeignToplevel::handleAppId(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   const char *appId) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    for (auto &w : self->windows_) {
        if (w.handle == toplevel) {
            w.appId = appId ? appId : "";
            break;
        }
    }
}

void ForeignToplevel::handleOutputEnter(void *data,
                                         struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                         struct wl_output *output) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    for (auto &w : self->windows_) {
        if (w.handle == toplevel) {
            for (const auto &o : self->client_.outputs()) {
                if (o.output == output) {
                    w.output = o.name;
                    break;
                }
            }
            break;
        }
    }
}

void ForeignToplevel::handleOutputLeave(void *data,
                                         struct zwlr_foreign_toplevel_handle_v1 *,
                                         struct wl_output *) {
    (void)data;
}

void ForeignToplevel::handleState(void *data,
                                   struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   struct wl_array *state) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    for (auto &w : self->windows_) {
        if (w.handle == toplevel) {
            w.maximized = false;
            w.fullscreen = false;
            w.active = false;
            w.minimized = false;

            auto *states = static_cast<uint32_t *>(state->data);
            size_t count = state->size / sizeof(uint32_t);
            for (size_t i = 0; i < count; ++i) {
                switch (states[i]) {
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                        w.maximized = true; break;
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                        w.fullscreen = true; break;
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                        w.active = true; break;
                    case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                        w.minimized = true; break;
                }
            }
            break;
        }
    }
}

void ForeignToplevel::handleDone(void *data,
                                  struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    for (const auto &w : self->windows_) {
        if (w.handle == toplevel) {
            if (self->changeCallback_) self->changeCallback_(w);
            break;
        }
    }
}

void ForeignToplevel::handleClosed(void *data,
                                    struct zwlr_foreign_toplevel_handle_v1 *toplevel) {
    auto *self = static_cast<ForeignToplevel *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->removeHandle(toplevel);
}

void ForeignToplevel::handleParent(void *data,
                                    struct zwlr_foreign_toplevel_handle_v1 *,
                                    struct zwlr_foreign_toplevel_handle_v1 *) {
    (void)data;
}

void ForeignToplevel::managerFinished(void *data,
                                       struct zwlr_foreign_toplevel_manager_v1 *) {
    auto *self = static_cast<ForeignToplevel *>(data);
    self->available_ = false;
}

} // namespace havel
