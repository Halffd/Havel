#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <memory>
#include <functional>
#include <gtkmm.h>
#include "WindowManager.hpp"
#include "IO.hpp"

namespace havel {

// Forward declarations
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

// Theme system
class Theme {
public:
    struct Colors {
        // Base colors
        uint32_t background{0x282c34};
        uint32_t foreground{0xabb2bf};
        uint32_t accent{0x61afef};
        uint32_t warning{0xe5c07b};
        uint32_t error{0xe06c75};
        
        // Additional colors
        uint32_t selection{0x3e4451};
        uint32_t highlight{0x2c313c};
        uint32_t border{0x181a1f};
        uint32_t shadow{0x0000007f};
        
        // Widget colors
        uint32_t buttonNormal{0x353b45};
        uint32_t buttonHover{0x3e4451};
        uint32_t buttonPress{0x2c313c};
        uint32_t inputBackground{0x1b1d23};
        uint32_t tooltipBackground{0x21252b};
    };
    
    struct Fonts {
        std::string regular{"Sans:size=10"};
        std::string bold{"Sans:bold:size=10"};
        std::string mono{"Monospace:size=10"};
        std::string title{"Sans:size=12:weight=bold"};
        std::string small{"Sans:size=8"};
        
        float dpi{96.0f};
        float scaling{1.0f};
    };
    
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
    };
    
    struct Animation {
        int duration{250};  // milliseconds
        std::string easing{"cubic-bezier(0.4, 0.0, 0.2, 1)"};
        bool enabled{true};
    };
    
    Colors colors;
    Fonts fonts;
    Metrics metrics;
    Animation animation;
    
    // Theme loading/saving
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename) const;
    void Reset();  // Reset to defaults
};

// GUI Elements
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

// Specific widgets
class Button : public Widget {
public:
    void SetText(const std::string& text);
    void SetIcon(const std::string& iconPath);
    void SetCallback(std::function<void()> callback);
    void Draw(cairo_t* cr) override;
    // ...
};

class Label : public Widget {
public:
    void SetText(const std::string& text);
    void SetAlignment(float x, float y);
    void SetWrap(bool wrap);
    void Draw(cairo_t* cr) override;
    // ...
};

class TextInput : public Widget {
public:
    void SetText(const std::string& text);
    void SetPlaceholder(const std::string& text);
    void SetPassword(bool isPassword);
    void Draw(cairo_t* cr) override;
    // ...
};

// Layout managers
class Layout {
public:
    virtual ~Layout() = default;
    virtual void AddWidget(std::shared_ptr<Widget> widget) = 0;
    virtual void RemoveWidget(std::shared_ptr<Widget> widget) = 0;
    virtual void UpdateLayout() = 0;
};

class GridLayout : public Layout {
    // Grid-based layout
};

class FlowLayout : public Layout {
    // Flow-based layout
};

} // namespace havel 