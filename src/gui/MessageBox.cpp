#include "x11_includes.h"
#include "Screen.hpp"
#include <cmath>

namespace havel {

MessageBox::MessageBox(Type type, Buttons buttons)
    : type(type)
    , buttons(buttons)
    , icon(nullptr, cairo_surface_destroy) {
    
    // Set default size
    bounds.width = 400;
    bounds.height = 200;
    
    // Create buttons
    CreateButtons();
}

void MessageBox::SetTitle(const std::string& newTitle) {
    if (title == newTitle) return;
    title = newTitle;
    NeedsRedraw();
}

void MessageBox::SetMessage(const std::string& newMessage) {
    if (message == newMessage) return;
    message = newMessage;
    NeedsRedraw();
}

void MessageBox::SetIcon(const std::string& iconPath) {
    icon.reset(cairo_image_surface_create_from_png(iconPath.c_str()));
    if (cairo_surface_status(icon.get()) != CAIRO_STATUS_SUCCESS) {
        icon.reset();
    }
    NeedsRedraw();
}

void MessageBox::Draw(cairo_t* cr) {
    if (!isVisible) return;
    
    cairo_save(cr);
    
    // Draw background with shadow
    DrawBackground(cr);
    
    // Draw title bar
    DrawTitleBar(cr);
    
    // Draw content
    DrawContent(cr);
    
    // Draw buttons
    DrawButtons(cr);
    
    cairo_restore(cr);
}

void MessageBox::DrawBackground(cairo_t* cr) {
    // Draw shadow
    double shadowRadius = theme.metrics.shadowRadius;
    double x = bounds.x;
    double y = bounds.y;
    double width = bounds.width;
    double height = bounds.height;
    
    cairo_pattern_t* pattern = cairo_pattern_create_radial(
        x + width/2, y + height/2, 0,
        x + width/2, y + height/2, shadowRadius
    );
    
    cairo_pattern_add_color_stop_rgba(pattern, 0, 0, 0, 0, 0.3);
    cairo_pattern_add_color_stop_rgba(pattern, 1, 0, 0, 0, 0);
    
    cairo_set_source(cr, pattern);
    cairo_rectangle(cr, x - shadowRadius, y - shadowRadius,
                       width + shadowRadius*2, height + shadowRadius*2);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
    
    // Draw background
    cairo_set_source_rgba(cr,
        ((theme.colors.background >> 16) & 0xFF) / 255.0,
        ((theme.colors.background >> 8) & 0xFF) / 255.0,
        (theme.colors.background & 0xFF) / 255.0,
        ((theme.colors.background >> 24) & 0xFF) / 255.0);
    
    if (theme.metrics.cornerRadius > 0) {
        double radius = theme.metrics.cornerRadius;
        cairo_new_path(cr);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 1.5 * M_PI);
        cairo_arc(cr, x + width - radius, y + radius, radius, 1.5 * M_PI, 2 * M_PI);
        cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, 0.5 * M_PI);
        cairo_arc(cr, x + radius, y + height - radius, radius, 0.5 * M_PI, M_PI);
        cairo_close_path(cr);
    } else {
        cairo_rectangle(cr, x, y, width, height);
    }
    
    cairo_fill(cr);
}

void MessageBox::DrawTitleBar(cairo_t* cr) {
    double x = bounds.x;
    double y = bounds.y;
    double width = bounds.width;
    double titleHeight = 40;
    
    // Draw title background
    cairo_set_source_rgba(cr,
        ((theme.colors.highlight >> 16) & 0xFF) / 255.0,
        ((theme.colors.highlight >> 8) & 0xFF) / 255.0,
        (theme.colors.highlight & 0xFF) / 255.0,
        ((theme.colors.highlight >> 24) & 0xFF) / 255.0);
    
    if (theme.metrics.cornerRadius > 0) {
        double radius = theme.metrics.cornerRadius;
        cairo_new_path(cr);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 1.5 * M_PI);
        cairo_arc(cr, x + width - radius, y + radius, radius, 1.5 * M_PI, 2 * M_PI);
        cairo_line_to(cr, x + width, y + titleHeight);
        cairo_line_to(cr, x, y + titleHeight);
        cairo_close_path(cr);
    } else {
        cairo_rectangle(cr, x, y, width, titleHeight);
    }
    cairo_fill(cr);
    
    // Draw title text
    cairo_select_font_face(cr, theme.fonts.title.c_str(),
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, theme.fonts.dpi / 96.0 * 16);
    
    cairo_text_extents_t extents;
    cairo_text_extents(cr, title.c_str(), &extents);
    
    cairo_set_source_rgba(cr,
        ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
        ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
        (theme.colors.foreground & 0xFF) / 255.0,
        ((theme.colors.foreground >> 24) & 0xFF) / 255.0);
    
    cairo_move_to(cr,
        x + theme.metrics.padding,
        y + (titleHeight + extents.height) / 2);
    cairo_show_text(cr, title.c_str());
}

void MessageBox::DrawContent(cairo_t* cr) {
    double x = bounds.x + theme.metrics.padding;
    double y = bounds.y + 40 + theme.metrics.padding;
    double width = bounds.width - theme.metrics.padding * 2;
    
    // Draw icon if present
    if (icon) {
        int iconWidth = cairo_image_surface_get_width(icon.get());
        int iconHeight = cairo_image_surface_get_height(icon.get());
        cairo_set_source_surface(cr, icon.get(), x, y);
        cairo_paint(cr);
        x += iconWidth + theme.metrics.padding;
        width -= iconWidth + theme.metrics.padding;
    }
    
    // Draw message text
    cairo_select_font_face(cr, theme.fonts.regular.c_str(),
        CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, theme.fonts.dpi / 96.0 * 14);
    
    cairo_set_source_rgba(cr,
        ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
        ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
        (theme.colors.foreground & 0xFF) / 255.0,
        ((theme.colors.foreground >> 24) & 0xFF) / 255.0);
    
    // Word wrap message
    std::vector<std::string> lines;
    WrapText(cr, message, width, lines);
    
    double lineHeight = theme.fonts.dpi / 96.0 * 20;
    for (const auto& line : lines) {
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, line.c_str());
        y += lineHeight;
    }
}

void MessageBox::DrawButtons(cairo_t* cr) {
    // Each button draws itself
    for (auto& button : buttonWidgets) {
        button->Draw(cr);
    }
}

void MessageBox::HandleEvent(const GUIEvent& event) {
    if (!isEnabled) return;
    
    // Forward events to buttons
    for (auto& button : buttonWidgets) {
        button->HandleEvent(event);
    }
    
    // Handle escape key
    if (event.type == GUIEvent::Type::KEY_PRESS && 
        event.keycode == XK_Escape) {
        if (onResult) {
            onResult(0); // Cancel/No result
        }
    }
}

void MessageBox::CreateButtons() {
    buttonWidgets.clear();
    
    int buttonWidth = 100;
    int buttonHeight = 30;
    int spacing = theme.metrics.padding;
    int y = bounds.y + bounds.height - buttonHeight - spacing;
    
    switch (buttons) {
        case Buttons::OK: {
            auto okButton = std::make_unique<Button>();
            okButton->SetText("OK");
            okButton->SetBounds(
                bounds.x + bounds.width - buttonWidth - spacing,
                y, buttonWidth, buttonHeight);
            okButton->SetCallback([this]() {
                if (onResult) onResult(1);
            });
            buttonWidgets.push_back(std::move(okButton));
            break;
        }
        
        case Buttons::OK_CANCEL: {
            auto cancelButton = std::make_unique<Button>();
            cancelButton->SetText("Cancel");
            cancelButton->SetBounds(
                bounds.x + bounds.width - buttonWidth * 2 - spacing * 2,
                y, buttonWidth, buttonHeight);
            cancelButton->SetCallback([this]() {
                if (onResult) onResult(0);
            });
            
            auto okButton = std::make_unique<Button>();
            okButton->SetText("OK");
            okButton->SetBounds(
                bounds.x + bounds.width - buttonWidth - spacing,
                y, buttonWidth, buttonHeight);
            okButton->SetCallback([this]() {
                if (onResult) onResult(1);
            });
            
            buttonWidgets.push_back(std::move(cancelButton));
            buttonWidgets.push_back(std::move(okButton));
            break;
        }
        
        // Add other button configurations...
    }
}

void MessageBox::WrapText(cairo_t* cr, const std::string& text, double maxWidth,
                         std::vector<std::string>& lines) {
    std::istringstream stream(text);
    std::string word;
    std::string currentLine;
    double lineWidth = 0;
    
    while (stream >> word) {
        cairo_text_extents_t extents;
        cairo_text_extents(cr, (currentLine + " " + word).c_str(), &extents);
        
        if (lineWidth + extents.width <= maxWidth) {
            if (!currentLine.empty()) currentLine += " ";
            currentLine += word;
            lineWidth = extents.width;
        } else {
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
            }
            currentLine = word;
            cairo_text_extents(cr, word.c_str(), &extents);
            lineWidth = extents.width;
        }
    }
    
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
}

void MessageBox::Show(const std::string& title, const std::string& message,
                     Type type, Buttons buttons, ResultCallback callback) {
    auto screen = std::make_shared<Screen>();
    screen->CoverAllMonitors();
    screen->SetBackgroundColor(0x00000080);  // Semi-transparent black
    
    auto msgBox = std::make_shared<MessageBox>(type, buttons);
    msgBox->SetTitle(title);
    msgBox->SetMessage(message);
    msgBox->SetOnResult([callback, screen](int result) {
        if (callback) callback(result);
        screen->Hide();
    });
    
    // Center the message box
    int screenWidth = screen->GetWidth();
    int screenHeight = screen->GetHeight();
    msgBox->SetBounds(
        (screenWidth - msgBox->GetWidth()) / 2,
        (screenHeight - msgBox->GetHeight()) / 2,
        msgBox->GetWidth(),
        msgBox->GetHeight()
    );
    
    screen->Show();
}

bool MessageBox::Contains(int x, int y) const {
    return x >= bounds.x && x < bounds.x + bounds.width &&
           y >= bounds.y && y < bounds.y + bounds.height;
}

} // namespace havel 