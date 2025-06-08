#pragma once

#include "Widget.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>

// Forward declarations
namespace havel {
class Button;
class Screen;
class InputBox;
class Dropdown;
class MessageBox;
}

#include "Button.hpp"  // Include after forward declarations

namespace havel {

class Screen : public Widget {
public:
    Screen();
    ~Screen() override = default;
    
    // Screen management
    void CoverAllMonitors();
    void CoverMonitor(int monitorIndex);
    void SetBackgroundColor(uint32_t color);
    void SetBackgroundOpacity(float opacity);
    
    // Input handling
    void BlockInput(bool block);
    void AllowClickThrough(bool allow);
    
    // Widget interface
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    bool Contains(int x, int y) const override;

private:
    struct Monitor {
        int x, y;
        int width, height;
        int index;
    };
    
    std::vector<Monitor> monitors;
    uint32_t backgroundColor{0x000000FF};
    float backgroundOpacity{1.0f};
    bool blockInput{true};
    bool clickThrough{false};
    
    void UpdateMonitorInfo();
};

// Input box component
class InputBox : public Widget {
public:
    InputBox();
    ~InputBox() override = default;
    
    // Text properties
    void SetText(const std::string& text);
    const std::string& GetText() const { return text; }
    void SetPlaceholder(const std::string& text);
    void SetMaxLength(size_t length);
    
    // Styling
    void SetPasswordMode(bool enabled);
    void SetReadOnly(bool readonly);
    void SetSelectOnFocus(bool select);
    
    // Callbacks
    using TextCallback = std::function<void(const std::string&)>;
    void SetOnTextChanged(TextCallback callback);
    void SetOnSubmit(TextCallback callback);
    
    // Widget interface
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    bool Contains(int x, int y) const override;

private:
    std::string text;
    std::string placeholder;
    size_t maxLength{0};
    bool isPassword{false};
    bool readOnly{false};
    bool selectOnFocus{true};
    bool isFocused{false};
    size_t cursorPos{0};
    size_t selectionStart{0};
    size_t selectionEnd{0};
    
    TextCallback onTextChanged;
    TextCallback onSubmit;
    
    void UpdateCursor(int x);
    void DeleteSelection();
    void InsertText(const std::string& str);
};

// Dropdown component
class Dropdown : public Widget {
public:
    Dropdown();
    ~Dropdown() override = default;
    
    // Items
    void AddItem(const std::string& text, const std::string& value = "");
    void RemoveItem(size_t index);
    void ClearItems();
    void SetSelectedIndex(size_t index);
    size_t GetSelectedIndex() const { return selectedIndex; }
    
    // Styling
    void SetMaxVisibleItems(size_t count);
    void SetItemHeight(int height);
    void SetDropdownWidth(int width);
    
    // Callbacks
    using SelectionCallback = std::function<void(size_t, const std::string&)>;
    void SetOnSelectionChanged(SelectionCallback callback);
    
    // Widget interface
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    bool Contains(int x, int y) const override;

private:
    struct Item {
        std::string text;
        std::string value;
    };
    
    std::vector<Item> items;
    size_t selectedIndex{0};
    size_t maxVisibleItems{8};
    int itemHeight{24};
    int dropdownWidth{0};
    bool isOpen{false};
    
    SelectionCallback onSelectionChanged;
    
    void ToggleDropdown();
    void CloseDropdown();
    size_t GetItemAtPosition(int y) const;
};

// Message box component
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
    
    MessageBox(Type type = Type::INFO, Buttons buttons = Buttons::OK);
    ~MessageBox() override = default;
    
    // Content
    void SetTitle(const std::string& title);
    void SetMessage(const std::string& message);
    void SetIcon(const std::string& iconPath);
    
    // Callbacks
    using ResultCallback = std::function<void(int)>;
    void SetOnResult(ResultCallback callback);
    
    // Static helpers
    static void Show(const std::string& title, const std::string& message,
                    Type type = Type::INFO, Buttons buttons = Buttons::OK,
                    ResultCallback callback = nullptr);
    
    // Widget interface
    void Draw(cairo_t* cr) override;
    void HandleEvent(const GUIEvent& event) override;
    bool Contains(int x, int y) const override;

private:
    Type type;
    Buttons buttons;
    std::string title;
    std::string message;
    std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> icon;
    ResultCallback onResult;
    
    std::vector<std::unique_ptr<Button>> buttonWidgets;
    void CreateButtons();
};

// Dialog builder for combining elements
class Dialog {
public:
    Dialog();
    ~Dialog() = default;
    
    // Building methods
    Dialog& WithTitle(const std::string& title);
    Dialog& WithMessage(const std::string& message);
    Dialog& WithInput(const std::string& placeholder = "");
    Dialog& WithDropdown(const std::vector<std::string>& items);
    Dialog& WithButtons(MessageBox::Buttons buttons);
    Dialog& WithIcon(const std::string& iconPath);
    Dialog& WithTheme(const Theme& theme);
    
    // Result handling
    struct DialogResult {
        int buttonPressed{0};
        std::string inputText;
        size_t dropdownSelection{0};
    };
    
    // Show dialog
    void Show(std::function<void(const DialogResult&)> callback = nullptr);

private:
    std::string title;
    std::string message;
    std::string inputPlaceholder;
    std::vector<std::string> dropdownItems;
    MessageBox::Buttons buttons{MessageBox::Buttons::OK};
    std::string iconPath;
    Theme theme;
    
    std::unique_ptr<Screen> screen;
    std::unique_ptr<MessageBox> messageBox;
    std::unique_ptr<InputBox> inputBox;
    std::unique_ptr<Dropdown> dropdown;
    
    void CreateWidgets();
    void LayoutWidgets();
};

} // namespace havel 