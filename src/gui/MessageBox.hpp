#pragma once

#include "Widget.hpp"
#include <cairo/cairo.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace havel {

class Button;

class MessageBox : public Widget {
public:
    enum class Type {
        INFO,
        WARNING,
        ERROR,
        QUESTION
    };
    
    enum class Buttons {
        OK,
        OK_CANCEL,
        YES_NO,
        YES_NO_CANCEL
    };
    
    enum class Result {
        OK,
        CANCEL,
        YES,
        NO
    };
    
    using ResultCallback = std::function<void(Result)>;
    
    MessageBox(Type type = Type::INFO, Buttons buttons = Buttons::OK);
    ~MessageBox() override = default;
    
    // Widget overrides
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    void SetBounds(int x, int y, int width, int height) override;
    bool Contains(int x, int y) const override;
    
    // Message box specific methods
    void SetTitle(const std::string& title);
    void SetMessage(const std::string& message);
    void SetIcon(const std::string& iconPath);
    
    // Static method to show a message box
    static void Show(const std::string& title, const std::string& message,
                    Type type = Type::INFO, Buttons buttons = Buttons::OK,
                    ResultCallback callback = nullptr);

private:
    void DrawBackground(cairo_t* cr);
    void DrawTitleBar(cairo_t* cr);
    void DrawContent(cairo_t* cr);
    void DrawButtons(cairo_t* cr);
    void CreateButtons();
    void WrapText(cairo_t* cr, const std::string& text, double maxWidth, std::vector<std::string>& lines);
    
    Type type;
    Buttons buttons;
    std::string title;
    std::string message;
    std::vector<std::unique_ptr<Button>> buttonWidgets;
    std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> icon;
    
    // Theme colors for message box
    struct {
        uint32_t background{0x282c34};
        uint32_t titleBar{0x21252b};
        uint32_t titleText{0xffffff};
        uint32_t border{0x181a1f};
        uint32_t shadow{0x0000007f};
    } colors;
    
    // Layout metrics
    struct {
        int padding{16};
        int titleBarHeight{32};
        int buttonHeight{32};
        int buttonSpacing{8};
        int buttonMargin{16};
        int cornerRadius{4};
    } metrics;
};

} // namespace havel
