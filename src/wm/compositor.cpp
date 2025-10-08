#define WLR_USE_UNSTABLE
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <algorithm>
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
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
// Forward declarations
struct Server;
struct XdgSurface;
static void arrange_views(Server* server);
struct Server {
struct wl_display* display;
struct wlr_backend* backend;
struct wlr_renderer* renderer;
struct wlr_allocator* allocator;
struct wlr_scene* scene;
struct wlr_scene_output_layout* scene_layout;
struct wlr_xdg_shell* xdg_shell;
struct wlr_compositor* compositor;
struct wlr_output_layout* output_layout;
struct wlr_seat* seat;
struct wlr_cursor* cursor;
struct wlr_xcursor_manager* cursor_mgr;
std::vector<struct XdgSurface*> views;
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
struct wlr_scene_tree* scene_tree;
Server* server;
int x, y, width, height;
struct wl_listener map;
struct wl_listener unmap;
struct wl_listener destroy;
struct wl_listener request_move;
struct wl_listener request_resize;
};
static void focus_view(struct XdgSurface* view, struct wlr_surface* surface) {
if (view == nullptr) {
return;
}
struct Server* server = view->server;
struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(server->seat);
if (keyboard != nullptr) {
wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes,
keyboard->num_keycodes, &keyboard->modifiers);
}
}
static struct XdgSurface* desktop_view_at(Server* server, double lx, double ly,
struct wlr_surface** surface, double* sx, double* sy) {
struct wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) {
return nullptr;
}
struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
struct wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
if (!scene_surface) {
return nullptr;
}
*surface = scene_surface->surface;
struct wlr_scene_tree* tree = node->parent;
while (tree != nullptr && tree->node.data == nullptr) {
tree = tree->node.parent;
}
return (struct XdgSurface*)tree->node.data;
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
wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
event->delta_discrete, event->source, event->relative_direction);
}
static void server_cursor_frame(struct wl_listener* listener, void* data) {
struct Server* server = wl_container_of(listener, server, cursor_frame);
wlr_seat_pointer_notify_frame(server->seat);
}
static void keyboard_handle_modifiers(struct wl_listener* listener, void* data) {
struct Keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}
static bool handle_keybinding(Server* server, xkb_keysym_t sym) {
switch (sym) {
case XKB_KEY_Escape:
wl_display_terminate(server->display);
return true;
default:
return false;
}
}
static void keyboard_handle_key(struct wl_listener* listener, void* data) {
struct Keyboard* keyboard = wl_container_of(listener, keyboard, key);
struct Server* server = keyboard->server;
struct wlr_keyboard_key_event* event = static_cast<struct wlr_keyboard_key_event*>(data);
struct wlr_seat* seat = server->seat;
struct wlr_keyboard* kb = keyboard->keyboard;
xkb_keycode_t keycode = event->keycode + 8;
const xkb_keysym_t* syms;
int nsyms = xkb_state_key_get_syms(kb->xkb_state, keycode, &syms);
bool handled = false;
uint32_t modifiers = wlr_keyboard_get_modifiers(kb);
if ((modifiers & WLR_MODIFIER_LOGO) && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
for (int i = 0; i < nsyms; i++) {
handled = handle_keybinding(server, syms[i]);
}
}
if (!handled) {
wlr_seat_set_keyboard(seat, kb);
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
struct wlr_keyboard* kb = wlr_keyboard_from_input_device(device);
struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
wlr_keyboard_set_keymap(kb, keymap);
xkb_keymap_unref(keymap);
xkb_context_unref(context);
wlr_keyboard_set_repeat_info(kb, 25, 600);
auto* keyboard = new Keyboard();
keyboard->server = server;
keyboard->keyboard = kb;
keyboard->modifiers.notify = keyboard_handle_modifiers;
wl_signal_add(&kb->events.modifiers, &keyboard->modifiers);
keyboard->key.notify = keyboard_handle_key;
wl_signal_add(&kb->events.key, &keyboard->key);
keyboard->destroy.notify = keyboard_handle_destroy;
wl_signal_add(&device->events.destroy, &keyboard->destroy);
wlr_seat_set_keyboard(server->seat, kb);
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
// Check if we have keyboards by checking if any keyboards are set
if (wlr_seat_get_keyboard(server->seat) != nullptr) {
caps |= WL_SEAT_CAPABILITY_KEYBOARD;
}
wlr_seat_set_capabilities(server->seat, caps);
}
static void arrange_views(Server* server) {
if (server->views.empty()) {
return;
}
struct wlr_output* output = nullptr;
struct wlr_output_layout_output* l_output = nullptr;
wl_list_for_each(l_output, &server->output_layout->outputs, link) {
output = l_output->output;
break;
}
if (!output) {
return;
}
int usable_width = output->width;
int usable_height = output->height;
int num_views = server->views.size();
int view_width = usable_width / num_views;
int x = 0;
for (auto* view : server->views) {
view->x = x;
view->y = 0;
view->width = view_width;
view->height = usable_height;
wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->width, view->height);
wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
x += view_width;
}
}
static void xdg_surface_map(struct wl_listener* listener, void* data) {
struct XdgSurface* surface = wl_container_of(listener, surface, map);
wlr_scene_node_set_enabled(&surface->scene_tree->node, true);
focus_view(surface, surface->xdg_toplevel->base->surface);
}
static void xdg_surface_unmap(struct wl_listener* listener, void* data) {
struct XdgSurface* surface = wl_container_of(listener, surface, unmap);
wlr_scene_node_set_enabled(&surface->scene_tree->node, false);
}
static void xdg_surface_destroy(struct wl_listener* listener, void* data) {
struct XdgSurface* surface = wl_container_of(listener, surface, destroy);
auto& views = surface->server->views;
views.erase(std::remove(views.begin(), views.end(), surface), views.end());
wl_list_remove(&surface->map.link);
wl_list_remove(&surface->unmap.link);
wl_list_remove(&surface->destroy.link);
wl_list_remove(&surface->request_move.link);
wl_list_remove(&surface->request_resize.link);
delete surface;
arrange_views(surface->server);
}
static void xdg_toplevel_request_move(struct wl_listener* listener, void* data) {
// TODO: Implement move
}
static void xdg_toplevel_request_resize(struct wl_listener* listener, void* data) {
// TODO: Implement resize
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
surface->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
surface->scene_tree->node.data = surface;
xdg_surface->data = surface->scene_tree;
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
server->views.push_back(surface);
arrange_views(server);
}
static void output_frame(struct wl_listener* listener, void* data) {
struct Output* output = wl_container_of(listener, output, frame);
struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(
output->server->scene, output->output);
wlr_scene_output_commit(scene_output, nullptr);
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
wlr_output_set_mode(wlr_output, mode);
wlr_output_enable(wlr_output, true);
if (!wlr_output_commit(wlr_output)) {
return;
}
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
printf("Starting minimal i3-like compositor...\n");
auto server = std::make_unique<Server>();
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
server->scene = wlr_scene_create();
wlr_scene_attach_output_layout(server->scene, server->output_layout);
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
printf("Compositor running on %s\n", socket);
wl_display_run(server->display);
wl_display_destroy_clients(server->display);
wl_display_destroy(server->display);
return 0;
}