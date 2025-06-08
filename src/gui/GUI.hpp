#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cairomm/cairomm.h>
#include "../utils/SafeQueue.hpp"
#include "../window/WindowManager.hpp"

// Forward declarations
struct _cairo;
typedef struct _cairo cairo_t;

namespace havel {

// Forward declarations
class Widget;
class Layout;

// Theme structure
struct Theme {
    struct Colors {
        uint32_t background{0x282c34};
        uint32_t foreground{0xabb2bf};
        uint32_t accent{0x61afef};
        uint32_t warning{0xe5c07b};
        uint32_t error{0xe06c75};
        uint32_t selection{0x3e4451};
        uint32_t highlight{0x2c313c};
        uint32_t border{0x181a1f};
        uint32_t shadow{0x0000007f};
        uint32_t buttonNormal{0x353b45};
        uint32_t buttonHover{0x3e4451};
        uint32_t buttonPress{0x2c313c};
        uint32_t inputBackground{0x1b1d23};
        uint32_t tooltipBackground{0x21252b};
    } colors;

    struct Metrics {
        int padding{8};
        int margin{4};
        int borderWidth{1};
        int cornerRadius{4};
        int shadowRadius{8};
        int iconSize{16};
        int minButtonWidth{80};
        int minButtonHeight{24};
        float opacity{0.95f};
        int buttonSpacing{8};
        int buttonMargin{8};
        int titleBarHeight{30};
        int buttonHeight{24};
    } metrics;

    struct Fonts {
        std::string regular{"Sans:size=10"};
        std::string bold{"Sans:bold:size=10"};
        std::string mono{"Monospace:size=10"};
        std::string title{"Sans:size=12:weight=bold"};
        std::string small{"Sans:size=8"};
        float dpi{96.0f};
        float scaling{1.0f};
    } fonts;
};

// Widget base class
class Widget {
public:
    virtual ~Widget() = default;
    virtual void Draw(cairo_t* cr) = 0;
    virtual void HandleEvent(const GUIEvent& event) = 0;
    virtual void SetBounds(int x, int y, int width, int height) = 0;
    virtual bool Contains(int x, int y) const = 0;
    
    void SetVisible(bool visible) { isVisible = visible; }
    void SetEnabled(bool enabled) { isEnabled = enabled; }
    void SetTheme(const Theme& theme) { this->theme = theme; }
    
protected:
    bool isVisible{true};
    bool isEnabled{true};
    Theme theme;
    
    struct {
        int x{0}, y{0};
        int width{0}, height{0};
    } bounds;
};

// Layout base class
class Layout {
public:
    virtual ~Layout() = default;
    virtual void AddWidget(std::shared_ptr<Widget> widget) = 0;
    virtual void RemoveWidget(std::shared_ptr<Widget> widget) = 0;
    virtual void UpdateLayout() = 0;
};

// GUI Event structure
struct GUIEvent {
    enum class Type {
        // Mouse events
        MOUSE_MOVE,
        MOUSE_CLICK,
        MOUSE_DRAG,
        MOUSE_WHEEL,
        MOUSE_ENTER,
        MOUSE_LEAVE,
        
        // Keyboard events
        KEY_PRESS,
        KEY_RELEASE,
        KEY_REPEAT,
        
        // Window events
        WINDOW_MOVE,
        WINDOW_RESIZE,
        WINDOW_FOCUS,
        WINDOW_BLUR,
        WINDOW_MINIMIZE,
        WINDOW_MAXIMIZE,
        WINDOW_RESTORE,
        
        // Theme events
        THEME_CHANGE,
        THEME_RELOAD,
        
        // System events
        DISPLAY_CHANGE,
        DPI_CHANGE,
        QUIT
    };
    
    Type type;
    int x{0}, y{0};
    int width{0}, height{0};
    unsigned int button{0};
    unsigned int keycode{0};
    unsigned int modifiers{0};
    std::string data;
};
class Window;
class Theme;

// GUI Events
struct GUIEvent {
    enum class Type {
        // Mouse events
        MOUSE_MOVE,
        MOUSE_CLICK,
        MOUSE_DRAG,
        MOUSE_WHEEL,
        MOUSE_ENTER,
        MOUSE_LEAVE,
        
        // Keyboard events
        KEY_PRESS,
        KEY_RELEASE,
        KEY_REPEAT,
        
        // Window events
        WINDOW_MOVE,
        WINDOW_RESIZE,
        WINDOW_FOCUS,
        WINDOW_BLUR,
        WINDOW_MINIMIZE,
        WINDOW_MAXIMIZE,
        WINDOW_RESTORE,
        
        // Theme events
        THEME_CHANGE,
        THEME_RELOAD,
        
        // System events
        DISPLAY_CHANGE,
        DPI_CHANGE,
        QUIT
    };
    
    Type type;
    int x{0}, y{0};
    int width{0}, height{0};
    unsigned int button{0};
    unsigned int keycode{0};
    unsigned int modifiers{0};
    std::string data;
};

// Base GUI interface
class IGUIBackend {
public:
    virtual ~IGUIBackend() = default;
    
    virtual void Initialize() = 0;
    virtual void Cleanup() noexcept = 0;
    virtual void ProcessEvents() = 0;
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void SetPosition(int x, int y) = 0;
    virtual void SetSize(int width, int height) = 0;
    virtual void SetTitle(const std::string& title) = 0;
    virtual void SetOpacity(float opacity) = 0;
    virtual void QueueRedraw() = 0;
};

// X11 implementation
class X11Backend : public IGUIBackend {
public:
    X11Backend(const Theme& theme);
    ~X11Backend() noexcept override;
    
    void Initialize() override;
    void Cleanup() noexcept override;
    void ProcessEvents() override;
    void Show() override;
    void Hide() override;
    void SetPosition(int x, int y) override;
    void SetSize(int width, int height) override;
    void SetTitle(const std::string& title) override;
    void SetOpacity(float opacity) override;
    void QueueRedraw() override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// GTK implementation
class GTKBackend : public IGUIBackend {
public:
    GTKBackend(const Theme& theme);
    ~GTKBackend() noexcept override;
    
    void Initialize() override;
    void Cleanup() noexcept override;
    void ProcessEvents() override;
    void Show() override;
    void Hide() override;
    void SetPosition(int x, int y) override;
    void SetSize(int width, int height) override;
    void SetTitle(const std::string& title) override;
    void SetOpacity(float opacity) override;
    void QueueRedraw() override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Main GUI class
class GUI {
public:
    enum class Mode {
        X11,
        GTK
    };
    
    explicit GUI(std::shared_ptr<WindowManager> windowManager, Mode mode = Mode::GTK);
    ~GUI() noexcept;
    
    // Non-copyable, movable
    GUI(const GUI&) = delete;
    GUI& operator=(const GUI&) = delete;
    GUI(GUI&&) noexcept = default;
    GUI& operator=(GUI&&) noexcept = default;
    
    // Control
    void Start();
    void Stop() noexcept;
    bool IsRunning() const noexcept { return running.load(std::memory_order_acquire); }
    
    // Mode switching
    void SwitchMode(Mode mode);
    Mode GetCurrentMode() const noexcept { return currentMode; }
    
    // Event handling
    using EventCallback = std::function<void(const GUIEvent&)>;
    void SetEventCallback(EventCallback callback);
    
    // Window management
    void ShowWindow();
    void HideWindow();
    void SetWindowPosition(int x, int y);
    void SetWindowSize(int width, int height);
    
    // Theme
    void SetTheme(const Theme& theme);
    void RefreshTheme();
    
    // Rendering
    void QueueRedraw();
    void SetTitle(const std::string& title);
    void SetOpacity(float opacity);
    
    // Widget management
    void AddWidget(std::shared_ptr<Widget> widget);
    void RemoveWidget(std::shared_ptr<Widget> widget);
    void SetLayout(std::shared_ptr<Layout> layout);
    
    // Rendering features
    void EnableVSync(bool enable);
    void SetFrameRate(int fps);
    void EnableDoubleBuffering(bool enable);
    void EnableAntialiasing(bool enable);
    
    // Animation
    void StartAnimation(const std::string& name, float duration);
    void StopAnimation(const std::string& name);
    void SetAnimationCallback(const std::string& name, std::function<void(float)> callback);

private:
    // Thread management
    std::unique_ptr<std::thread> eventThread;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    
    // Event queue
    SafeQueue<GUIEvent> eventQueue;
    
    // Backend management
    Mode currentMode;
    std::unique_ptr<IGUIBackend> backend;
    std::shared_ptr<WindowManager> windowManager;
    Theme theme;
    
    // Event handling
    void ProcessEvents();
    void OnWindowClosed();
    EventCallback eventCallback;
    
    // Helper methods
    void InitializeBackend();
    void CleanupBackend() noexcept;
    
    std::vector<std::shared_ptr<Widget>> widgets;
    std::shared_ptr<Layout> layout;
    
    // Animation system
    struct Animation {
        float duration;
        float currentTime;
        bool running;
        std::function<void(float)> callback;
    };
    std::unordered_map<std::string, Animation> animations;
    
    void UpdateAnimations(float deltaTime);
    void RenderFrame();
};

    void SetText(const std::string& text) { this->text = text; }
    void SetIcon(const std::string& iconPath) { this->iconPath = iconPath; }
    void SetCallback(std::function<void()> callback) { this->callback = callback; }
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    void SetBounds(int x, int y, int width, int height) override;
    bool Contains(int x, int y) const override;

private:
    std::string text;
    std::string iconPath;
    std::function<void()> callback;
};

class Label : public Widget {
public:
    void SetText(const std::string& text) { this->text = text; }
    void SetAlignment(float x, float y) { alignX = x; alignY = y; }
    void SetWrap(bool wrap) { this->wrap = wrap; }
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override {}
    void SetBounds(int x, int y, int width, int height) override {
        bounds.x = x;
        bounds.y = y;
        bounds.width = width;
        bounds.height = height;
    }
    bool Contains(int x, int y) const override {
        return x >= bounds.x && x <= bounds.x + bounds.width &&
               y >= bounds.y && y <= bounds.y + bounds.height;
    }

private:
    std::string text;
    float alignX{0.0f}, alignY{0.5f};
    bool wrap{false};
};

// Layout managers
class GridLayout : public Layout {
public:
    void AddWidget(std::shared_ptr<Widget> widget) override;
    void RemoveWidget(std::shared_ptr<Widget> widget) override;
    void UpdateLayout() override;

private:
    std::vector<std::shared_ptr<Widget>> widgets;
};

class FlowLayout : public Layout {
public:
    void AddWidget(std::shared_ptr<Widget> widget) override;
    void RemoveWidget(std::shared_ptr<Widget> widget) override;
    void UpdateLayout() override;

private:
    std::vector<std::shared_ptr<Widget>> widgets;
};

} // namespace havel 