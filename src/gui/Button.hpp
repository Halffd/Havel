#pragma once
#include "Widget.hpp"
#include <cairo/cairo.h>
#include <string>
#include <functional>
#include <memory>

namespace havel {

class Button : public Widget {
public:
    Button();
    ~Button() override = default;
    
    // Text/Icon
    void SetText(const std::string& text) { this->text = text; NeedsRedraw(); }
    void SetIcon(const std::string& iconPath);
    void SetIconPosition(IconPosition pos) { iconPosition = pos; NeedsRedraw(); }
    
    // Styling
    void SetRoundedCorners(bool rounded) { this->roundedCorners = rounded; NeedsRedraw(); }
    void SetElevation(float elevation) { this->elevation = elevation; NeedsRedraw(); }
    void SetHoverEffect(bool enable) { hoverEffect = enable; }
    
    // Callbacks
    void SetCallback(std::function<void()> callback) { onClick = std::move(callback); }
    void SetHoverCallback(std::function<void(bool)> callback) { onHover = std::move(callback); }
    
    // Widget interface
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    bool Contains(int x, int y) const override;

private:
    // State
    std::string text;
    std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> icon;
    IconPosition iconPosition{IconPosition::LEFT};
    bool isHovered{false};
    bool isPressed{false};
    
    // Style
    bool roundedCorners{true};
    float elevation{2.0f};
    bool hoverEffect{true};
    
    // Callbacks
    std::function<void()> onClick;
    std::function<void(bool)> onHover;
    
    // Animation
    float pressAnimation{0.0f};
    float hoverAnimation{0.0f};
    
    // Drawing helpers
    void DrawBackground(cairo_t* cr);
    void DrawText(cairo_t* cr);
    void DrawIcon(cairo_t* cr);
    void DrawShadow(cairo_t* cr);
    void UpdateAnimations(float deltaTime);
};

} // namespace havel 