/*
 * ImGuiBackend.hpp - Dear ImGui implementation of UIBackend
 *
 * Native Dear ImGui backend using OpenGL/GLFW for the Havel UI system.
 */
#pragma once

#ifdef HAVE_IMGUI_BACKEND

#include "UIBackend.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <deque>
#include <filesystem>

// Forward declarations for ImGui and GLFW
struct GLFWwindow;
struct ImGuiContext;
struct ImFont;
struct ImVec2;

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
    std::shared_ptr<ui::UIElement> table(int rows, int cols) override;
    std::shared_ptr<ui::UIElement> flex(const std::string &direction) override;
    std::shared_ptr<ui::UIElement> scroll() override;
    std::shared_ptr<ui::UIElement> canvas(int width, int height) override;

    // Menu elements
    std::shared_ptr<ui::UIElement> menu(const std::string &title) override;
    std::shared_ptr<ui::UIElement> menuItem(const std::string &label, const std::string &shortcut) override;
    std::shared_ptr<ui::UIElement> menuSeparator() override;

    // Realization
    void realize(std::shared_ptr<ui::UIElement> element) override;

    // Element resolution and parenting (id-only over the ffi shim)
    std::shared_ptr<ui::UIElement> resolve(ui::ElementId id) override;
    void addChild(std::shared_ptr<ui::UIElement> parent,
                  std::shared_ptr<ui::UIElement> child) override;

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

    // Event loop management (routed through UI module)
    int runEventLoop() override;
    void quitEventLoop(int exitCode = 0) override;
    void setApplicationMetadata(const ApplicationMetadata& meta) override;
    void resetPerRunState() override;
    void setIdleCallback(std::function<void()> cb) override;

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

  // Canvas drawing
  void canvasFlush(std::shared_ptr<ui::UIElement> canvas) override;
  void canvasClear(std::shared_ptr<ui::UIElement> canvas) override;
  void canvasDrawLine(std::shared_ptr<ui::UIElement> canvas, int x1, int y1, int x2,
                      int y2) override;
  void canvasDrawRect(std::shared_ptr<ui::UIElement> canvas, int x, int y, int w,
                      int h) override;
  void canvasDrawCircle(std::shared_ptr<ui::UIElement> canvas, int cx, int cy,
                        int r) override;
  void canvasSetPen(std::shared_ptr<ui::UIElement> canvas, int r, int g, int b,
                    int width) override;
  void canvasFill(std::shared_ptr<ui::UIElement> canvas, int x, int y) override;

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
    int exitCode_ = 0;
    UIBackend::ApplicationMetadata appMeta_;
    
    std::unordered_map<ui::ElementId, std::shared_ptr<ui::UIElement>> elements_;
    std::unordered_map<ui::ElementId, std::string> elementValues_;
    std::unordered_map<ui::ElementId, bool> elementOpen_; // window visibility
    std::vector<ui::ElementId> windowStack_;
    // Creation order: immediate mode redraws the tree in this order so widget
    // ids and stacking stay stable between frames.
    std::vector<ui::ElementId> creationOrder_;
    std::function<void()> onAllWindowsClosedCallback_;
    std::function<void()> idleCallback_;
    
    // ImGui-specific storage
    std::unordered_map<ui::ElementId, int> intValues_;
    std::unordered_map<ui::ElementId, float> floatValues_;
    std::unordered_map<ui::ElementId, bool> boolValues_;
    std::unordered_map<ui::ElementId, std::string> textValues_;
    std::unordered_map<ui::ElementId, int> comboSelections_;
    std::unordered_map<ui::ElementId, std::vector<std::string>> dropdownOptions_;
    
    // Tray (not applicable, but track for interface compliance)
    bool trayVisible_ = false;
    std::string trayIconPath_;
    std::string trayTooltip_;

    // In-app notifications, drawn as an overlay for a few seconds
    struct Toast {
        std::string message;
        std::string type;
        double expiresAt = 0.0;
    };
    std::deque<Toast> toasts_;

    // Modal dialogs: the synchronous api sets the pending dialog and pumps
    // frames until the user answers.
    struct PendingDialog {
        enum class Kind { Alert, Confirm, File, Dir } kind = Kind::Alert;
        std::string message;
        std::string title;
        std::string path;            // file/dir browser result
        std::filesystem::path dir;   // file/dir browser position
        bool answer = false;         // confirm result
        bool done = false;
    };
    std::unique_ptr<PendingDialog> pendingDialog_;
    std::string fileDialogBuffer_;
    double now_ = 0.0;
    double fpsLimit_ = 0.0;
    // Frames actually presented since init; reported on close/shutdown so a
    // silent "not rendering" path is visible in logs.
    uint64_t frameCount_ = 0;

    // Helper methods
    bool initGLFW();
    bool initImGui();
    void shutdownGLFW();
    void shutdownImGui();
    void reportGlfwError(const char *where) const;
    void renderFrame();
    void processInputs();

    // Runs frames until `done` flips or the deadline passes, so the
    // synchronous dialog api can block on a user answer.
    void pumpUntilAnswered(const std::function<bool()> &done, int timeoutMs);
    void drawPendingDialog();
    bool drawFileBrowser(bool directoriesOnly);

    // Element ID generation: unique ElementId per element (registry key over
    // the FFI shim, which passes id-only placeholder elements back in)
    std::shared_ptr<ui::UIElement> createElem(const char *type);
    ui::ElementId nextId_ = 1;

    // Stable ImGui widget label for an element id
    static std::string imguiId(const ui::ElementId id, const char *suffix = "");

    // Immediate-mode tree walk
    void drawElement(const std::shared_ptr<ui::UIElement> &element);
    void drawChildren(const std::shared_ptr<ui::UIElement> &parent);
    void drawMenuContents(const std::shared_ptr<ui::UIElement> &menu);
    void drawMenuBarFor(const std::shared_ptr<ui::UIElement> &window);
    void replayCanvas(const std::shared_ptr<ui::UIElement> &element,
                      ImVec2 origin);
    void drawToasts();
    void dropElement(ui::ElementId id);

};

} // namespace havel::host

#endif // HAVE_IMGUI_BACKEND
