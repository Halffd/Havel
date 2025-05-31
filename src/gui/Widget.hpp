#pragma once
#include <memory>
#include "Theme.hpp"
#include "GUIEvent.hpp"

namespace havel {

enum class IconPosition {
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
    CENTER
};

class Widget {
public:
    Widget();
    virtual ~Widget() = default;
    
    // Core interface
    virtual void Draw(cairo_t* cr) = 0;
    virtual void HandleEvent(const GUIEvent& event) = 0;
    virtual void SetBounds(int x, int y, int width, int height);
    virtual bool Contains(int x, int y) const = 0;
    
    // Common properties
    void SetVisible(bool visible) { isVisible = visible; NeedsRedraw(); }
    void SetEnabled(bool enabled) { isEnabled = enabled; NeedsRedraw(); }
    void SetTheme(const Theme& theme) { this->theme = theme; NeedsRedraw(); }
    
    // Getters
    bool IsVisible() const { return isVisible; }
    bool IsEnabled() const { return isEnabled; }
    const Theme& GetTheme() const { return theme; }
    
    // Parent/child relationship
    void SetParent(Widget* parent);
    Widget* GetParent() const { return parent; }
    
    // Animation
    void StartAnimation(const std::string& name, float duration);
    void StopAnimation(const std::string& name);
    bool IsAnimating() const { return !animations.empty(); }
    
protected:
    bool isVisible{true};
    bool isEnabled{true};
    Theme theme;
    struct Bounds {
        int x{0}, y{0};
        int width{0}, height{0};
    } bounds;
    
    // Helper methods
    void NeedsRedraw();
    void InvalidateLayout();
    
    // Animation system
    struct Animation {
        float duration;
        float currentTime{0};
        float startValue{0};
        float endValue{0};
        bool running{true};
        std::function<void(float)> callback;
    };
    std::unordered_map<std::string, Animation> animations;
    void UpdateAnimations(float deltaTime);
    
private:
    Widget* parent{nullptr};
    bool needsRedraw{false};
    bool needsLayout{false};
};

} // namespace havel 