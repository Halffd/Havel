#include "Button.hpp"
#include <cmath>

namespace havel {

Button::Button() : icon(nullptr, cairo_surface_destroy) {}

void Button::Draw(cairo_t* cr) {
    if (!isVisible) return;
    
    // Check if we need to redraw
    if (!needsRedraw && !IsAnimating()) return;
    
    // Enable antialiasing
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    
    // Save state
    cairo_save(cr);
    
    // Create clipping region for better performance
    cairo_rectangle(cr, bounds.x - theme.metrics.shadowRadius,
                       bounds.y - theme.metrics.shadowRadius,
                       bounds.width + theme.metrics.shadowRadius * 2,
                       bounds.height + theme.metrics.shadowRadius * 2);
    cairo_clip(cr);
    
    // Draw layers
    if (elevation > 0) {
        DrawShadow(cr);
    }
    DrawBackground(cr);
    DrawIcon(cr);
    DrawText(cr);
    
    // Restore state
    cairo_restore(cr);
    
    needsRedraw = false;
}

void Button::DrawBackground(cairo_t* cr) {
    // Calculate colors based on state
    auto bgColor = theme.colors.buttonNormal;
    if (!isEnabled) {
        bgColor = theme.colors.buttonNormal & 0x7FFFFFFF; // 50% opacity
    } else if (isPressed) {
        bgColor = theme.colors.buttonPress;
    } else if (isHovered) {
        bgColor = theme.colors.buttonHover;
    }
    
    // Set color
    cairo_set_source_rgba(cr,
        ((bgColor >> 16) & 0xFF) / 255.0,
        ((bgColor >> 8) & 0xFF) / 255.0,
        (bgColor & 0xFF) / 255.0,
        ((bgColor >> 24) & 0xFF) / 255.0
    );
    
    // Draw rounded rectangle
    if (roundedCorners) {
        double radius = theme.metrics.cornerRadius;
        double x = bounds.x;
        double y = bounds.y;
        double width = bounds.width;
        double height = bounds.height;
        
        cairo_new_path(cr);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 1.5 * M_PI);
        cairo_arc(cr, x + width - radius, y + radius, radius, 1.5 * M_PI, 2 * M_PI);
        cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, 0.5 * M_PI);
        cairo_arc(cr, x + radius, y + height - radius, radius, 0.5 * M_PI, M_PI);
        cairo_close_path(cr);
    } else {
        cairo_rectangle(cr, bounds.x, bounds.y, bounds.width, bounds.height);
    }
    
    cairo_fill(cr);
}

void Button::DrawText(cairo_t* cr) {
    if (text.empty()) return;
    
    // Set font
    cairo_select_font_face(cr, theme.fonts.regular.c_str(),
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, theme.fonts.dpi / 96.0 * 14);
    
    // Get text extents
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text.c_str(), &extents);
    
    // Calculate position
    double x = bounds.x + (bounds.width - extents.width) / 2;
    double y = bounds.y + (bounds.height + extents.height) / 2;
    
    // Draw text
    cairo_set_source_rgba(cr,
        ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
        ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
        (theme.colors.foreground & 0xFF) / 255.0,
        isEnabled ? 1.0 : 0.5
    );
    
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text.c_str());
}

void Button::HandleEvent(const GUIEvent& event) {
    if (!isEnabled) return;
    
    switch (event.type) {
        case GUIEvent::Type::MOUSE_ENTER:
            isHovered = true;
            if (onHover) onHover(true);
            NeedsRedraw();
            break;
            
        case GUIEvent::Type::MOUSE_LEAVE:
            isHovered = false;
            isPressed = false;
            if (onHover) onHover(false);
            NeedsRedraw();
            break;
            
        case GUIEvent::Type::MOUSE_CLICK:
            if (event.button == 1) { // Left click
                isPressed = true;
                if (onClick) onClick();
                NeedsRedraw();
            }
            break;
            
        case GUIEvent::Type::MOUSE_DRAG:
            if (isPressed && Contains(event.x, event.y)) {
                // Handle dragging
                NeedsRedraw();
            }
            break;
            
        case GUIEvent::Type::KEY_PRESS:
            if (isHovered && event.keycode == KEY_RETURN) {
                if (onClick) onClick();
                NeedsRedraw();
            }
            break;
            
        case GUIEvent::Type::MOUSE_WHEEL:
            if (isHovered) {
                // Handle scroll if needed
                NeedsRedraw();
            }
            break;
            
        default:
            break;
    }
}

bool Button::Contains(int x, int y) const {
    return x >= bounds.x && x < bounds.x + bounds.width &&
           y >= bounds.y && y < bounds.y + bounds.height;
}

void Button::SetIcon(const std::string& iconPath) {
    // Load icon using Cairo
    icon.reset(cairo_image_surface_create_from_png(iconPath.c_str()));
    if (cairo_surface_status(icon.get()) != CAIRO_STATUS_SUCCESS) {
        icon.reset();
    }
    NeedsRedraw();
}

void Button::DrawIcon(cairo_t* cr) {
    if (!icon) return;
    
    int iconWidth = cairo_image_surface_get_width(icon.get());
    int iconHeight = cairo_image_surface_get_height(icon.get());
    
    // Calculate icon position
    double x = bounds.x;
    double y = bounds.y;
    
    switch (iconPosition) {
        case IconPosition::LEFT:
            x += theme.metrics.padding;
            y += (bounds.height - iconHeight) / 2;
            break;
        case IconPosition::RIGHT:
            x += bounds.width - iconWidth - theme.metrics.padding;
            y += (bounds.height - iconHeight) / 2;
            break;
        case IconPosition::TOP:
            x += (bounds.width - iconWidth) / 2;
            y += theme.metrics.padding;
            break;
        case IconPosition::BOTTOM:
            x += (bounds.width - iconWidth) / 2;
            y += bounds.height - iconHeight - theme.metrics.padding;
            break;
        case IconPosition::CENTER:
            x += (bounds.width - iconWidth) / 2;
            y += (bounds.height - iconHeight) / 2;
            break;
    }
    
    cairo_set_source_surface(cr, icon.get(), x, y);
    cairo_paint_with_alpha(cr, isEnabled ? 1.0 : 0.5);
}

void Button::DrawShadow(cairo_t* cr) {
    if (elevation <= 0) return;
    
    // Create gradient for shadow
    double radius = theme.metrics.shadowRadius * elevation;
    double x = bounds.x + bounds.width / 2;
    double y = bounds.y + bounds.height / 2;
    
    cairo_pattern_t* pattern = cairo_pattern_create_radial(
        x, y, 0,
        x, y, radius
    );
    
    // Add color stops for smooth shadow
    cairo_pattern_add_color_stop_rgba(pattern, 0, 0, 0, 0, 0.25 * elevation);
    cairo_pattern_add_color_stop_rgba(pattern, 1, 0, 0, 0, 0);
    
    // Draw shadow
    cairo_save(cr);
    cairo_translate(cr, 0, elevation * 2);
    cairo_set_source(cr, pattern);
    if (roundedCorners) {
        double r = theme.metrics.cornerRadius;
        cairo_arc(cr, x - radius, y - radius, r, M_PI, 1.5 * M_PI);
        cairo_arc(cr, x + radius, y - radius, r, 1.5 * M_PI, 2 * M_PI);
        cairo_arc(cr, x + radius, y + radius, r, 0, 0.5 * M_PI);
        cairo_arc(cr, x - radius, y + radius, r, 0.5 * M_PI, M_PI);
    } else {
        cairo_rectangle(cr, x - radius, y - radius, radius * 2, radius * 2);
    }
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_pattern_destroy(pattern);
}

void Button::UpdateAnimations(float deltaTime) {
    // Update press animation
    if (isPressed && pressAnimation < 1.0f) {
        pressAnimation = std::min(1.0f, pressAnimation + deltaTime * 4.0f);
        NeedsRedraw();
    } else if (!isPressed && pressAnimation > 0.0f) {
        pressAnimation = std::max(0.0f, pressAnimation - deltaTime * 4.0f);
        NeedsRedraw();
    }
    
    // Update hover animation
    if (isHovered && hoverAnimation < 1.0f) {
        hoverAnimation = std::min(1.0f, hoverAnimation + deltaTime * 3.0f);
        NeedsRedraw();
    } else if (!isHovered && hoverAnimation > 0.0f) {
        hoverAnimation = std::max(0.0f, hoverAnimation - deltaTime * 3.0f);
        NeedsRedraw();
    }
    
    // Apply animations to visual properties
    if (hoverEffect) {
        elevation = 2.0f + hoverAnimation * 2.0f;
    }
}

} // namespace havel 