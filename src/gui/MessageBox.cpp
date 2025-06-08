#include "MessageBox.hpp"
#include "Button.hpp"
#include "Theme.hpp"
#include "x11_includes.h"
#include <cmath>
#include <sstream>
#include <cstring>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>

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

void MessageBox::SetBounds(int x, int y, int width, int height) {
    bounds.x = x;
    bounds.y = y;
    bounds.width = width;
    bounds.height = height;
    
    // Update button positions
    UpdateButtonPositions();
    NeedsRedraw();
}

void MessageBox::UpdateButtonPositions() {
    if (buttonWidgets.empty()) return;
    
    const int buttonWidth = 80;
    const int buttonSpacing = theme.metrics.buttonSpacing;
    const int buttonMargin = theme.metrics.buttonMargin;
    const int buttonHeight = theme.metrics.buttonHeight;
    
    int totalButtonsWidth = (buttonWidth + buttonSpacing) * buttonWidgets.size() - buttonSpacing;
    int startX = bounds.x + (bounds.width - totalButtonsWidth) / 2;
    int buttonY = bounds.y + bounds.height - buttonHeight - buttonMargin;
    
    for (auto& button : buttonWidgets) {
        if (button) {
            button->SetBounds(startX, buttonY, buttonWidth, buttonHeight);
            startX += buttonWidth + buttonSpacing;
        }
    }
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
    
    cairo_save(cr);
    
    // Draw title bar background
    cairo_set_source_rgba(cr, 
        ((colors.titleBar >> 16) & 0xFF) / 255.0,
        ((colors.titleBar >> 8) & 0xFF) / 255.0,
        (colors.titleBar & 0xFF) / 255.0,
        1.0);
    
    cairo_rectangle(cr, bounds.x, bounds.y, bounds.width, metrics.titleBarHeight);
    cairo_fill(cr);
    
    // Draw title text
    if (!title.empty()) {
        cairo_set_source_rgba(cr, 
            ((colors.titleText >> 16) & 0xFF) / 255.0,
            ((colors.titleText >> 8) & 0xFF) / 255.0,
            (colors.titleText & 0xFF) / 255.0,
            1.0);
            
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);
        
        cairo_text_extents_t extents;
        cairo_text_extents(cr, title.c_str(), &extents);
        
        double x = bounds.x + metrics.padding;
        double y = bounds.y + metrics.titleBarHeight / 2 + extents.height / 2;
        
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, title.c_str());
    }
    
    cairo_restore(cr);
}

void MessageBox::DrawContent(cairo_t* cr) {
    if (!isVisible || message.empty()) return;
    
    cairo_save(cr);
    
    // Set up text properties
    cairo_set_source_rgba(cr, 
        ((theme.colors.foreground >> 16) & 0xFF) / 255.0,
        ((theme.colors.foreground >> 8) & 0xFF) / 255.0,
        (theme.colors.foreground & 0xFF) / 255.0,
        1.0);
    
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    
    // Calculate content area
    double contentX = bounds.x + metrics.padding;
    double contentY = bounds.y + metrics.titleBarHeight + metrics.padding;
    double contentWidth = bounds.width - 2 * metrics.padding;
    double contentHeight = bounds.height - metrics.titleBarHeight - metrics.buttonHeight - 3 * metrics.padding;
    
    // Wrap text
    std::vector<std::string> lines;
    WrapText(cr, message, contentWidth, lines);
    
    // Draw wrapped text
    cairo_text_extents_t extents;
    cairo_text_extents(cr, "Hg", &extents);
    double lineHeight = extents.height * 1.2;
    
    double y = contentY + extents.height;
    for (const auto& line : lines) {
        if (y > contentY + contentHeight) break;
        cairo_move_to(cr, contentX, y);
        cairo_show_text(cr, line.c_str());
        y += lineHeight;
    }
    
    cairo_restore(cr);
}

void MessageBox::DrawButtons(cairo_t* cr) {
    if (!isVisible || buttonWidgets.empty()) return;
    
    cairo_save(cr);
    
    // Draw each button
    for (auto& button : buttonWidgets) {
        if (button) {
            button->Draw(cr);
        }
    }
    
    cairo_restore(cr);
}

void MessageBox::HandleEvent(const GUIEvent& event) {
    if (!isVisible || !isEnabled) return;
    
    // Handle mouse events
    if (event.type == GUIEvent::Type::MOUSE_BUTTON_PRESS ||
        event.type == GUIEvent::Type::MOUSE_BUTTON_RELEASE) {
        
        // Check if click is inside our bounds
        if (Contains(event.mouseX, event.mouseY)) {
            // Forward event to buttons
            for (auto& button : buttonWidgets) {
                if (button) {
                    button->HandleEvent(event);
                }
            }
        }
    }
    // Handle mouse move for hover effects
    else if (event.type == GUIEvent::Type::MOUSE_MOVE) {
        for (auto& button : buttonWidgets) {
            if (button) {
                button->HandleEvent(event);
            }
        }
    }
    // Handle escape key
    else if (event.type == GUIEvent::Type::KEY_PRESS && 
        event.keycode == XK_Escape) {
        if (onResult) {
            onResult(0); // Cancel/No result
        }
    }
}

void MessageBox::CreateButtons() {
    buttonWidgets.clear();
    
    const int buttonWidth = 80;
    const int buttonHeight = 32;
    const int spacing = 8;
    const int y = bounds.y + bounds.height - buttonHeight - 16;
    
    switch (buttons) {
        case Buttons::OK: {
            auto okButton = std::make_unique<Button>();
            okButton->SetText("OK");
            okButton->SetBounds(
                bounds.x + (bounds.width - buttonWidth) / 2,
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
        
        case Buttons::YES_NO: {
            auto noButton = std::make_unique<Button>();
            noButton->SetText("No");
            noButton->SetBounds(
                bounds.x + bounds.width - buttonWidth * 2 - spacing * 2,
                y, buttonWidth, buttonHeight);
            noButton->SetCallback([this]() {
                if (onResult) onResult(0);
            });
            
            auto yesButton = std::make_unique<Button>();
            yesButton->SetText("Yes");
            yesButton->SetBounds(
                bounds.x + bounds.width - buttonWidth - spacing,
                y, buttonWidth, buttonHeight);
            yesButton->SetCallback([this]() {
                if (onResult) onResult(1);
            });
            
            buttonWidgets.push_back(std::move(noButton));
            buttonWidgets.push_back(std::move(yesButton));
            break;
        }
        
        case Buttons::YES_NO_CANCEL: {
            auto cancelButton = std::make_unique<Button>();
            cancelButton->SetText("Cancel");
            cancelButton->SetBounds(
                bounds.x + bounds.width - buttonWidth * 3 - spacing * 3,
                y, buttonWidth, buttonHeight);
            cancelButton->SetCallback([this]() {
                if (onResult) onResult(0);
            });
            
            auto noButton = std::make_unique<Button>();
            noButton->SetText("No");
            noButton->SetBounds(
                bounds.x + bounds.width - buttonWidth * 2 - spacing * 2,
                y, buttonWidth, buttonHeight);
            noButton->SetCallback([this]() {
                if (onResult) onResult(2);
            });
            
            auto yesButton = std::make_unique<Button>();
            yesButton->SetText("Yes");
            yesButton->SetBounds(
                bounds.x + bounds.width - buttonWidth - spacing,
                y, buttonWidth, buttonHeight);
            yesButton->SetCallback([this]() {
                if (onResult) onResult(1);
            });
            
            buttonWidgets.push_back(std::move(cancelButton));
            buttonWidgets.push_back(std::move(noButton));
            buttonWidgets.push_back(std::move(yesButton));
            break;
        }
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

bool MessageBox::Contains(int x, int y) {
    return (x >= bounds.x && x < bounds.x + bounds.width &&
            y >= bounds.y && y < bounds.y + bounds.height);
}

void MessageBox::WrapText(cairo_t* cr, const std::string& text, double maxWidth, std::vector<std::string>& lines) {
    if (text.empty()) return;
    
    std::istringstream iss(text);
    std::string word;
    std::string currentLine;
    
    while (iss >> word) {
        std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
        cairo_text_extents_t extents;
        cairo_text_extents(cr, testLine.c_str(), &extents);
        
        if (extents.width <= maxWidth) {
            currentLine = testLine;
        } else {
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
            }
            currentLine = word;
        }
    }
    
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
}

} // namespace havel