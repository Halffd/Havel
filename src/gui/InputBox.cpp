#define _USE_MATH_DEFINES  // For M_PI on Linux
#include <cmath>           // For M_PI and other math constants
#include "x11_includes.h"

// Define M_PI if not already defined by cmath
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "Screen.hpp"
#include <X11/keysym.h>
#include <cstring>
#include <limits>  // For std::numeric_limits
#include <algorithm>

namespace havel {

InputBox::InputBox() {
    // Set default size
    bounds.width = 200;
    bounds.height = 30;
}

void InputBox::SetText(const std::string& newText) {
    if (text == newText) return;
    
    text = newText;
    cursorPos = text.length();
    selectionStart = selectionEnd = cursorPos;
    
    if (onTextChanged) {
        onTextChanged(text);
    }
    
    NeedsRedraw();
}

void InputBox::SetPlaceholder(const std::string& text) {
    if (placeholder == text) return;
    placeholder = text;
    NeedsRedraw();
}

void InputBox::SetMaxLength(size_t length) {
    maxLength = length;
    if (maxLength > 0 && text.length() > maxLength) {
        text = text.substr(0, maxLength);
        cursorPos = std::min(cursorPos, maxLength);
        selectionStart = std::min(selectionStart, maxLength);
        selectionEnd = std::min(selectionEnd, maxLength);
        NeedsRedraw();
    }
}

void InputBox::Draw(cairo_t* cr) {
    if (!isVisible) return;
    
    cairo_save(cr);
    
    // Draw background
    cairo_set_source_rgba(cr,
        ((theme.colors.inputBackground >> 16) & 0xFF) / 255.0,
        ((theme.colors.inputBackground >> 8) & 0xFF) / 255.0,
        (theme.colors.inputBackground & 0xFF) / 255.0,
        ((theme.colors.inputBackground >> 24) & 0xFF) / 255.0);
    
    if (theme.metrics.cornerRadius > 0) {
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
    
    // Draw border if focused
    if (isFocused) {
        cairo_set_source_rgba(cr,
            ((theme.colors.accent >> 16) & 0xFF) / 255.0,
            ((theme.colors.accent >> 8) & 0xFF) / 255.0,
            (theme.colors.accent & 0xFF) / 255.0,
            ((theme.colors.accent >> 24) & 0xFF) / 255.0);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);
    }
    
    // Set up font
    cairo_select_font_face(cr, theme.fonts.regular.c_str(),
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, theme.fonts.dpi / 96.0 * 14);
    
    // Calculate text position
    cairo_text_extents_t extents;
    std::string displayText = isPassword ? std::string(text.length(), '*') : text;
    cairo_text_extents(cr, displayText.c_str(), &extents);
    
    double textX = bounds.x + theme.metrics.padding;
    double textY = bounds.y + (bounds.height + extents.height) / 2;
    
    // Draw selection if any
    if (isFocused && selectionStart != selectionEnd) {
        std::string beforeSelection = displayText.substr(0, selectionStart);
        std::string selection = displayText.substr(selectionStart, selectionEnd - selectionStart);
        
        cairo_text_extents_t beforeExtents, selectionExtents;
        cairo_text_extents(cr, beforeSelection.c_str(), &beforeExtents);
        cairo_text_extents(cr, selection.c_str(), &selectionExtents);
        
        cairo_set_source_rgba(cr,
            ((theme.colors.selection >> 16) & 0xFF) / 255.0,
            ((theme.colors.selection >> 8) & 0xFF) / 255.0,
            (theme.colors.selection & 0xFF) / 255.0,
            ((theme.colors.selection >> 24) & 0xFF) / 255.0);
            
        cairo_rectangle(cr,
            textX + beforeExtents.width,
            bounds.y + 2,
            selectionExtents.width,
            bounds.height - 4);
        cairo_fill(cr);
    }
    
    // Draw text
    if (!text.empty()) {
        cairo_set_source_rgba(cr,
            ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
            ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
            (theme.colors.foreground & 0xFF) / 255.0,
            ((theme.colors.foreground >> 24) & 0xFF) / 255.0);
    } else if (!placeholder.empty()) {
        // Draw placeholder text
        cairo_set_source_rgba(cr,
            ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
            ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
            (theme.colors.foreground & 0xFF) / 255.0,
            0.5); // 50% opacity for placeholder
        displayText = placeholder;
    }
    
    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, displayText.c_str());
    
    // Draw cursor if focused
    if (isFocused) {
        std::string beforeCursor = displayText.substr(0, cursorPos);
        cairo_text_extents_t cursorExtents;
        cairo_text_extents(cr, beforeCursor.c_str(), &cursorExtents);
        
        cairo_set_source_rgba(cr,
            ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
            ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
            (theme.colors.foreground & 0xFF) / 255.0,
            0.8); // 80% opacity for cursor
            
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, textX + cursorExtents.width, bounds.y + 4);
        cairo_line_to(cr, textX + cursorExtents.width, bounds.y + bounds.height - 4);
        cairo_stroke(cr);
    }
    
    cairo_restore(cr);
}

void InputBox::HandleEvent(const GUIEvent& event) {
    if (!isEnabled) return;
    
    switch (event.type) {
        case GUIEvent::Type::MOUSE_CLICK:
            if (Contains(event.x, event.y)) {
                isFocused = true;
                if (selectOnFocus) {
                    selectionStart = 0;
                    selectionEnd = text.length();
                }
                UpdateCursor(event.x);
                NeedsRedraw();
            } else {
                isFocused = false;
                NeedsRedraw();
            }
            break;
            
        case GUIEvent::Type::KEY_PRESS:
            if (!isFocused) break;
            
            switch (event.keycode) {
                case XK_Left:
                    if (cursorPos > 0) {
                        cursorPos--;
                        if (!(event.modifiers & ShiftMask)) {
                            selectionStart = selectionEnd = cursorPos;
                        }
                        NeedsRedraw();
                    }
                    break;
                    
                case XK_Right:
                    if (cursorPos < text.length()) {
                        cursorPos++;
                        if (!(event.modifiers & ShiftMask)) {
                            selectionStart = selectionEnd = cursorPos;
                        }
                        NeedsRedraw();
                    }
                    break;
                    
                case XK_BackSpace:
                    if (selectionStart != selectionEnd) {
                        DeleteSelection();
                    } else if (cursorPos > 0) {
                        text.erase(cursorPos - 1, 1);
                        cursorPos--;
                        selectionStart = selectionEnd = cursorPos;
                        if (onTextChanged) onTextChanged(text);
                        NeedsRedraw();
                    }
                    break;
                    
                case XK_Delete:
                    if (selectionStart != selectionEnd) {
                        DeleteSelection();
                    } else if (cursorPos < text.length()) {
                        text.erase(cursorPos, 1);
                        if (onTextChanged) onTextChanged(text);
                        NeedsRedraw();
                    }
                    break;
                    
                case XK_Return:
                case XK_KP_Enter:
                    if (onSubmit) onSubmit(text);
                    break;
                    
                default:
                    if (event.keycode >= XK_space && event.keycode <= XK_asciitilde) {
                        if (readOnly) break;
                        if (maxLength > 0 && text.length() >= maxLength) break;
                        
                        char c = static_cast<char>(event.keycode);
                        if (event.modifiers & ShiftMask) {
                            c = toupper(c);
                        }
                        
                        if (selectionStart != selectionEnd) {
                            DeleteSelection();
                        }
                        
                        text.insert(cursorPos, 1, c);
                        cursorPos++;
                        selectionStart = selectionEnd = cursorPos;
                        
                        if (onTextChanged) onTextChanged(text);
                        NeedsRedraw();
                    }
                    break;
            }
            break;
            
        default:
            break;
    }
}

void InputBox::UpdateCursor(int x) {
    // Calculate cursor position from x coordinate
    x -= bounds.x + theme.metrics.padding;
    
    cairo_t* cr = cairo_create(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1));
    cairo_select_font_face(cr, theme.fonts.regular.c_str(),
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, theme.fonts.dpi / 96.0 * 14);
    
    std::string displayText = isPassword ? std::string(text.length(), '*') : text;
    size_t bestPos = 0;
    double bestDist = std::numeric_limits<double>::max();
    
    for (size_t i = 0; i <= displayText.length(); i++) {
        cairo_text_extents_t extents;
        cairo_text_extents(cr, displayText.substr(0, i).c_str(), &extents);
        
        double dist = std::abs(x - extents.width);
        if (dist < bestDist) {
            bestDist = dist;
            bestPos = i;
        }
    }
    
    cursorPos = bestPos;
    cairo_destroy(cr);
}

void InputBox::DeleteSelection() {
    if (readOnly) return;
    
    size_t start = std::min(selectionStart, selectionEnd);
    size_t end = std::max(selectionStart, selectionEnd);
    
    text.erase(start, end - start);
    cursorPos = start;
    selectionStart = selectionEnd = start;
    
    if (onTextChanged) onTextChanged(text);
    NeedsRedraw();
}

bool InputBox::Contains(int x, int y) const {
    return x >= bounds.x && x < bounds.x + bounds.width &&
           y >= bounds.y && y < bounds.y + bounds.height;
}

} // namespace havel