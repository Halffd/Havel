/*
 * ImGuiBackend.hpp - Dear ImGui implementation of UIBackend
 *
 * Native Dear ImGui backend using OpenGL/GLFW for the Havel UI system.
 */
#pragma once

#ifdef HAVE_IMGUI_BACKEND

#include "UIBackend.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

// Forward declarations for ImGui and GLFW
struct GLFWwindow;
struct ImGuiContext;
struct ImFont;

namespace havel::host {

/**
 * ImGuiBackend - Dear ImGui implementation of UI backend
 *
 * Provides immediate-mode GUI using Dear ImGui with GLFW/OpenGL.
 */
class ImGuiBackend : public UIBackend {
public:
    ImGuiBackend();
    ~ImGuiBackend() override;

    // Backend info
    Api getApi() const override { return Api::IMGUI; }
    std::string getApiName() const override { return "imgui"; }
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

    // System Tray (not applicable for ImGui, but required by interface)
    void trayIcon(const std::string &iconPath, const std::string &tooltip) override;
    void trayMenu(std::shared_ptr<ui::UIElement> menu) override;
    void trayNotify(const std::string &title, const std::string &message, const std::string &iconType) override;
    void trayShow() override;
    void trayHide() override;
    bool trayIsVisible() const override;

    // Styling
    void applyStyle(std::shared_ptr<ui::UIElement> element, const std::string &key, const ui::PropValue &value) override;

    // ImGui-specific features
    void setWindowTitle(const std::string &title);
    void setWindowSize(int width, int height);
    void setFrameRate(int fps);
    
    // Access internal ImGui context
    ImGuiContext* getContext() const { return context_; }

private:
    GLFWwindow* window_ = nullptr;
    ImGuiContext* context_ = nullptr;
    bool initialized_ = false;
    bool running_ = false;
    int windowWidth_ = 1280;
    int windowHeight_ = 720;
    std::string windowTitle_ = "Havel UI";
    int targetFps_ = 60;
    
    std::unordered_map<std::string, std::shared_ptr<ui::UIElement>> elements_;
    std::unordered_map<std::string, std::string> elementValues_;
    std::unordered_map<std::string, bool> elementOpen_; // for windows
    std::vector<std::string> windowStack_;
    std::function<void()> onAllWindowsClosedCallback_;
    
    // ImGui-specific storage
    std::unordered_map<std::string, int> intValues_;
    std::unordered_map<std::string, float> floatValues_;
    std::unordered_map<std::string, bool> boolValues_;
    std::unordered_map<std::string, std::string> textValues_;
    std::unordered_map<std::string, int> comboSelections_;
    
    // Tray (not applicable, but track for interface compliance)
    bool trayVisible_ = false;
    std::string trayIconPath_;
    std::string trayTooltip_;

    // Helper methods
    bool initGLFW();
    bool initImGui();
    void shutdownGLFW();
    void shutdownImGui();
    void renderFrame();
    void processInputs();
    
    // Element ID generation
    std::string generateId(const std::string &prefix);
    int idCounter_ = 0;
    
    // Drawing helpers
    void drawWindow(std::shared_ptr<ui::UIElement> element);
    void drawButton(std::shared_ptr<ui::UIElement> element);
    void drawInput(std::shared_ptr<ui::UIElement> element);
    void drawTextArea(std::shared_ptr<ui::UIElement> element);
    void drawCheckbox(std::shared_ptr<ui::UIElement> element);
    void drawToggle(std::shared_ptr<ui::UIElement> element);
    void drawSlider(std::shared_ptr<ui::UIElement> element);
    void drawDropdown(std::shared_ptr<ui::UIElement> element);
    void drawLabel(std::shared_ptr<ui::UIElement> element);
    void drawText(std::shared_ptr<ui::UIElement> element);
    void drawProgress(std::shared_ptr<ui::UIElement> element);
    void drawImage(std::shared_ptr<ui::UIElement> element);
    void drawIcon(std::shared_ptr<ui::UIElement> element);
    void drawRow(std::shared_ptr<ui::UIElement> element);
    void drawCol(std::shared_ptr<ui::UIElement> element);
    void drawGrid(std::shared_ptr<ui::UIElement> element);
    void drawScroll(std::shared_ptr<ui::UIElement> element);
    void drawCanvas(std::shared_ptr<ui::UIElement> element);
    void drawMenu(std::shared_ptr<ui::UIElement> element);
    void drawMenuItem(std::shared_ptr<ui::UIElement> element);
    void drawDivider();
    void drawSpacer(int size);
    void drawSpinner();
};

} // namespace havel::host

#endif // HAVE_IMGUI_BACKEND
