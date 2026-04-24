/*
 * GtkBackend.cpp - GTK4 implementation of UIBackend
 */
#include "GtkBackend.hpp"

#ifdef HAVE_GTK_BACKEND

#include "modules/ui/UIElement.hpp"
#include "utils/Logger.hpp"
#include <iostream>

// GTK headers - using extern "C" for C headers
extern "C" {
#include <gtk/gtk.h>
}

namespace havel::host {

GtkBackend::GtkBackend() = default;

GtkBackend::~GtkBackend() {
    if (initialized_) {
        shutdown();
    }
}

bool GtkBackend::isAvailable() const {
    // Check if GTK4 is available
    int argc = 0;
    char **argv = nullptr;
    
    if (!gtk_init_check()) {
        return false;
    }
    
    return gtk_get_major_version() >= 4;
}

bool GtkBackend::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Initialize GTK
    if (!gtk_init_check()) {
        havel::error("Failed to initialize GTK");
        return false;
    }
    
    // Create application
    app_ = gtk_application_new(appId_.c_str(), G_APPLICATION_DEFAULT_FLAGS);
    if (!app_) {
        havel::error("Failed to create GTK application");
        return false;
    }
    
    setupSignalHandlers();
    initialized_ = true;
    
    havel::info("GTK backend initialized (GTK {}.{}.{})",
                 gtk_get_major_version(),
                 gtk_get_minor_version(),
                 gtk_get_micro_version());
    
    return true;
}

void GtkBackend::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Clean up all widgets
    for (auto &pair : widgets_) {
        if (pair.second && GTK_IS_WIDGET(pair.second)) {
            gtk_widget_unparent(pair.second);
        }
    }
    widgets_.clear();
    
    // Release application
    if (app_) {
        g_object_unref(app_);
        app_ = nullptr;
    }
    
    initialized_ = false;
    havel::info("GTK backend shutdown complete");
}

// Element creation implementations
std::shared_ptr<ui::UIElement> GtkBackend::window(const std::string &title) {
    auto window = createWindowInternal(title, false);
    if (!window) {
        return nullptr;
    }
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "window_" + std::to_string(reinterpret_cast<uintptr_t>(window));
    element->type = ui::UIElement::Type::Window;
    element->props["title"] = title;
    
    registerWidget(element->id, window);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::panel(const std::string &side) {
    auto box = createBoxInternal(side == "left" || side == "right");
    if (!box) {
        return nullptr;
    }
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "panel_" + std::to_string(reinterpret_cast<uintptr_t>(box));
    element->type = ui::UIElement::Type::Panel;
    element->props["side"] = side;
    
    registerWidget(element->id, box);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::modal(const std::string &title) {
    auto window = createWindowInternal(title, true);
    if (!window) {
        return nullptr;
    }
    
    gtk_window_set_modal(GTK_WINDOW(window), true);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "modal_" + std::to_string(reinterpret_cast<uintptr_t>(window));
    element->type = ui::UIElement::Type::Modal;
    element->props["title"] = title;
    
    registerWidget(element->id, window);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::text(const std::string &content) {
    auto label = gtk_label_new(content.c_str());
    gtk_label_set_wrap(GTK_LABEL(label), true);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "text_" + std::to_string(reinterpret_cast<uintptr_t>(label));
    element->type = ui::UIElement::Type::Text;
    element->props["content"] = content;
    
    registerWidget(element->id, label);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::label(const std::string &content) {
    auto label = gtk_label_new(content.c_str());
    gtk_widget_add_css_class(label, "label");
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "label_" + std::to_string(reinterpret_cast<uintptr_t>(label));
    element->type = ui::UIElement::Type::Label;
    element->props["content"] = content;
    
    registerWidget(element->id, label);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::image(const std::string &path) {
    auto image = gtk_image_new_from_file(path.c_str());
    if (!image) {
        // Fallback to empty image
        image = gtk_image_new();
    }
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "image_" + std::to_string(reinterpret_cast<uintptr_t>(image));
    element->type = ui::UIElement::Type::Image;
    element->props["path"] = path;
    
    registerWidget(element->id, image);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::icon(const std::string &name) {
    // GTK uses icon names from icon theme
    auto image = gtk_image_new_from_icon_name(name.c_str());
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "icon_" + std::to_string(reinterpret_cast<uintptr_t>(image));
    element->type = ui::UIElement::Type::Icon;
    element->props["name"] = name;
    
    registerWidget(element->id, image);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::divider() {
    auto separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "divider_" + std::to_string(reinterpret_cast<uintptr_t>(separator));
    element->type = ui::UIElement::Type::Divider;
    
    registerWidget(element->id, separator);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::spacer(int size) {
    auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(box, size, size);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "spacer_" + std::to_string(reinterpret_cast<uintptr_t>(box));
    element->type = ui::UIElement::Type::Spacer;
    element->props["size"] = size;
    
    registerWidget(element->id, box);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::progress(int value, int max) {
    auto progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 
                                   static_cast<double>(value) / max);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "progress_" + std::to_string(reinterpret_cast<uintptr_t>(progress));
    element->type = ui::UIElement::Type::Progress;
    element->props["value"] = value;
    element->props["max"] = max;
    
    registerWidget(element->id, progress);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::spinner() {
    auto spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "spinner_" + std::to_string(reinterpret_cast<uintptr_t>(spinner));
    element->type = ui::UIElement::Type::Spinner;
    
    registerWidget(element->id, spinner);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::btn(const std::string &label) {
    auto button = gtk_button_new_with_label(label.c_str());
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "button_" + std::to_string(reinterpret_cast<uintptr_t>(button));
    element->type = ui::UIElement::Type::Button;
    element->props["label"] = label;
    
    registerWidget(element->id, button);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::input(const std::string &placeholder) {
    auto entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder.c_str());
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "input_" + std::to_string(reinterpret_cast<uintptr_t>(entry));
    element->type = ui::UIElement::Type::Input;
    element->props["placeholder"] = placeholder;
    
    registerWidget(element->id, entry);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::textarea(const std::string &placeholder) {
    auto textview = gtk_text_view_new();
    auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(buffer, placeholder.c_str(), -1);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "textarea_" + std::to_string(reinterpret_cast<uintptr_t>(textview));
    element->type = ui::UIElement::Type::TextArea;
    element->props["placeholder"] = placeholder;
    
    registerWidget(element->id, textview);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::checkbox(const std::string &label, bool checked) {
    auto check = gtk_check_button_new_with_label(label.c_str());
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), checked);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "checkbox_" + std::to_string(reinterpret_cast<uintptr_t>(check));
    element->type = ui::UIElement::Type::Checkbox;
    element->props["label"] = label;
    element->props["checked"] = checked;
    
    registerWidget(element->id, check);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::toggle(const std::string &label, bool value) {
    // GTK uses switches for toggles
    auto toggle = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(toggle), value);
    
    auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    auto label_widget = gtk_label_new(label.c_str());
    gtk_box_append(GTK_BOX(box), label_widget);
    gtk_box_append(GTK_BOX(box), toggle);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "toggle_" + std::to_string(reinterpret_cast<uintptr_t>(box));
    element->type = ui::UIElement::Type::Toggle;
    element->props["label"] = label;
    element->props["value"] = value;
    
    registerWidget(element->id, box);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::slider(int min, int max, int value) {
    auto adjustment = gtk_adjustment_new(value, min, max, 1, 10, 0);
    auto scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "slider_" + std::to_string(reinterpret_cast<uintptr_t>(scale));
    element->type = ui::UIElement::Type::Slider;
    element->props["min"] = min;
    element->props["max"] = max;
    element->props["value"] = value;
    
    registerWidget(element->id, scale);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::dropdown(const std::vector<std::string> &options) {
    auto dropdown = gtk_drop_down_new(nullptr, nullptr);
    
    // Create string list for options
    const char **strings = new const char*[options.size() + 1];
    for (size_t i = 0; i < options.size(); i++) {
        strings[i] = options[i].c_str();
    }
    strings[options.size()] = nullptr;
    
    auto list = gtk_string_list_new(strings);
    gtk_drop_down_set_model(GTK_DROP_DOWN(dropdown), G_LIST_MODEL(list));
    
    delete[] strings;
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "dropdown_" + std::to_string(reinterpret_cast<uintptr_t>(dropdown));
    element->type = ui::UIElement::Type::Dropdown;
    element->props["options"] = options;
    
    registerWidget(element->id, dropdown);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::row() {
    auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(box, "row");
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "row_" + std::to_string(reinterpret_cast<uintptr_t>(box));
    element->type = ui::UIElement::Type::Row;
    
    registerWidget(element->id, box);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::col() {
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(box, "col");
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "col_" + std::to_string(reinterpret_cast<uintptr_t>(box));
    element->type = ui::UIElement::Type::Column;
    
    registerWidget(element->id, box);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::grid(int cols) {
    auto grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "grid_" + std::to_string(reinterpret_cast<uintptr_t>(grid));
    element->type = ui::UIElement::Type::Grid;
    element->props["columns"] = cols;
    
    registerWidget(element->id, grid);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::table(int rows, int cols) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = "table_" + std::to_string(reinterpret_cast<uintptr_t>(element.get()));
    element->type = ui::UIElement::Type::Table;
    element->props["rows"] = rows;
    element->props["cols"] = cols;
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::flex(const std::string &direction) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = "flex_" + std::to_string(reinterpret_cast<uintptr_t>(element.get()));
    element->type = ui::UIElement::Type::Flex;
    element->props["direction"] = direction;
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::scroll() {
    auto scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                     GTK_POLICY_AUTOMATIC,
                                     GTK_POLICY_AUTOMATIC);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "scroll_" + std::to_string(reinterpret_cast<uintptr_t>(scrolled));
    element->type = ui::UIElement::Type::Scroll;
    
    registerWidget(element->id, scrolled);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::canvas(int width, int height) {
    auto drawingArea = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(drawingArea), width);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(drawingArea), height);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "canvas_" + std::to_string(reinterpret_cast<uintptr_t>(drawingArea));
    element->type = ui::UIElement::Type::Canvas;
    element->props["width"] = width;
    element->props["height"] = height;
    
    registerWidget(element->id, drawingArea);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::menu(const std::string &title) {
    auto menu = gtk_menu_new();
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "menu_" + std::to_string(reinterpret_cast<uintptr_t>(menu));
    element->type = ui::UIElement::Type::Menu;
    element->props["title"] = title;
    
    registerWidget(element->id, menu);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::menuItem(const std::string &label, const std::string &shortcut) {
    auto item = gtk_menu_item_new_with_label(label.c_str());
    if (!shortcut.empty()) {
        // GTK 4 uses accelerators differently
        // Store shortcut in user data or tooltip
        gtk_widget_set_tooltip_text(item, shortcut.c_str());
    }
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "menuitem_" + std::to_string(reinterpret_cast<uintptr_t>(item));
    element->type = ui::UIElement::Type::MenuItem;
    element->props["label"] = label;
    element->props["shortcut"] = shortcut;
    
    registerWidget(element->id, item);
    return element;
}

std::shared_ptr<ui::UIElement> GtkBackend::menuSeparator() {
    auto separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    
    auto element = std::make_shared<ui::UIElement>();
    element->id = "menuseparator_" + std::to_string(reinterpret_cast<uintptr_t>(separator));
    element->type = ui::UIElement::Type::MenuSeparator;
    
    registerWidget(element->id, separator);
    return element;
}

void GtkBackend::realize(std::shared_ptr<ui::UIElement> element) {
    // In GTK, realization happens when the widget is added to a window
    // This is typically handled automatically
    (void)element;
}

void GtkBackend::show(std::shared_ptr<ui::UIElement> window) {
    if (!window) return;
    
    auto widget = getWidget(window->id);
    if (widget && GTK_IS_WINDOW(widget)) {
        gtk_widget_show(widget);
        gtk_window_present(GTK_WINDOW(widget));
    }
}

void GtkBackend::hide(std::shared_ptr<ui::UIElement> window) {
    if (!window) return;
    
    auto widget = getWidget(window->id);
    if (widget) {
        gtk_widget_hide(widget);
    }
}

void GtkBackend::close(std::shared_ptr<ui::UIElement> window) {
    if (!window) return;
    
    auto widget = getWidget(window->id);
    if (widget && GTK_IS_WINDOW(widget)) {
        gtk_window_close(GTK_WINDOW(widget));
    }
    destroyWidget(window->id);
}

void GtkBackend::alert(const std::string &message) {
    // Create a simple message dialog
    auto dialog = gtk_message_dialog_new(nullptr,
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_INFO,
                                        GTK_BUTTONS_OK,
                                        "%s", message.c_str());
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_window_destroy(GTK_WINDOW(dialog));
}

bool GtkBackend::confirm(const std::string &message) {
    auto dialog = gtk_message_dialog_new(nullptr,
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_YES_NO,
                                        "%s", message.c_str());
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_window_destroy(GTK_WINDOW(dialog));
    
    return response == GTK_RESPONSE_YES;
}

std::string GtkBackend::filePicker(const std::string &title) {
    auto dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title.c_str());
    
    // GTK 4 uses async file dialogs
    // For simplicity, we'll use a synchronous approach
    // In production, this should be async with a callback
    
    // Placeholder implementation
    (void)dialog;
    return "";
}

std::string GtkBackend::dirPicker(const std::string &title) {
    auto dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, title.c_str());
    
    // Placeholder implementation
    (void)dialog;
    return "";
}

void GtkBackend::notify(const std::string &message, const std::string &type) {
    // Use GNotification for desktop notifications
    auto notification = g_notification_new("Havel");
    g_notification_set_body(notification, message.c_str());
    
    if (type == "error") {
        g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_URGENT);
    } else if (type == "warning") {
        g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_HIGH);
    }
    
    if (app_) {
        g_application_send_notification(G_APPLICATION(app_), nullptr, notification);
    }
    
    g_object_unref(notification);
}

void GtkBackend::pumpEvents(int timeoutMs) {
    // Process GTK events
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    
    if (timeoutMs > 0) {
        // Use g_timeout_add for delayed processing
        // In a real implementation, this would integrate with the main loop
        (void)timeoutMs;
    }
}

bool GtkBackend::hasActiveWindows() const {
    if (!app_) return false;
    
    auto windows = gtk_application_get_windows(app_);
    return windows != nullptr;
}

void GtkBackend::onAllWindowsClosed(std::function<void()> callback) {
    onAllWindowsClosedCallback_ = callback;
}

std::string GtkBackend::getValue(std::shared_ptr<ui::UIElement> element) {
    if (!element) return "";
    
    auto widget = getWidget(element->id);
    if (!widget) return "";
    
    if (GTK_IS_ENTRY(widget)) {
        return gtk_editable_get_text(GTK_EDITABLE(widget));
    } else if (GTK_IS_TEXT_VIEW(widget)) {
        auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        return gtk_text_buffer_get_text(buffer, &start, &end, false);
    } else if (GTK_IS_CHECK_BUTTON(widget)) {
        return gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)) ? "true" : "false";
    } else if (GTK_IS_SWITCH(widget)) {
        return gtk_switch_get_active(GTK_SWITCH(widget)) ? "true" : "false";
    }
    
    return "";
}

void GtkBackend::setValue(std::shared_ptr<ui::UIElement> element, const std::string &value) {
    if (!element) return;
    
    auto widget = getWidget(element->id);
    if (!widget) return;
    
    if (GTK_IS_ENTRY(widget)) {
        gtk_editable_set_text(GTK_EDITABLE(widget), value.c_str());
    } else if (GTK_IS_TEXT_VIEW(widget)) {
        auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        gtk_text_buffer_set_text(buffer, value.c_str(), -1);
    } else if (GTK_IS_LABEL(widget)) {
        gtk_label_set_text(GTK_LABEL(widget), value.c_str());
    } else if (GTK_IS_PROGRESS_BAR(widget)) {
        double fraction = std::stod(value);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(widget), fraction);
    }
}

void GtkBackend::trayIcon(const std::string &iconPath, const std::string &tooltip) {
    // GTK uses libayatana-appindicator or libappindicator for system tray
    // This is a placeholder implementation
    (void)iconPath;
    (void)tooltip;
    trayVisible_ = true;
}

void GtkBackend::trayMenu(std::shared_ptr<ui::UIElement> menu) {
    // Connect menu to tray icon
    (void)menu;
}

void GtkBackend::trayNotify(const std::string &title, const std::string &message, const std::string &iconType) {
    notify(message, iconType);
}

void GtkBackend::trayShow() {
    trayVisible_ = true;
}

void GtkBackend::trayHide() {
    trayVisible_ = false;
}

bool GtkBackend::trayIsVisible() const {
    return trayVisible_;
}

void GtkBackend::applyStyle(std::shared_ptr<ui::UIElement> element, const std::string &key, const ui::PropValue &value) {
    if (!element) return;
    
    auto widget = getWidget(element->id);
    if (!widget) return;
    
    // Convert key-value to CSS
    std::visit([&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            // Apply string styles
            if (key == "css-class") {
                gtk_widget_add_css_class(widget, arg.c_str());
            }
        } else if constexpr (std::is_same_v<T, int>) {
            // Apply numeric styles
            if (key == "width") {
                gtk_widget_set_size_request(widget, arg, -1);
            } else if (key == "height") {
                gtk_widget_set_size_request(widget, -1, arg);
            }
        }
    }, value);
}

// Private helper methods
GtkWidget* GtkBackend::createWindowInternal(const std::string &title, bool modal) {
    auto window = gtk_application_window_new(app_);
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    
    if (modal) {
        gtk_window_set_modal(GTK_WINDOW(window), true);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(window), true);
    }
    
    // Connect close signal
    g_signal_connect(window, "destroy", G_CALLBACK(onWindowClosed), this);
    
    return window;
}

GtkWidget* GtkBackend::createBoxInternal(bool horizontal) {
    auto orientation = horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    return gtk_box_new(orientation, 6);
}

void GtkBackend::setupSignalHandlers() {
    // Setup application-level signals
    g_signal_connect(app_, "activate", G_CALLBACK(+[](GtkApplication *app, gpointer userData) {
        (void)app;
        (void)userData;
        // Activation handler
    }), this);
}

void GtkBackend::onWindowClosed(GtkWindow *window, gpointer userData) {
    (void)window;
    auto *backend = static_cast<GtkBackend*>(userData);
    
    if (!backend->hasActiveWindows() && backend->onAllWindowsClosedCallback_) {
        backend->onAllWindowsClosedCallback_();
    }
}

void GtkBackend::registerWidget(const std::string &id, GtkWidget *widget) {
    widgets_[id] = widget;
}

GtkWidget* GtkBackend::getWidget(const std::string &id) const {
    auto it = widgets_.find(id);
    if (it != widgets_.end()) {
        return it->second;
    }
    return nullptr;
}

void GtkBackend::destroyWidget(const std::string &id) {
    auto it = widgets_.find(id);
    if (it != widgets_.end()) {
        if (it->second && GTK_IS_WIDGET(it->second)) {
            gtk_widget_unparent(it->second);
        }
        widgets_.erase(it);
    }
}

} // namespace havel::host

#endif // HAVE_GTK_BACKEND
