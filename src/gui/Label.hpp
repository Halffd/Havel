#pragma once
#include "Widget.hpp"
#include <string>
#include <memory>
#include <cairo/cairo.h>

namespace havel {

class Label : public Widget {
public:
    enum class Alignment {
        LEFT,
        CENTER,
        RIGHT
    };
    
    enum class VAlignment {
        TOP,
        MIDDLE,
        BOTTOM
    };
    
    Label();
    explicit Label(const std::string& text);
    ~Label() override = default;
    
    // Text properties
    void SetText(const std::string& text);
    const std::string& GetText() const { return text; }
    
    // Styling
    void SetFontSize(float size);
    void SetFontWeight(int weight);  // Normal = 400, Bold = 700
    void SetFontStyle(cairo_font_slant_t style);
    void SetColor(uint32_t color);
    void SetOpacity(float opacity);
    
    // Layout
    void SetAlignment(Alignment align);
    void SetVerticalAlignment(VAlignment align);
    void SetWrap(bool wrap) { this->wrap = wrap; NeedsRedraw(); }
    void SetMaxLines(int lines) { maxLines = lines; NeedsRedraw(); }
    void SetLineSpacing(float spacing) { lineSpacing = spacing; NeedsRedraw(); }
    
    // Widget interface
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    bool Contains(int x, int y) const override;
    
    // Measurement
    void GetTextExtents(cairo_t* cr, double& width, double& height) const;
    int GetLineCount() const { return lineCount; }

private:
    // Text content
    std::string text;
    std::vector<std::string> lines;
    int lineCount{0};
    
    // Styling
    float fontSize{14.0f};
    int fontWeight{400};
    cairo_font_slant_t fontStyle{CAIRO_FONT_SLANT_NORMAL};
    uint32_t textColor{0xFFFFFFFF};
    float textOpacity{1.0f};
    
    // Layout
    Alignment alignment{Alignment::LEFT};
    VAlignment vAlignment{VAlignment::MIDDLE};
    bool wrap{false};
    int maxLines{0};  // 0 = unlimited
    float lineSpacing{1.2f};
    
    // Cached measurements
    mutable struct {
        double width{0};
        double height{0};
        bool valid{false};
    } textExtents;
    
    // Animation properties
    struct TextAnimation {
        enum class Type {
            FADE_IN,
            FADE_OUT,
            TYPE_WRITER,
            SLIDE_IN,
            SLIDE_OUT,
            BOUNCE
        };
        
        Type type{Type::FADE_IN};
        float progress{0.0f};
        float duration{0.3f};
        bool running{false};
        std::string targetText;
        
        // Animation-specific properties
        struct {
            float startX{0}, startY{0};
            float endX{0}, endY{0};
            float bounce{1.2f};
        } params;
    };
    
    TextAnimation currentAnimation;
    
    // Animation methods
    void UpdateTextAnimation(float deltaTime);
    void ApplyTextAnimation(cairo_t* cr);
    float EaseInOutCubic(float t);
    float Bounce(float t);
    
    // Helper methods
    void UpdateLineBreaks(cairo_t* cr);
    void DrawLine(cairo_t* cr, const std::string& line, double x, double y);
    void InvalidateExtents() { textExtents.valid = false; }
    
    // Rendering properties
    struct RenderEffects {
        bool shadow{false};
        float shadowOffset{2.0f};
        uint32_t shadowColor{0x00000080};
        
        bool outline{false};
        float outlineWidth{1.0f};
        uint32_t outlineColor{0x000000FF};
        
        bool gradient{false};
        uint32_t gradientStart{0xFFFFFFFF};
        uint32_t gradientEnd{0xFFFFFFFF};
        float gradientAngle{0.0f}; // in radians
        
        bool blur{false};
        float blurRadius{2.0f};
        
        bool glow{false};
        float glowRadius{4.0f};
        uint32_t glowColor{0x61AFEFFF};
    };
    
    RenderEffects effects;
    
    // Rendering methods
    void ApplyEffects(cairo_t* cr);
    void DrawTextWithEffects(cairo_t* cr, const std::string& text, double x, double y);
    void CreateGradientPattern(cairo_t* cr, double x, double y, double width, double height);
};

} // namespace havel 