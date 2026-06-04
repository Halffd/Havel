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
private:
    using DestroyFn = void(*)(void *);
    DestroyFn destroy_fn_ = nullptr;

public:
    enum class Api {
        QT,
        GTK,
        IMGUI,
        AUTO
    };

    virtual ~UIBackend() = default;

    void setDestroyFn(DestroyFn fn) { destroy_fn_ = fn; }
    DestroyFn getDestroyFn() const { return destroy_fn_; }


    struct ApplicationMetadata {
        int* argc = nullptr;
        char** argv = nullptr;
        std::string applicationName;
        std::string applicationVersion;
        std::string organizationName;
        bool quitOnLastWindowClosed = false;
    };

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
    virtual std::shared_ptr<ui::UIElement> table(int rows, int cols) = 0;
    virtual std::shared_ptr<ui::UIElement> flex(const std::string &direction) = 0;
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

    // Event loop management (routed through UI module)
    virtual int runEventLoop() = 0;
    virtual void quitEventLoop(int exitCode = 0) = 0;

    virtual void setApplicationMetadata(const ApplicationMetadata& meta) = 0;

    virtual void resetPerRunState() = 0;

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

    // Canvas drawing
    virtual void canvasFlush(std::shared_ptr<ui::UIElement> canvas) = 0;
    virtual void canvasClear(std::shared_ptr<ui::UIElement> canvas) = 0;

    // Canvas drawing extras (default no-op, Qt overrides)
    virtual void canvasDrawLine(std::shared_ptr<ui::UIElement> canvasEl, int x1, int y1, int x2, int y2) { (void)canvasEl; (void)x1; (void)y1; (void)x2; (void)y2; }
    virtual void canvasDrawRect(std::shared_ptr<ui::UIElement> canvasEl, int x, int y, int w, int h) { (void)canvasEl; (void)x; (void)y; (void)w; (void)h; }
    virtual void canvasDrawCircle(std::shared_ptr<ui::UIElement> canvasEl, int cx, int cy, int r) { (void)canvasEl; (void)cx; (void)cy; (void)r; }
    virtual void canvasSetPen(std::shared_ptr<ui::UIElement> canvasEl, int r, int g, int b, int width) { (void)canvasEl; (void)r; (void)g; (void)b; (void)width; }
    virtual void canvasFill(std::shared_ptr<ui::UIElement> canvasEl, int x, int y) { (void)canvasEl; (void)x; (void)y; }
    virtual void canvasBeginStroke(std::shared_ptr<ui::UIElement> canvasEl) { (void)canvasEl; }
    virtual void canvasEndStroke(std::shared_ptr<ui::UIElement> canvasEl) { (void)canvasEl; }
    virtual bool canvasUndo(std::shared_ptr<ui::UIElement> canvasEl) { (void)canvasEl; return false; }
    virtual std::vector<int> canvasLassoSelect(std::shared_ptr<ui::UIElement> canvasEl, int x, int y) { (void)canvasEl; (void)x; (void)y; return {}; }

    // Timer (default no-op, Qt overrides)
    using TimerCallback = std::function<void()>;
    virtual int64_t timerCreate(int intervalMs, bool singleShot, TimerCallback cb) { (void)intervalMs; (void)singleShot; (void)cb; return 0; }
    virtual void timerStart(int64_t timerId) { (void)timerId; }
    virtual void timerStop(int64_t timerId) { (void)timerId; }
    virtual bool timerIsActive(int64_t timerId) const { (void)timerId; return false; }
    virtual void timerSetInterval(int64_t timerId, int intervalMs) { (void)timerId; (void)intervalMs; }
    virtual void timerSetSingleShot(int64_t timerId, bool singleShot) { (void)timerId; (void)singleShot; }
    virtual void timerDestroy(int64_t timerId) { (void)timerId; }

    // Settings (default no-op, Qt overrides)
    virtual void *settingsCreate(const std::string &org, const std::string &app) { (void)org; (void)app; return nullptr; }
    virtual void settingsDestroy(void *settings) { (void)settings; }
    virtual void settingsSetValue(void *settings, const std::string &key, const std::string &value) { (void)settings; (void)key; (void)value; }
    virtual std::string settingsValue(void *settings, const std::string &key, const std::string &defaultValue) { (void)settings; (void)key; (void)defaultValue; return defaultValue; }
    virtual bool settingsContains(void *settings, const std::string &key) { (void)settings; (void)key; return false; }
    virtual void settingsRemove(void *settings, const std::string &key) { (void)settings; (void)key; }
    virtual void settingsSync(void *settings) { (void)settings; }

    // Extra dialogs (default no-op, Qt overrides)
    virtual std::string colorPicker(const std::string &initialColor) { (void)initialColor; return ""; }
    virtual std::string fontPicker(const std::string &initialFont) { (void)initialFont; return ""; }
    virtual std::string inputText(const std::string &title, const std::string &label, const std::string &defaultValue) { (void)title; (void)label; (void)defaultValue; return ""; }
    virtual int64_t inputInt(const std::string &title, const std::string &label, int defaultValue, int min, int max, int step) { (void)title; (void)label; (void)defaultValue; (void)min; (void)max; (void)step; return defaultValue; }
};

} // namespace havel::host
