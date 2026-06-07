#pragma once

#include <wayland-client.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <cstdint>

struct zwlr_foreign_toplevel_handle_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1_listener;
struct zwlr_foreign_toplevel_manager_v1_listener;

namespace havel {

class WaylandProtocolClient;

struct ForeignToplevelWindow {
    uint32_t id = 0;
    std::string title;
    std::string appId;
    std::string output;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    bool active = false;
    bool maximized = false;
    bool minimized = false;
    bool fullscreen = false;
    bool floating = true;

    struct zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
};

class ForeignToplevel {
public:
    explicit ForeignToplevel(WaylandProtocolClient &client);
    ~ForeignToplevel();

    bool initialize();
    void cleanup();
    bool isAvailable() const { return available_; }

    std::vector<ForeignToplevelWindow> windows() const;
    ForeignToplevelWindow activeWindow() const;
    bool focusWindow(uint32_t id);
    bool closeWindow(uint32_t id);
    bool minimizeWindow(uint32_t id);
    bool maximizeWindow(uint32_t id);
    bool setFullscreen(uint32_t id, bool fullscreen);

    bool pollEvents(int timeoutMs = 100);

    using WindowCallback = std::function<void(const ForeignToplevelWindow &)>;
    void onWindowAdded(WindowCallback cb) { addCallback_ = std::move(cb); }
    void onWindowRemoved(WindowCallback cb) { removeCallback_ = std::move(cb); }
    void onWindowChanged(WindowCallback cb) { changeCallback_ = std::move(cb); }

    static void handleToplevel(void *data,
                                struct zwlr_foreign_toplevel_manager_v1 *manager,
                                struct zwlr_foreign_toplevel_handle_v1 *toplevel);
    static void handleTitle(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                             const char *title);
    static void handleAppId(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                             const char *appId);
    static void handleOutputEnter(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   struct wl_output *output);
    static void handleOutputLeave(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                                   struct wl_output *output);
    static void handleState(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                             struct wl_array *state);
    static void handleDone(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel);
    static void handleClosed(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel);
    static void handleParent(void *data, struct zwlr_foreign_toplevel_handle_v1 *toplevel,
                              struct zwlr_foreign_toplevel_handle_v1 *parent);

    static void managerFinished(void *data,
                                 struct zwlr_foreign_toplevel_manager_v1 *manager);

    static const struct zwlr_foreign_toplevel_handle_v1_listener toplevelListener_;
    static const struct zwlr_foreign_toplevel_manager_v1_listener managerListener_;

private:
    void removeHandle(struct zwlr_foreign_toplevel_handle_v1 *handle);

    WaylandProtocolClient &client_;
    struct zwlr_foreign_toplevel_manager_v1 *manager_ = nullptr;
    bool available_ = false;

    mutable std::mutex mutex_;
    std::vector<ForeignToplevelWindow> windows_;
    uint32_t nextId_ = 1;

    WindowCallback addCallback_;
    WindowCallback removeCallback_;
    WindowCallback changeCallback_;
};

} // namespace havel
