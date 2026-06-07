#pragma once

#include "host/ui/UIBackend.hpp"
#include "modules/ui/UIElement.hpp"

#ifdef __cplusplus
extern "C" {
#endif

int  havel_ui_shim_init(void* backend);
void havel_ui_shim_shutdown(void* backend);

int64_t havel_ui_shim_window(void* backend, const char* title);
int64_t havel_ui_shim_panel(void* backend, const char* side);
int64_t havel_ui_shim_modal(void* backend, const char* title);
int64_t havel_ui_shim_text(void* backend, const char* content);
int64_t havel_ui_shim_label(void* backend, const char* content);
int64_t havel_ui_shim_image(void* backend, const char* path);
int64_t havel_ui_shim_icon(void* backend, const char* name);
int64_t havel_ui_shim_divider(void* backend);
int64_t havel_ui_shim_spacer(void* backend, int size);
int64_t havel_ui_shim_progress(void* backend, int value, int max);
int64_t havel_ui_shim_spinner(void* backend);
int64_t havel_ui_shim_btn(void* backend, const char* label);
int64_t havel_ui_shim_input(void* backend, const char* placeholder);
int64_t havel_ui_shim_textarea(void* backend, const char* placeholder);
int64_t havel_ui_shim_checkbox(void* backend, const char* label, int checked);
int64_t havel_ui_shim_toggle(void* backend, const char* label, int value);
int64_t havel_ui_shim_slider(void* backend, int min, int max, int value);
int64_t havel_ui_shim_dropdown(void* backend, const char* options_csv);
int64_t havel_ui_shim_row(void* backend);
int64_t havel_ui_shim_col(void* backend);
int64_t havel_ui_shim_grid(void* backend, int cols);
int64_t havel_ui_shim_table(void* backend, int rows, int cols);
int64_t havel_ui_shim_flex(void* backend, const char* direction);
int64_t havel_ui_shim_scroll(void* backend);
int64_t havel_ui_shim_canvas(void* backend, int width, int height);
int64_t havel_ui_shim_menu(void* backend, const char* title);
int64_t havel_ui_shim_menu_item(void* backend, const char* label, const char* shortcut);
int64_t havel_ui_shim_menu_separator(void* backend);

void havel_ui_shim_show(void* backend, int64_t id);
void havel_ui_shim_hide(void* backend, int64_t id);
void havel_ui_shim_close(void* backend, int64_t id);
void havel_ui_shim_realize(void* backend, int64_t id);
void havel_ui_shim_add_child(void* backend, int64_t parentId, int64_t childId);
void havel_ui_shim_set_title(void* backend, int64_t id, const char* title);
void havel_ui_shim_set_size(void* backend, int64_t id, int w, int h);
void havel_ui_shim_set_pos(void* backend, int64_t id, int x, int y);
void havel_ui_shim_set_style(void* backend, int64_t id, const char* key, const char* value);
const char* havel_ui_shim_get_value(void* backend, int64_t id);
void havel_ui_shim_set_value(void* backend, int64_t id, const char* value);

void havel_ui_shim_canvas_flush(void* backend, int64_t id);
void havel_ui_shim_canvas_clear(void* backend, int64_t id);
void havel_ui_shim_canvas_draw_line(void* backend, int64_t id, int x1, int y1, int x2, int y2);
void havel_ui_shim_canvas_draw_rect(void* backend, int64_t id, int x, int y, int w, int h);
void havel_ui_shim_canvas_draw_circle(void* backend, int64_t id, int cx, int cy, int r);
void havel_ui_shim_canvas_set_pen(void* backend, int64_t id, int r, int g, int b, int width);
void havel_ui_shim_canvas_fill(void* backend, int64_t id, int x, int y);

int  havel_ui_shim_run_loop(void* backend);
void havel_ui_shim_quit_loop(void* backend, int code);
void havel_ui_shim_pump_events(void* backend, int timeoutMs);

void havel_ui_shim_alert(void* backend, const char* msg);
int  havel_ui_shim_confirm(void* backend, const char* msg);
const char* havel_ui_shim_file_picker(void* backend, const char* title);
const char* havel_ui_shim_dir_picker(void* backend, const char* title);
void havel_ui_shim_notify(void* backend, const char* msg, const char* type);

void havel_ui_shim_tray_icon(void* backend, const char* iconPath, const char* tooltip);
void havel_ui_shim_tray_menu(void* backend, int64_t menuId);
void havel_ui_shim_tray_notify(void* backend, const char* title, const char* msg, const char* iconType);
void havel_ui_shim_tray_show(void* backend);
void havel_ui_shim_tray_hide(void* backend);
int  havel_ui_shim_tray_is_visible(void* backend);

int64_t havel_ui_shim_timer_create(void* backend, int intervalMs, int singleShot);
void havel_ui_shim_timer_start(void* backend, int64_t timerId);
void havel_ui_shim_timer_stop(void* backend, int64_t timerId);
void havel_ui_shim_timer_destroy(void* backend, int64_t timerId);

void* havel_ui_shim_settings_create(void* backend, const char* org, const char* app);
void havel_ui_shim_settings_destroy(void* backend, void* settings);
void havel_ui_shim_settings_set_value(void* backend, void* settings, const char* key, const char* value);
const char* havel_ui_shim_settings_value(void* backend, void* settings, const char* key, const char* defaultValue);
int  havel_ui_shim_settings_contains(void* backend, void* settings, const char* key);
void havel_ui_shim_settings_remove(void* backend, void* settings, const char* key);
void havel_ui_shim_settings_sync(void* backend, void* settings);

const char* havel_ui_shim_color_picker(void* backend, const char* initial);
const char* havel_ui_shim_font_picker(void* backend, const char* initial);
const char* havel_ui_shim_input_text(void* backend, const char* title, const char* label, const char* defaultValue);
int64_t havel_ui_shim_input_int(void* backend, const char* title, const char* label, int defaultValue, int min, int max, int step);

int  havel_ui_shim_has_active_windows(void* backend);
int  havel_ui_shim_is_available(void* backend);
const char* havel_ui_shim_get_api_name(void* backend);
void havel_ui_shim_on_all_windows_closed(void* backend, void (*callback)(void*), void* userData);
void havel_ui_shim_set_app_metadata(void* backend, const char* name, const char* version, const char* org, int quitOnLast);

const char* havel_ui_shim_last_error(void);

#ifdef __cplusplus
}
#endif
