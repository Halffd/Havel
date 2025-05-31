#include "Label.hpp"
#include <algorithm>
#include <cmath>

namespace havel {

Label::Label() = default;

Label::Label(const std::string& text) : text(text) {
    InvalidateExtents();
}

void Label::SetText(const std::string& text) {
    if (this->text == text) return;
    this->text = text;
    InvalidateExtents();
    NeedsRedraw();
}

void Label::SetFontSize(float size) {
    if (fontSize == size) return;
    fontSize = size;
    InvalidateExtents();
    NeedsRedraw();
}

void Label::SetFontWeight(int weight) {
    if (fontWeight == weight) return;
    fontWeight = weight;
    InvalidateExtents();
    NeedsRedraw();
}

void Label::SetFontStyle(cairo_font_slant_t style) {
    if (fontStyle == style) return;
    fontStyle = style;
    InvalidateExtents();
    NeedsRedraw();
}

void Label::SetColor(uint32_t color) {
    if (textColor == color) return;
    textColor = color;
    NeedsRedraw();
}

void Label::SetOpacity(float opacity) {
    if (textOpacity == opacity) return;
    textOpacity = std::clamp(opacity, 0.0f, 1.0f);
    NeedsRedraw();
}

void Label::SetAlignment(Alignment align) {
    if (alignment == align) return;
    alignment = align;
    NeedsRedraw();
}

void Label::SetVerticalAlignment(VAlignment align) {
    if (vAlignment == align) return;
    vAlignment = align;
    NeedsRedraw();
}

void Label::Draw(cairo_t* cr) {
    if (!isVisible || text.empty()) return;
    
    // Save state
    cairo_save(cr);
    
    // Set up font
    cairo_select_font_face(cr, theme.fonts.regular.c_str(),
        fontStyle,
        fontWeight >= 700 ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, fontSize * theme.fonts.scaling);
    
    // Update line breaks if needed
    if (wrap) {
        UpdateLineBreaks(cr);
    } else {
        lines = {text};
        lineCount = 1;
    }
    
    // Calculate total height
    double lineHeight = fontSize * lineSpacing;
    double totalHeight = lineHeight * lineCount;
    
    // Calculate starting Y position
    double y;
    switch (vAlignment) {
        case VAlignment::TOP:
            y = bounds.y;
            break;
        case VAlignment::MIDDLE:
            y = bounds.y + (bounds.height - totalHeight) / 2;
            break;
        case VAlignment::BOTTOM:
            y = bounds.y + bounds.height - totalHeight;
            break;
    }
    
    // Set text color
    cairo_set_source_rgba(cr,
        ((textColor >> 16) & 0xFF) / 255.0,
        ((textColor >> 8) & 0xFF) / 255.0,
        (textColor & 0xFF) / 255.0,
        ((textColor >> 24) & 0xFF) / 255.0 * textOpacity);
    
    // Draw each line
    for (const auto& line : lines) {
        DrawLine(cr, line, bounds.x, y);
        y += lineHeight;
    }
    
    // Restore state
    cairo_restore(cr);
}

void Label::DrawLine(cairo_t* cr, const std::string& line, double x, double y) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, line.c_str(), &extents);
    
    // Calculate X position based on alignment
    switch (alignment) {
        case Alignment::LEFT:
            // x remains unchanged
            break;
        case Alignment::CENTER:
            x += (bounds.width - extents.width) / 2;
            break;
        case Alignment::RIGHT:
            x += bounds.width - extents.width;
            break;
    }
    
    // Draw text
    cairo_move_to(cr, x, y + extents.height);
    cairo_show_text(cr, line.c_str());
}

void Label::UpdateLineBreaks(cairo_t* cr) {
    lines.clear();
    lineCount = 0;
    
    if (bounds.width <= 0) {
        lines.push_back(text);
        lineCount = 1;
        return;
    }
    
    std::string currentLine;
    std::istringstream stream(text);
    std::string word;
    
    cairo_text_extents_t extents;
    double lineWidth = 0;
    
    while (stream >> word) {
        // Get word width
        cairo_text_extents(cr, word.c_str(), &extents);
        double wordWidth = extents.width;
        
        // Check if word fits on current line
        if (lineWidth + wordWidth <= bounds.width) {
            if (!currentLine.empty()) {
                currentLine += " ";
                lineWidth += theme.metrics.padding; // Space width
            }
            currentLine += word;
            lineWidth += wordWidth;
        } else {
            // Start new line
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
                lineCount++;
            }
            currentLine = word;
            lineWidth = wordWidth;
        }
    }
    
    // Add last line
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
        lineCount++;
    }
    
    // Truncate if maxLines is set
    if (maxLines > 0 && lineCount > maxLines) {
        lines.resize(maxLines);
        lineCount = maxLines;
        // Add ellipsis to last line
        if (!lines.empty()) {
            lines.back() += "...";
        }
    }
}

void Label::HandleEvent(const GUIEvent& event) {
    // Labels typically don't handle events
}

bool Label::Contains(int x, int y) const {
    return x >= bounds.x && x < bounds.x + bounds.width &&
           y >= bounds.y && y < bounds.y + bounds.height;
}

void Label::GetTextExtents(cairo_t* cr, double& width, double& height) const {
    if (textExtents.valid) {
        width = textExtents.width;
        height = textExtents.height;
        return;
    }
    
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text.c_str(), &extents);
    
    textExtents.width = extents.width;
    textExtents.height = extents.height;
    textExtents.valid = true;
    
    width = textExtents.width;
    height = textExtents.height;
}

void Label::UpdateTextAnimation(float deltaTime) {
    if (!currentAnimation.running) return;
    
    currentAnimation.progress = std::min(1.0f, 
        currentAnimation.progress + deltaTime / currentAnimation.duration);
    
    if (currentAnimation.progress >= 1.0f) {
        currentAnimation.running = false;
        text = currentAnimation.targetText;
    }
    
    NeedsRedraw();
}

void Label::ApplyTextAnimation(cairo_t* cr) {
    if (!currentAnimation.running) return;
    
    float t = currentAnimation.progress;
    
    switch (currentAnimation.type) {
        case TextAnimation::Type::FADE_IN:
            textOpacity = EaseInOutCubic(t);
            break;
            
        case TextAnimation::Type::FADE_OUT:
            textOpacity = 1.0f - EaseInOutCubic(t);
            break;
            
        case TextAnimation::Type::TYPE_WRITER: {
            size_t charCount = static_cast<size_t>(t * text.length());
            text = currentAnimation.targetText.substr(0, charCount);
            break;
        }
        
        case TextAnimation::Type::SLIDE_IN: {
            float x = currentAnimation.params.startX + 
                     (currentAnimation.params.endX - currentAnimation.params.startX) * EaseInOutCubic(t);
            float y = currentAnimation.params.startY +
                     (currentAnimation.params.endY - currentAnimation.params.startY) * EaseInOutCubic(t);
            cairo_translate(cr, x, y);
            break;
        }
        
        case TextAnimation::Type::BOUNCE: {
            float scale = 1.0f + (currentAnimation.params.bounce - 1.0f) * Bounce(t);
            cairo_translate(cr, bounds.width / 2, bounds.height / 2);
            cairo_scale(cr, scale, scale);
            cairo_translate(cr, -bounds.width / 2, -bounds.height / 2);
            break;
        }
    }
}

void Label::ApplyEffects(cairo_t* cr) {
    if (effects.shadow) {
        // Draw shadow
        cairo_save(cr);
        cairo_translate(cr, effects.shadowOffset, effects.shadowOffset);
        cairo_set_source_rgba(cr,
            ((effects.shadowColor >> 16) & 0xFF) / 255.0,
            ((effects.shadowColor >> 8) & 0xFF) / 255.0,
            (effects.shadowColor & 0xFF) / 255.0,
            ((effects.shadowColor >> 24) & 0xFF) / 255.0);
        cairo_show_text(cr, text.c_str());
        cairo_restore(cr);
    }
    
    if (effects.outline) {
        // Draw outline
        cairo_save(cr);
        cairo_set_line_width(cr, effects.outlineWidth);
        cairo_set_source_rgba(cr,
            ((effects.outlineColor >> 16) & 0xFF) / 255.0,
            ((effects.outlineColor >> 8) & 0xFF) / 255.0,
            (effects.outlineColor & 0xFF) / 255.0,
            ((effects.outlineColor >> 24) & 0xFF) / 255.0);
        cairo_text_path(cr, text.c_str());
        cairo_stroke(cr);
        cairo_restore(cr);
    }
    
    if (effects.gradient) {
        // Create gradient pattern
        CreateGradientPattern(cr, bounds.x, bounds.y, bounds.width, bounds.height);
        cairo_text_path(cr, text.c_str());
        cairo_fill(cr);
    }
    
    if (effects.glow) {
        // Draw glow effect
        for (float radius = 0; radius < effects.glowRadius; radius += 0.5f) {
            cairo_save(cr);
            cairo_set_source_rgba(cr,
                ((effects.glowColor >> 16) & 0xFF) / 255.0,
                ((effects.glowColor >> 8) & 0xFF) / 255.0,
                (effects.glowColor & 0xFF) / 255.0,
                ((effects.glowColor >> 24) & 0xFF) / 255.0 * (1.0f - radius / effects.glowRadius));
            cairo_set_line_width(cr, radius * 2);
            cairo_text_path(cr, text.c_str());
            cairo_stroke(cr);
            cairo_restore(cr);
        }
    }
}

void Label::CreateGradientPattern(cairo_t* cr, double x, double y, double width, double height) {
    double angle = effects.gradientAngle;
    double dx = cos(angle) * width;
    double dy = sin(angle) * height;
    
    cairo_pattern_t* pattern = cairo_pattern_create_linear(
        x, y,
        x + dx, y + dy
    );
    
    cairo_pattern_add_color_stop_rgba(pattern, 0,
        ((effects.gradientStart >> 16) & 0xFF) / 255.0,
        ((effects.gradientStart >> 8) & 0xFF) / 255.0,
        (effects.gradientStart & 0xFF) / 255.0,
        ((effects.gradientStart >> 24) & 0xFF) / 255.0);
        
    cairo_pattern_add_color_stop_rgba(pattern, 1,
        ((effects.gradientEnd >> 16) & 0xFF) / 255.0,
        ((effects.gradientEnd >> 8) & 0xFF) / 255.0,
        (effects.gradientEnd & 0xFF) / 255.0,
        ((effects.gradientEnd >> 24) & 0xFF) / 255.0);
    
    cairo_set_source(cr, pattern);
    cairo_pattern_destroy(pattern);
}

float Label::EaseInOutCubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
}

float Label::Bounce(float t) {
    return sin(t * M_PI * 2) * exp(-t * 3);
}

} // namespace havel 