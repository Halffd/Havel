// Unified Havel Wayland Compositor
// Combines all features from both compositor implementations

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>
#include <string>
#include <unistd.h>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
}

// Forward declarations
struct Server;
struct XdgSurface;
static void arrange_views(Server* server);

enum class TilingMode {
    HORIZONTAL, // Side by side
    VERTICAL,   // Stacked
    GRID        // Grid layout
};

struct Workspace {
    int id;
    std::string name;
    std::vector<struct XdgSurface*> views;
    TilingMode tiling_mode = TilingMode::HORIZONTAL;
    struct XdgSurface* focused_view = nullptr;
};

struct WindowSwitcher {
    bool active = false;
    std::vector<struct XdgSurface*> window_list;
    size_t current_index = 0;
    uint32_t start_time = 0;
};

struct Server {
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    
    struct wlr_xdg_shell* xdg_shell;
    struct wlr_compositor* compositor;
    struct wlr_output_layout* output_layout;
    struct wlr_seat* seat;
    struct wlr_cursor* cursor;
    struct wlr_xcursor_manager* cursor_mgr;
    
    // Workspace management
    std::vector<Workspace> workspaces;
    int current_workspace = 0;
    std::vector<struct XdgSurface*> all_views; // All views across workspaces
    WindowSwitcher window_switcher;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_surface;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
};

struct Output {
    struct wlr_output* output;
    Server* server;
    struct wl_listener frame;
    struct wl_listener destroy;
};

struct Keyboard {
    struct wlr_keyboard* keyboard;
    Server* server;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct XdgSurface {
    struct wlr_xdg_toplevel* xdg_toplevel;
    Server* server;
    int x = 100, y = 100, width = 800, height = 600;
    bool mapped = false;
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
};

// Focus management
static void focus_view(struct XdgSurface* view, struct wlr_surface* surface) {
    if (view == nullptr || view->server == nullptr) {
        return;
    }
    
    struct Server* server = view->server;
    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard != nullptr) {
        wlr_seat_keyboard_notify_enter(server->seat, surface, 
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

// Simple view finding without scene API
static struct XdgSurface* desktop_view_at(Server* server, double lx, double ly,
                                          struct wlr_surface** surface, double* sx, double* sy) {
    // Simple implementation: iterate through views and check bounds
    for (auto* view : server->all_views) {
        if (!view->mapped) continue;
        
        if (lx >= view->x && lx < view->x + view->width &&
            ly >= view->y && ly < view->y + view->height) {
            if (surface) *surface = view->xdg_toplevel->base->surface;
            if (sx) *sx = lx - view->x;
            if (sy) *sy = ly - view->y;
            return view;
        }
    }
    
    return nullptr;
}

static void process_cursor_motion(Server* server, uint32_t time) {
    double sx, sy;
    struct wlr_seat* seat = server->seat;
    struct wlr_surface* surface = nullptr;
    struct XdgSurface* view = desktop_view_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        
    if (!view) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
    }
    
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void server_cursor_motion(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event* event = static_cast<struct wlr_pointer_motion_event*>(data);
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event* event = static_cast<struct wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event* event = static_cast<struct wlr_pointer_button_event*>(data);
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
    
    double sx, sy;
    struct wlr_surface* surface;
    struct XdgSurface* view = desktop_view_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && view) {
        focus_view(view, surface);
    }
}

static void server_cursor_axis(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event* event = static_cast<struct wlr_pointer_axis_event*>(data);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, 
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

// Initialize workspaces
static void init_workspaces(Server* server) {
    server->workspaces.clear();
    for (int i = 0; i < 10; i++) {
        Workspace ws;
        ws.id = i;
        ws.name = std::to_string(i + 1);
        server->workspaces.push_back(ws);
    }
}

// Focus next view in current workspace
static void focus_next_view(Server* server) {
    if (server->current_workspace >= static_cast<int>(server->workspaces.size())) return;
    
    auto& current_ws = server->workspaces[server->current_workspace];
    if (current_ws.views.empty()) return;
    
    // Find currently focused view
    struct wlr_surface* focused = server->seat->keyboard_state.focused_surface;
    auto current_it = current_ws.views.begin();
    
    for (auto it = current_ws.views.begin(); it != current_ws.views.end(); ++it) {
        if ((*it)->xdg_toplevel->base->surface == focused) {
            current_it = it;
            break;
        }
    }
    
    // Move to next view
    ++current_it;
    if (current_it == current_ws.views.end()) {
        current_it = current_ws.views.begin();
    }
    
    if (current_it != current_ws.views.end()) {
        focus_view(*current_it, (*current_it)->xdg_toplevel->base->surface);
        current_ws.focused_view = *current_it;
    }
}

// Focus previous view in current workspace
static void focus_previous_view(Server* server) {
    if (server->current_workspace >= static_cast<int>(server->workspaces.size())) return;
    
    auto& current_ws = server->workspaces[server->current_workspace];
    if (current_ws.views.empty()) return;
    
    // Find currently focused view
    struct wlr_surface* focused = server->seat->keyboard_state.focused_surface;
    auto current_it = current_ws.views.begin();
    
    for (auto it = current_ws.views.begin(); it != current_ws.views.end(); ++it) {
        if ((*it)->xdg_toplevel->base->surface == focused) {
            current_it = it;
            break;
        }
    }
    
    // Move to previous view
    if (current_it == current_ws.views.begin()) {
        current_it = current_ws.views.end();
    }
    --current_it;
    
    if (current_it != current_ws.views.end()) {
        focus_view(*current_it, (*current_it)->xdg_toplevel->base->surface);
        current_ws.focused_view = *current_it;
    }
}

// Switch to workspace
static void switch_to_workspace(Server* server, int workspace_id) {
    if (workspace_id < 0 || workspace_id >= static_cast<int>(server->workspaces.size())) return;
    
    server->current_workspace = workspace_id;
    arrange_views(server);
    
    // Focus first view in new workspace
    auto& ws = server->workspaces[workspace_id];
    if (!ws.views.empty()) {
        focus_view(ws.views[0], ws.views[0]->xdg_toplevel->base->surface);
        ws.focused_view = ws.views[0];
    }
}

// Move focused view to workspace
static void move_view_to_workspace(Server* server, int workspace_id) {
    if (workspace_id < 0 || workspace_id >= static_cast<int>(server->workspaces.size())) return;
    if (server->current_workspace >= static_cast<int>(server->workspaces.size())) return;
    
    auto& current_ws = server->workspaces[server->current_workspace];
    auto& target_ws = server->workspaces[workspace_id];
    
    if (current_ws.focused_view) {
        struct XdgSurface* view = current_ws.focused_view;
        
        // Remove from current workspace
        current_ws.views.erase(std::remove(current_ws.views.begin(), current_ws.views.end(), view), current_ws.views.end());
        current_ws.focused_view = current_ws.views.empty() ? nullptr : current_ws.views[0];
        
        // Add to target workspace
        target_ws.views.push_back(view);
        if (!target_ws.focused_view) {
            target_ws.focused_view = view;
        }
        
        arrange_views(server);
    }
}

// Set tiling mode for current workspace
static void set_tiling_mode(Server* server, TilingMode mode) {
    if (server->current_workspace >= static_cast<int>(server->workspaces.size())) return;
    
    server->workspaces[server->current_workspace].tiling_mode = mode;
    arrange_views(server);
}

// Window switcher functionality
static void start_window_switcher(Server* server, bool forward) {
    server->window_switcher.active = true;
    server->window_switcher.window_list = server->all_views;
    server->window_switcher.current_index = 0;
    
    if (!server->window_switcher.window_list.empty()) {
        auto* view = server->window_switcher.window_list[0];
        focus_view(view, view->xdg_toplevel->base->surface);
    }
}

static void cycle_window_switcher(Server* server, bool forward) {
    if (!server->window_switcher.active || server->window_switcher.window_list.empty()) {
        return;
    }
    
    if (forward) {
        server->window_switcher.current_index = 
            (server->window_switcher.current_index + 1) % server->window_switcher.window_list.size();
    } else {
        if (server->window_switcher.current_index == 0) {
            server->window_switcher.current_index = server->window_switcher.window_list.size() - 1;
        } else {
            server->window_switcher.current_index--;
        }
    }
    
    auto* view = server->window_switcher.window_list[server->window_switcher.current_index];
    focus_view(view, view->xdg_toplevel->base->surface);
}

static void end_window_switcher(Server* server) {
    server->window_switcher.active = false;
    server->window_switcher.window_list.clear();
}

// Arrange views in current workspace
static void arrange_views(Server* server) {
    if (server->current_workspace >= static_cast<int>(server->workspaces.size())) return;
    
    auto& current_ws = server->workspaces[server->current_workspace];
    if (current_ws.views.empty()) return;
    
    // Get output dimensions
    struct wlr_output* output = nullptr;
    if (!wl_list_empty(&server->output_layout->outputs)) {
        struct wlr_output_layout_output* layout_output = wl_container_of(
            server->output_layout->outputs.next, layout_output, link);
        output = layout_output->output;
    }
    
    if (!output) return;
    
    int screen_width = output->width;
    int screen_height = output->height;
    size_t num_views = current_ws.views.size();
    
    switch (current_ws.tiling_mode) {
        case TilingMode::HORIZONTAL:
            for (size_t i = 0; i < num_views; ++i) {
                auto* view = current_ws.views[i];
                view->x = static_cast<int>(i * screen_width / num_views);
                view->y = 0;
                view->width = screen_width / static_cast<int>(num_views);
                view->height = screen_height;
                
                wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->width, view->height);
            }
            break;
            
        case TilingMode::VERTICAL:
            for (size_t i = 0; i < num_views; ++i) {
                auto* view = current_ws.views[i];
                view->x = 0;
                view->y = static_cast<int>(i * screen_height / num_views);
                view->width = screen_width;
                view->height = screen_height / static_cast<int>(num_views);
                
                wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->width, view->height);
            }
            break;
            
        case TilingMode::GRID:
            int cols = static_cast<int>(std::ceil(std::sqrt(num_views)));
            int rows = static_cast<int>(std::ceil(static_cast<double>(num_views) / cols));
            
            for (size_t i = 0; i < num_views; ++i) {
                auto* view = current_ws.views[i];
                int col = static_cast<int>(i % cols);
                int row = static_cast<int>(i / cols);
                
                view->x = col * screen_width / cols;
                view->y = row * screen_height / rows;
                view->width = screen_width / cols;
                view->height = screen_height / rows;
                
                wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->width, view->height);
            }
            break;
    }
}

// Keyboard handling
static void keyboard_handle_modifiers(struct wl_listener* listener, void* data) {
    struct Keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

static bool handle_keybinding(Server* server, xkb_keysym_t sym, uint32_t modifiers) {
    bool is_super = (modifiers & WLR_MODIFIER_LOGO);
    bool is_alt = (modifiers & WLR_MODIFIER_ALT);
    bool is_shift = (modifiers & WLR_MODIFIER_SHIFT);
    
    if (is_super) {
        switch (sym) {
            case XKB_KEY_Escape:
                wl_display_terminate(server->display);
                return true;
                
            case XKB_KEY_Return: // Super + Return: launch terminal
                if (fork() == 0) {
                    execl("/usr/bin/weston-terminal", "weston-terminal", (char*)NULL);
                    _exit(1);
                }
                return true;
                
            case XKB_KEY_d: // Super + d: launch application launcher
                if (fork() == 0) {
                    execl("/usr/bin/wofi", "wofi", "--show", "drun", (char*)NULL);
                    _exit(1);
                }
                return true;
                
            case XKB_KEY_q: // Super + q: close window
                if (!server->all_views.empty()) {
                    auto* view = server->all_views.back();
                    wlr_xdg_toplevel_send_close(view->xdg_toplevel);
                }
                return true;
                
            case XKB_KEY_j: // Super + j: focus next window
                focus_next_view(server);
                return true;
                
            case XKB_KEY_k: // Super + k: focus previous window
                focus_previous_view(server);
                return true;
                
            case XKB_KEY_h: // Super + h: horizontal tiling
                set_tiling_mode(server, TilingMode::HORIZONTAL);
                return true;
                
            case XKB_KEY_v: // Super + v: vertical tiling
                set_tiling_mode(server, TilingMode::VERTICAL);
                return true;
                
            case XKB_KEY_g: // Super + g: grid tiling
                set_tiling_mode(server, TilingMode::GRID);
                return true;
                
            // Workspace switching (Super + 1-0)
            case XKB_KEY_1: case XKB_KEY_2: case XKB_KEY_3: case XKB_KEY_4: case XKB_KEY_5:
            case XKB_KEY_6: case XKB_KEY_7: case XKB_KEY_8: case XKB_KEY_9: case XKB_KEY_0: {
                int workspace_id = (sym == XKB_KEY_0) ? 9 : (sym - XKB_KEY_1);
                if (is_shift) {
                    // Super + Shift + Number: move window to workspace
                    move_view_to_workspace(server, workspace_id);
                } else {
                    // Super + Number: switch to workspace
                    switch_to_workspace(server, workspace_id);
                }
                return true;
            }
        }
    } else if (is_alt) {
        switch (sym) {
            case XKB_KEY_Tab: // Alt + Tab: window switcher
                if (is_shift) {
                    if (server->window_switcher.active) {
                        cycle_window_switcher(server, false);
                    } else {
                        start_window_switcher(server, false);
                    }
                } else {
                    if (server->window_switcher.active) {
                        cycle_window_switcher(server, true);
                    } else {
                        start_window_switcher(server, true);
                    }
                }
                return true;
        }
    }
    
    return false;
}

static void keyboard_handle_key(struct wl_listener* listener, void* data) {
    struct Keyboard* keyboard = wl_container_of(listener, keyboard, key);
    struct Server* server = keyboard->server;
    struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);
    struct wlr_seat* seat = server->seat;
    
    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->keyboard->xkb_state, keycode, &syms);
    
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);
    
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(server, syms[i], modifiers) || handled;
        }
    }
    
    // Handle Alt release for window switcher
    if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        bool alt_released = false;
        for (int i = 0; i < nsyms; i++) {
            if (syms[i] == XKB_KEY_Alt_L || syms[i] == XKB_KEY_Alt_R) {
                alt_released = true;
                break;
            }
        }
        if (alt_released && server->window_switcher.active) {
            end_window_switcher(server);
            handled = true;
        }
    }
    
    if (!handled) {
        wlr_seat_set_keyboard(seat, keyboard->keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener* listener, void* data) {
    struct Keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    delete keyboard;
}

static void server_new_keyboard(Server* server, struct wlr_input_device* device) {
    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);
    
    struct Keyboard* keyboard = new Keyboard();
    keyboard->server = server;
    keyboard->keyboard = wlr_keyboard;
    
    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, 
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
    
    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);
    
    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
}

static void server_new_pointer(Server* server, struct wlr_input_device* device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, new_input);
    struct wlr_input_device* device = static_cast<struct wlr_input_device*>(data);
    
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        default:
            break;
    }
    
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    // Check if we have at least one keyboard
    if (server->seat->keyboard_state.keyboard) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

// XDG surface handling
static void xdg_surface_map(struct wl_listener* listener, void* data) {
    struct XdgSurface* surface = wl_container_of(listener, surface, map);
    surface->mapped = true;
    focus_view(surface, surface->xdg_toplevel->base->surface);
    arrange_views(surface->server);
}

static void xdg_surface_unmap(struct wl_listener* listener, void* data) {
    struct XdgSurface* surface = wl_container_of(listener, surface, unmap);
    surface->mapped = false;
}

static void xdg_surface_destroy(struct wl_listener* listener, void* data) {
    struct XdgSurface* surface = wl_container_of(listener, surface, destroy);
    struct Server* server = surface->server;
    
    // Remove from workspace views
    for (auto& ws : server->workspaces) {
        ws.views.erase(std::remove(ws.views.begin(), ws.views.end(), surface), ws.views.end());
        if (ws.focused_view == surface) {
            ws.focused_view = ws.views.empty() ? nullptr : ws.views[0];
        }
    }
    
    // Remove from all_views
    auto& all_views = server->all_views;
    all_views.erase(std::remove(all_views.begin(), all_views.end(), surface), all_views.end());
    
    wl_list_remove(&surface->map.link);
    wl_list_remove(&surface->unmap.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->request_move.link);
    wl_list_remove(&surface->request_resize.link);
    delete surface;
    arrange_views(server);
}

static void xdg_toplevel_request_move(struct wl_listener* listener, void* data) {
    struct XdgSurface* surface = wl_container_of(listener, surface, request_move);
    printf("Move requested for surface\n");
    // Interactive move implementation would go here
}

static void xdg_toplevel_request_resize(struct wl_listener* listener, void* data) {
    struct XdgSurface* surface = wl_container_of(listener, surface, request_resize);
    struct wlr_xdg_toplevel_resize_event* event = static_cast<struct wlr_xdg_toplevel_resize_event*>(data);
    printf("Resize requested for surface with edges: %u\n", event->edges);
    // Interactive resize implementation would go here
}

static void server_new_xdg_surface(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface* xdg_surface = static_cast<struct wlr_xdg_surface*>(data);
    
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }
    
    printf("New XDG surface: %s\n",
        xdg_surface->toplevel->title ? xdg_surface->toplevel->title : "unnamed");
        
    struct XdgSurface* surface = new XdgSurface();
    surface->xdg_toplevel = xdg_surface->toplevel;
    surface->server = server;
    xdg_surface->data = surface;
    
    surface->map.notify = xdg_surface_map;
    wl_signal_add(&xdg_surface->surface->events.map, &surface->map);
    surface->unmap.notify = xdg_surface_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &surface->unmap);
    surface->destroy.notify = xdg_surface_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &surface->destroy);
    surface->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_surface->toplevel->events.request_move, &surface->request_move);
    surface->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_surface->toplevel->events.request_resize, &surface->request_resize);
    
    // Add to current workspace and all_views
    server->all_views.push_back(surface);
    if (server->current_workspace < static_cast<int>(server->workspaces.size())) {
        auto& current_ws = server->workspaces[server->current_workspace];
        current_ws.views.push_back(surface);
        if (!current_ws.focused_view) {
            current_ws.focused_view = surface;
        }
    }
    
    arrange_views(server);
}

// Output handling - simplified version
static void output_frame(struct wl_listener* listener, void* data) {
    struct Output* output = wl_container_of(listener, output, frame);
    
    // Simple commit - in a real compositor, you would render surfaces here
    // For now, just commit to show that the compositor is working
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    
    // Just commit the output without rendering
    // This provides a basic working compositor that can manage windows
    // without complex rendering logic
    wlr_output_commit_state(output->output, &state);
    wlr_output_state_finish(&state);
}

static void output_destroy(struct wl_listener* listener, void* data) {
    struct Output* output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    delete output;
}

static void server_new_output(struct wl_listener* listener, void* data) {
    struct Server* server = wl_container_of(listener, server, new_output);
    struct wlr_output* wlr_output = static_cast<struct wlr_output*>(data);
    
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_mode(&state, mode);
        wlr_output_state_set_enabled(&state, true);
        if (!wlr_output_commit_state(wlr_output, &state)) {
            wlr_output_state_finish(&state);
            return;
        }
        wlr_output_state_finish(&state);
    }
    
    printf("New output: %s\n", wlr_output->name);
    
    struct Output* output = new Output();
    output->output = wlr_output;
    output->server = server;
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
}

int main() {
    wlr_log_init(WLR_DEBUG, nullptr);
    printf("Starting Havel Wayland Compositor...\n");
    
    auto server = std::make_unique<Server>();
    init_workspaces(server.get());
    
    server->display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->display), nullptr);
    if (!server->backend) {
        printf("Failed to create backend\n");
        return 1;
    }
    
    server->renderer = wlr_renderer_autocreate(server->backend);
    wlr_renderer_init_wl_display(server->renderer, server->display);
    
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    server->compositor = wlr_compositor_create(server->display, 5, server->renderer);
    wlr_data_device_manager_create(server->display);
    
    server->output_layout = wlr_output_layout_create(server->display);
    
    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    server->new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);
    
    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
    
    server->seat = wlr_seat_create(server->display, "seat0");
    
    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
    
    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
    
    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    
    const char* socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        printf("Failed to create socket\n");
        return 1;
    }
    
    if (!wlr_backend_start(server->backend)) {
        printf("Failed to start backend\n");
        return 1;
    }
    
    setenv("WAYLAND_DISPLAY", socket, true);
    printf("Havel Compositor running on %s\n", socket);
    printf("Keybindings:\n");
    printf("  Super + Escape     - Exit compositor\n");
    printf("  Super + Return     - Launch terminal\n");
    printf("  Super + d          - Launch app launcher\n");
    printf("  Super + q          - Close window\n");
    printf("  Super + j/k        - Focus next/previous window\n");
    printf("  Super + h/v/g      - Set horizontal/vertical/grid tiling\n");
    printf("  Super + 1-0        - Switch to workspace 1-10\n");
    printf("  Super + Shift + 1-0 - Move window to workspace 1-10\n");
    printf("  Alt + Tab          - Window switcher\n");
    
    wl_display_run(server->display);
    
    wl_display_destroy_clients(server->display);
    wl_display_destroy(server->display);
    return 0;
}