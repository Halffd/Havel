#include "ui_shim.hpp"
#include <cstring>
#include <string>
#include <unordered_map>

using namespace havel::host;
using namespace havel::ui;

static thread_local std::string g_lastError;
static thread_local std::string g_strResult;

static inline UIBackend* be(void* p) {
    return static_cast<UIBackend*>(p);
}

static inline std::shared_ptr<UIElement> findElem(UIBackend*, int64_t id) {
    auto e = std::make_shared<UIElement>();
    e->id = static_cast<ElementId>(id);
    return e;
}

static int64_t elemToId(const std::shared_ptr<UIElement>& elem) {
    return elem ? static_cast<int64_t>(elem->id) : 0;
}

extern "C" {

int havel_ui_shim_init(void* backend) {
    try { return be(backend)->initialize() ? 1 : 0; }
    catch (...) { g_lastError = "init failed"; return 0; }
}

void havel_ui_shim_shutdown(void* backend) {
    try { be(backend)->shutdown(); }
    catch (...) { g_lastError = "shutdown failed"; }
}

int64_t havel_ui_shim_window(void* backend, const char* title) {
    try { return elemToId(be(backend)->window(title ? title : "")); }
    catch (...) { g_lastError = "window failed"; return 0; }
}

int64_t havel_ui_shim_panel(void* backend, const char* side) {
    try { return elemToId(be(backend)->panel(side ? side : "")); }
    catch (...) { g_lastError = "panel failed"; return 0; }
}

int64_t havel_ui_shim_modal(void* backend, const char* title) {
    try { return elemToId(be(backend)->modal(title ? title : "")); }
    catch (...) { g_lastError = "modal failed"; return 0; }
}

int64_t havel_ui_shim_text(void* backend, const char* content) {
    try { return elemToId(be(backend)->text(content ? content : "")); }
    catch (...) { g_lastError = "text failed"; return 0; }
}

int64_t havel_ui_shim_label(void* backend, const char* content) {
    try { return elemToId(be(backend)->label(content ? content : "")); }
    catch (...) { g_lastError = "label failed"; return 0; }
}

int64_t havel_ui_shim_image(void* backend, const char* path) {
    try { return elemToId(be(backend)->image(path ? path : "")); }
    catch (...) { g_lastError = "image failed"; return 0; }
}

int64_t havel_ui_shim_icon(void* backend, const char* name) {
    try { return elemToId(be(backend)->icon(name ? name : "")); }
    catch (...) { g_lastError = "icon failed"; return 0; }
}

int64_t havel_ui_shim_divider(void* backend) {
    try { return elemToId(be(backend)->divider()); }
    catch (...) { g_lastError = "divider failed"; return 0; }
}

int64_t havel_ui_shim_spacer(void* backend, int size) {
    try { return elemToId(be(backend)->spacer(size)); }
    catch (...) { g_lastError = "spacer failed"; return 0; }
}

int64_t havel_ui_shim_progress(void* backend, int value, int max) {
    try { return elemToId(be(backend)->progress(value, max)); }
    catch (...) { g_lastError = "progress failed"; return 0; }
}

int64_t havel_ui_shim_spinner(void* backend) {
    try { return elemToId(be(backend)->spinner()); }
    catch (...) { g_lastError = "spinner failed"; return 0; }
}

int64_t havel_ui_shim_btn(void* backend, const char* label) {
    try { return elemToId(be(backend)->btn(label ? label : "")); }
    catch (...) { g_lastError = "btn failed"; return 0; }
}

int64_t havel_ui_shim_input(void* backend, const char* placeholder) {
    try { return elemToId(be(backend)->input(placeholder ? placeholder : "")); }
    catch (...) { g_lastError = "input failed"; return 0; }
}

int64_t havel_ui_shim_textarea(void* backend, const char* placeholder) {
    try { return elemToId(be(backend)->textarea(placeholder ? placeholder : "")); }
    catch (...) { g_lastError = "textarea failed"; return 0; }
}

int64_t havel_ui_shim_checkbox(void* backend, const char* label, int checked) {
    try { return elemToId(be(backend)->checkbox(label ? label : "", checked != 0)); }
    catch (...) { g_lastError = "checkbox failed"; return 0; }
}

int64_t havel_ui_shim_toggle(void* backend, const char* label, int value) {
    try { return elemToId(be(backend)->toggle(label ? label : "", value != 0)); }
    catch (...) { g_lastError = "toggle failed"; return 0; }
}

int64_t havel_ui_shim_slider(void* backend, int min, int max, int value) {
    try { return elemToId(be(backend)->slider(min, max, value)); }
    catch (...) { g_lastError = "slider failed"; return 0; }
}

int64_t havel_ui_shim_dropdown(void* backend, const char* options_csv) {
    try {
        std::vector<std::string> opts;
        if (options_csv && options_csv[0]) {
            std::string csv(options_csv);
            size_t start = 0, end;
            while ((end = csv.find(',', start)) != std::string::npos) {
                opts.push_back(csv.substr(start, end - start));
                start = end + 1;
            }
            opts.push_back(csv.substr(start));
        }
        return elemToId(be(backend)->dropdown(opts));
    } catch (...) { g_lastError = "dropdown failed"; return 0; }
}

int64_t havel_ui_shim_row(void* backend) {
    try { return elemToId(be(backend)->row()); }
    catch (...) { g_lastError = "row failed"; return 0; }
}

int64_t havel_ui_shim_col(void* backend) {
    try { return elemToId(be(backend)->col()); }
    catch (...) { g_lastError = "col failed"; return 0; }
}

int64_t havel_ui_shim_grid(void* backend, int cols) {
    try { return elemToId(be(backend)->grid(cols)); }
    catch (...) { g_lastError = "grid failed"; return 0; }
}

int64_t havel_ui_shim_table(void* backend, int rows, int cols) {
    try { return elemToId(be(backend)->table(rows, cols)); }
    catch (...) { g_lastError = "table failed"; return 0; }
}

int64_t havel_ui_shim_flex(void* backend, const char* direction) {
    try { return elemToId(be(backend)->flex(direction ? direction : "row")); }
    catch (...) { g_lastError = "flex failed"; return 0; }
}

int64_t havel_ui_shim_scroll(void* backend) {
    try { return elemToId(be(backend)->scroll()); }
    catch (...) { g_lastError = "scroll failed"; return 0; }
}

int64_t havel_ui_shim_canvas(void* backend, int width, int height) {
    try { return elemToId(be(backend)->canvas(width, height)); }
    catch (...) { g_lastError = "canvas failed"; return 0; }
}

int64_t havel_ui_shim_menu(void* backend, const char* title) {
    try { return elemToId(be(backend)->menu(title ? title : "")); }
    catch (...) { g_lastError = "menu failed"; return 0; }
}

int64_t havel_ui_shim_menu_item(void* backend, const char* label, const char* shortcut) {
    try { return elemToId(be(backend)->menuItem(label ? label : "", shortcut ? shortcut : "")); }
    catch (...) { g_lastError = "menu_item failed"; return 0; }
}

int64_t havel_ui_shim_menu_separator(void* backend) {
    try { return elemToId(be(backend)->menuSeparator()); }
    catch (...) { g_lastError = "menu_separator failed"; return 0; }
}

void havel_ui_shim_show(void* backend, int64_t id) {
    try { be(backend)->show(findElem(be(backend), id)); }
    catch (...) {}
}

void havel_ui_shim_hide(void* backend, int64_t id) {
    try { be(backend)->hide(findElem(be(backend), id)); }
    catch (...) {}
}

void havel_ui_shim_close(void* backend, int64_t id) {
    try { be(backend)->close(findElem(be(backend), id)); }
    catch (...) {}
}

void havel_ui_shim_realize(void* backend, int64_t id) {
    try { be(backend)->realize(findElem(be(backend), id)); }
    catch (...) {}
}

void havel_ui_shim_add_child(void* backend, int64_t parentId, int64_t childId) {
    try {
        auto parent = findElem(be(backend), parentId);
        auto child = findElem(be(backend), childId);
        if (parent && child) parent->add(child);
    } catch (...) {}
}

void havel_ui_shim_set_title(void* backend, int64_t id, const char* title) {
    try {
        auto elem = findElem(be(backend), id);
        if (elem) elem->set("title", std::string(title ? title : ""));
    } catch (...) {}
}

void havel_ui_shim_set_size(void* backend, int64_t id, int w, int h) {
    try {
        auto elem = findElem(be(backend), id);
        if (elem) {
            elem->set("width", static_cast<int64_t>(w));
            elem->set("height", static_cast<int64_t>(h));
        }
    } catch (...) {}
}

void havel_ui_shim_set_pos(void* backend, int64_t id, int x, int y) {
    try {
        auto elem = findElem(be(backend), id);
        if (elem) {
            elem->set("x", static_cast<int64_t>(x));
            elem->set("y", static_cast<int64_t>(y));
        }
    } catch (...) {}
}

void havel_ui_shim_set_style(void* backend, int64_t id, const char* key, const char* value) {
    try {
        auto elem = findElem(be(backend), id);
        if (elem && key) {
            PropValue pv = value ? PropValue(std::string(value)) : PropValue(nullptr);
            be(backend)->applyStyle(elem, key, pv);
        }
    } catch (...) {}
}

const char* havel_ui_shim_get_value(void* backend, int64_t id) {
    try {
        g_strResult = be(backend)->getValue(findElem(be(backend), id));
        return g_strResult.c_str();
    } catch (...) { return ""; }
}

void havel_ui_shim_set_value(void* backend, int64_t id, const char* value) {
    try { be(backend)->setValue(findElem(be(backend), id), value ? value : ""); }
    catch (...) {}
}

void havel_ui_shim_canvas_flush(void* backend, int64_t id) {
    try { be(backend)->canvasFlush(findElem(be(backend), id)); }
    catch (...) {}
}

void havel_ui_shim_canvas_clear(void* backend, int64_t id) {
    try { be(backend)->canvasClear(findElem(be(backend), id)); }
    catch (...) {}
}

void havel_ui_shim_canvas_draw_line(void* backend, int64_t id, int x1, int y1, int x2, int y2) {
    try { be(backend)->canvasDrawLine(findElem(be(backend), id), x1, y1, x2, y2); }
    catch (...) {}
}

void havel_ui_shim_canvas_draw_rect(void* backend, int64_t id, int x, int y, int w, int h) {
    try { be(backend)->canvasDrawRect(findElem(be(backend), id), x, y, w, h); }
    catch (...) {}
}

void havel_ui_shim_canvas_draw_circle(void* backend, int64_t id, int cx, int cy, int r) {
    try { be(backend)->canvasDrawCircle(findElem(be(backend), id), cx, cy, r); }
    catch (...) {}
}

void havel_ui_shim_canvas_set_pen(void* backend, int64_t id, int r, int g, int b, int width) {
    try { be(backend)->canvasSetPen(findElem(be(backend), id), r, g, b, width); }
    catch (...) {}
}

void havel_ui_shim_canvas_fill(void* backend, int64_t id, int x, int y) {
    try { be(backend)->canvasFill(findElem(be(backend), id), x, y); }
    catch (...) {}
}

int havel_ui_shim_run_loop(void* backend) {
    try { return be(backend)->runEventLoop(); }
    catch (...) { g_lastError = "run_loop failed"; return -1; }
}

void havel_ui_shim_quit_loop(void* backend, int code) {
    try { be(backend)->quitEventLoop(code); }
    catch (...) {}
}

void havel_ui_shim_pump_events(void* backend, int timeoutMs) {
    try { be(backend)->pumpEvents(timeoutMs); }
    catch (...) {}
}

void havel_ui_shim_alert(void* backend, const char* msg) {
    try { be(backend)->alert(msg ? msg : ""); }
    catch (...) {}
}

int havel_ui_shim_confirm(void* backend, const char* msg) {
    try { return be(backend)->confirm(msg ? msg : "") ? 1 : 0; }
    catch (...) { return 0; }
}

const char* havel_ui_shim_file_picker(void* backend, const char* title) {
    try {
        g_strResult = be(backend)->filePicker(title ? title : "");
        return g_strResult.c_str();
    } catch (...) { return ""; }
}

const char* havel_ui_shim_dir_picker(void* backend, const char* title) {
    try {
        g_strResult = be(backend)->dirPicker(title ? title : "");
        return g_strResult.c_str();
    } catch (...) { return ""; }
}

void havel_ui_shim_notify(void* backend, const char* msg, const char* type) {
    try { be(backend)->notify(msg ? msg : "", type ? type : "info"); }
    catch (...) {}
}

void havel_ui_shim_tray_icon(void* backend, const char* iconPath, const char* tooltip) {
    try { be(backend)->trayIcon(iconPath ? iconPath : "", tooltip ? tooltip : ""); }
    catch (...) {}
}

void havel_ui_shim_tray_menu(void* backend, int64_t menuId) {
    try { be(backend)->trayMenu(findElem(be(backend), menuId)); }
    catch (...) {}
}

void havel_ui_shim_tray_notify(void* backend, const char* title, const char* msg, const char* iconType) {
    try { be(backend)->trayNotify(title ? title : "", msg ? msg : "", iconType ? iconType : "info"); }
    catch (...) {}
}

void havel_ui_shim_tray_show(void* backend) {
    try { be(backend)->trayShow(); }
    catch (...) {}
}

void havel_ui_shim_tray_hide(void* backend) {
    try { be(backend)->trayHide(); }
    catch (...) {}
}

int havel_ui_shim_tray_is_visible(void* backend) {
    try { return be(backend)->trayIsVisible() ? 1 : 0; }
    catch (...) { return 0; }
}

int64_t havel_ui_shim_timer_create(void* backend, int intervalMs, int singleShot) {
    try {
        return be(backend)->timerCreate(intervalMs, singleShot != 0, [](){});
    } catch (...) { return 0; }
}

void havel_ui_shim_timer_start(void* backend, int64_t timerId) {
    try { be(backend)->timerStart(timerId); }
    catch (...) {}
}

void havel_ui_shim_timer_stop(void* backend, int64_t timerId) {
    try { be(backend)->timerStop(timerId); }
    catch (...) {}
}

void havel_ui_shim_timer_destroy(void* backend, int64_t timerId) {
    try { be(backend)->timerDestroy(timerId); }
    catch (...) {}
}

void* havel_ui_shim_settings_create(void* backend, const char* org, const char* app) {
    try { return be(backend)->settingsCreate(org ? org : "", app ? app : ""); }
    catch (...) { return nullptr; }
}

void havel_ui_shim_settings_destroy(void* backend, void* settings) {
    try { be(backend)->settingsDestroy(settings); }
    catch (...) {}
}

void havel_ui_shim_settings_set_value(void* backend, void* settings, const char* key, const char* value) {
    try { be(backend)->settingsSetValue(settings, key ? key : "", value ? value : ""); }
    catch (...) {}
}

const char* havel_ui_shim_settings_value(void* backend, void* settings, const char* key, const char* defaultValue) {
    try {
        g_strResult = be(backend)->settingsValue(settings, key ? key : "", defaultValue ? defaultValue : "");
        return g_strResult.c_str();
    } catch (...) { return defaultValue ? defaultValue : ""; }
}

int havel_ui_shim_settings_contains(void* backend, void* settings, const char* key) {
    try { return be(backend)->settingsContains(settings, key ? key : "") ? 1 : 0; }
    catch (...) { return 0; }
}

void havel_ui_shim_settings_remove(void* backend, void* settings, const char* key) {
    try { be(backend)->settingsRemove(settings, key ? key : ""); }
    catch (...) {}
}

void havel_ui_shim_settings_sync(void* backend, void* settings) {
    try { be(backend)->settingsSync(settings); }
    catch (...) {}
}

const char* havel_ui_shim_color_picker(void* backend, const char* initial) {
    try {
        g_strResult = be(backend)->colorPicker(initial ? initial : "");
        return g_strResult.c_str();
    } catch (...) { return ""; }
}

const char* havel_ui_shim_font_picker(void* backend, const char* initial) {
    try {
        g_strResult = be(backend)->fontPicker(initial ? initial : "");
        return g_strResult.c_str();
    } catch (...) { return ""; }
}

const char* havel_ui_shim_input_text(void* backend, const char* title, const char* label, const char* defaultValue) {
    try {
        g_strResult = be(backend)->inputText(title ? title : "", label ? label : "", defaultValue ? defaultValue : "");
        return g_strResult.c_str();
    } catch (...) { return ""; }
}

int64_t havel_ui_shim_input_int(void* backend, const char* title, const char* label, int defaultValue, int min, int max, int step) {
    try { return be(backend)->inputInt(title ? title : "", label ? label : "", defaultValue, min, max, step); }
    catch (...) { return defaultValue; }
}

int havel_ui_shim_has_active_windows(void* backend) {
    try { return be(backend)->hasActiveWindows() ? 1 : 0; }
    catch (...) { return 0; }
}

int havel_ui_shim_is_available(void* backend) {
    try { return be(backend)->isAvailable() ? 1 : 0; }
    catch (...) { return 0; }
}

const char* havel_ui_shim_get_api_name(void* backend) {
    try {
        g_strResult = be(backend)->getApiName();
        return g_strResult.c_str();
    } catch (...) { return "unknown"; }
}

static void (*g_allWindowsClosedCb)(void*) = nullptr;
static void* g_allWindowsClosedData = nullptr;

void havel_ui_shim_on_all_windows_closed(void* backend, void (*callback)(void*), void* userData) {
    g_allWindowsClosedCb = callback;
    g_allWindowsClosedData = userData;
    try {
        be(backend)->onAllWindowsClosed([callback, userData]() {
            if (callback) callback(userData);
        });
    } catch (...) {}
}

void havel_ui_shim_set_app_metadata(void* backend, const char* name, const char* version, const char* org, int quitOnLast) {
    try {
        UIBackend::ApplicationMetadata meta;
        meta.applicationName = name ? name : "";
        meta.applicationVersion = version ? version : "";
        meta.organizationName = org ? org : "";
        meta.quitOnLastWindowClosed = quitOnLast != 0;
        be(backend)->setApplicationMetadata(meta);
    } catch (...) {}
}

const char* havel_ui_shim_last_error(void) {
    return g_lastError.c_str();
}

}
