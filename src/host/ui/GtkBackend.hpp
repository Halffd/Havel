/*
 * GtkBackend.hpp - GTK4 implementation of UIBackend
 *
 * Native GTK4 backend for the Havel UI system.
 */
#pragma once

#ifdef HAVE_GTK_BACKEND

#include "UIBackend.hpp"
#include <memory>
#include <unordered_map>
#include <functional>

// Forward declarations for GTK
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkBox GtkBox;
typedef struct _GtkButton GtkButton;
typedef struct _GtkEntry GtkEntry;
typedef struct _GtkLabel GtkLabel;
typedef struct _GtkCheckButton GtkCheckButton;
typedef struct _GtkScrolledWindow GtkScrolledWindow;
typedef struct _GtkMenu GtkMenu;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _GtkStatusIcon GtkStatusIcon;

namespace havel::host {

/**
 * GtkBackend - Native GTK4 implementation of UI backend
 *
 * Provides full GTK4 support with modern widgets and features.
 */
class GtkBackend : public UIBackend {
public:
    GtkBackend();
    ~GtkBackend() override;

    // Backend info
    Api getApi() const override { return Api::GTK; }
    std::string getApiName() const override { return "gtk"; }
    bool isAvailable() const override;

    // Initialization
    bool initialize() override;
    void shutdown() override;

    // Element creation
    std::shared_ptr<ui::UIElement> window(const std::string &title) override;
    std::shared_ptr<ui::UIElement> panel(const std::string &side) override;
    std::shared_ptr<ui::UIElement> modal(const std::string &title) override;

    // Display elements
    std::shared_ptr<ui::UIElement> text(const std::string &content) override;
    std::shared_ptr<ui::UIElement> label(const std::string &content) override;
    std::shared_ptr<ui::UIElement> image(const std::string &path) override;
    std::shared_ptr<ui::UIElement> icon(const std::string &name) override;
    std::shared_ptr<ui::UIElement> divider() override;
    std::shared_ptr<ui::UIElement> spacer(int size) override;
    std::shared_ptr<ui::UIElement> progress(int value, int max) override;
    std::shared_ptr<ui::UIElement> spinner() override;

    // Input elements
    std::shared_ptr<ui::UIElement> btn(const std::string &label) override;
    std::shared_ptr<ui::UIElement> input(const std::string &placeholder) override;
    std::shared_ptr<ui::UIElement> textarea(const std::string &placeholder) override;
    std::shared_ptr<ui::UIElement> checkbox(const std::string &label, bool checked) override;
    std::shared_ptr<ui::UIElement> toggle(const std::string &label, bool value) override;
    std::shared_ptr<ui::UIElement> slider(int min, int max, int value) override;
    std::shared_ptr<ui::UIElement> dropdown(const std::vector<std::string> &options) override;

    // Layout containers
    std::shared_ptr<ui::UIElement> row() override;
    std::shared_ptr<ui::UIElement> col() override;
    std::shared_ptr<ui::UIElement> grid(int cols) override;
    std::shared_ptr<ui::UIElement> scroll() override;
    std::shared_ptr<ui::UIElement> canvas(int width, int height) override;

    // Menu elements
    std::shared_ptr<ui::UIElement> menu(const std::string &title) override;
    std::shared_ptr<ui::UIElement> menuItem(const std::string &label, const std::string &shortcut) override;
    std::shared_ptr<ui::UIElement> menuSeparator() override;

    // Realization
    void realize(std::shared_ptr<ui::UIElement> element) override;

    // Show/hide
    void show(std::shared_ptr<ui::UIElement> window) override;
    void hide(std::shared_ptr<ui::UIElement> window) override;
    void close(std::shared_ptr<ui::UIElement> window) override;

    // Dialogs
    void alert(const std::string &message) override;
    bool confirm(const std::string &message) override;
    std::string filePicker(const std::string &title) override;
    std::string dirPicker(const std::string &title) override;
    void notify(const std::string &message, const std::string &type) override;

    // Event pumping
    void pumpEvents(int timeoutMs) override;

    // Window state
    bool hasActiveWindows() const override;
    void onAllWindowsClosed(std::function<void()> callback) override;

    // Element value
    std::string getValue(std::shared_ptr<ui::UIElement> element) override;
    void setValue(std::shared_ptr<ui::UIElement> element, const std::string &value) override;

    // System Tray (GTK uses libayatana-appindicator or similar)
    void trayIcon(const std::string &iconPath, const std::string &tooltip) override;
    void trayMenu(std::shared_ptr<ui::UIElement> menu) override;
    void trayNotify(const std::string &title, const std::string &message, const std::string &iconType) override;
    void trayShow() override;
    void trayHide() override;
    bool trayIsVisible() const override;

    // Styling
    void applyStyle(std::shared_ptr<ui::UIElement> element, const std::string &key, const ui::PropValue &value) override;

    // GTK-specific features
    GtkApplication* getApplication() const { return app_; }
    void setApplicationId(const std::string &id) { appId_ = id; }

private:
    GtkApplication* app_ = nullptr;
    std::string appId_ = "org.havel.ui";
    std::unordered_map<std::string, GtkWidget*> widgets_;
    std::function<void()> onAllWindowsClosedCallback_;
    bool initialized_ = false;
    bool trayVisible_ = false;

    // GTK helper methods
    GtkWidget* createWindowInternal(const std::string &title, bool modal = false);
    GtkWidget* createBoxInternal(bool horizontal);
    void setupSignalHandlers();
    static void onWindowClosed(GtkWindow *window, gpointer userData);
    void registerWidget(const std::string &id, GtkWidget *widget);
    GtkWidget* getWidget(const std::string &id) const;
    void destroyWidget(const std::string &id);
};

} // namespace havel::host

#endif // HAVE_GTK_BACKEND
