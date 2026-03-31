/*
 * UIBackend.hpp - Unified UI backend interface
 *
 * Abstract interface for UI operations that can be implemented
 * by Qt, GTK, ImGui, or other backends.
 */
#pragma once

#include "modules/ui/UIElement.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace havel::host {

/**
 * UIBackend - Abstract interface for UI implementations
 *
 * All UI operations go through this interface.
 * Implementations: QtBackend, GtkBackend, ImGuiBackend
 */
class UIBackend {
public:
    enum class Api {
        QT,
        GTK,
        IMGUI,
        AUTO
    };

    virtual ~UIBackend() = default;

    // Backend info
    virtual Api getApi() const = 0;
    virtual std::string getApiName() const = 0;
    virtual bool isAvailable() const = 0;

    // Initialization
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Element creation
    virtual std::shared_ptr<ui::UIElement> window(const std::string &title) = 0;
    virtual std::shared_ptr<ui::UIElement> panel(const std::string &side) = 0;
    virtual std::shared_ptr<ui::UIElement> modal(const std::string &title) = 0;

    // Display elements
    virtual std::shared_ptr<ui::UIElement> text(const std::string &content) = 0;
    virtual std::shared_ptr<ui::UIElement> label(const std::string &content) = 0;
    virtual std::shared_ptr<ui::UIElement> image(const std::string &path) = 0;
    virtual std::shared_ptr<ui::UIElement> icon(const std::string &name) = 0;
    virtual std::shared_ptr<ui::UIElement> divider() = 0;
    virtual std::shared_ptr<ui::UIElement> spacer(int size) = 0;
    virtual std::shared_ptr<ui::UIElement> progress(int value, int max) = 0;
    virtual std::shared_ptr<ui::UIElement> spinner() = 0;

    // Input elements
    virtual std::shared_ptr<ui::UIElement> btn(const std::string &label) = 0;
    virtual std::shared_ptr<ui::UIElement> input(const std::string &placeholder) = 0;
    virtual std::shared_ptr<ui::UIElement> textarea(const std::string &placeholder) = 0;
    virtual std::shared_ptr<ui::UIElement> checkbox(const std::string &label, bool checked) = 0;
    virtual std::shared_ptr<ui::UIElement> toggle(const std::string &label, bool value) = 0;
    virtual std::shared_ptr<ui::UIElement> slider(int min, int max, int value) = 0;
    virtual std::shared_ptr<ui::UIElement> dropdown(const std::vector<std::string> &options) = 0;

    // Layout containers
    virtual std::shared_ptr<ui::UIElement> row() = 0;
    virtual std::shared_ptr<ui::UIElement> col() = 0;
    virtual std::shared_ptr<ui::UIElement> grid(int cols) = 0;
    virtual std::shared_ptr<ui::UIElement> scroll() = 0;
    virtual std::shared_ptr<ui::UIElement> canvas(int width, int height) = 0;

    // Menu elements
    virtual std::shared_ptr<ui::UIElement> menu(const std::string &title) = 0;
    virtual std::shared_ptr<ui::UIElement> menuItem(const std::string &label, const std::string &shortcut) = 0;
    virtual std::shared_ptr<ui::UIElement> menuSeparator() = 0;

    // Realization
    virtual void realize(std::shared_ptr<ui::UIElement> element) = 0;

    // Show/hide
    virtual void show(std::shared_ptr<ui::UIElement> window) = 0;
    virtual void hide(std::shared_ptr<ui::UIElement> window) = 0;
    virtual void close(std::shared_ptr<ui::UIElement> window) = 0;

    // Dialogs
    virtual void alert(const std::string &message) = 0;
    virtual bool confirm(const std::string &message) = 0;
    virtual std::string filePicker(const std::string &title) = 0;
    virtual std::string dirPicker(const std::string &title) = 0;
    virtual void notify(const std::string &message, const std::string &type) = 0;

    // Event pumping
    virtual void pumpEvents(int timeoutMs) = 0;

    // Window state
    virtual bool hasActiveWindows() const = 0;
    virtual void onAllWindowsClosed(std::function<void()> callback) = 0;

    // Element value
    virtual std::string getValue(std::shared_ptr<ui::UIElement> element) = 0;
    virtual void setValue(std::shared_ptr<ui::UIElement> element, const std::string &value) = 0;

    // System Tray
    virtual void trayIcon(const std::string &iconPath, const std::string &tooltip) = 0;
    virtual void trayMenu(std::shared_ptr<ui::UIElement> menu) = 0;
    virtual void trayNotify(const std::string &title, const std::string &message, const std::string &iconType) = 0;
    virtual void trayShow() = 0;
    virtual void trayHide() = 0;
    virtual bool trayIsVisible() const = 0;

    // Styling
    virtual void applyStyle(std::shared_ptr<ui::UIElement> element, const std::string &key, const ui::PropValue &value) = 0;
};

} // namespace havel::host
