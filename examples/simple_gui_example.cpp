#include "../src/gui/GUI.hpp"
#include "../src/gui/Button.hpp"
#include "../src/gui/Label.hpp"
#include "../src/window/WindowManager.hpp"
#include <gtkmm/application.h>
#include <iostream>

using namespace havel;

int main(int argc, char** argv) {
    try {
        // Create a GTK application
        auto app = Gtk::Application::create(argc, argv, "com.havel.example");
        
        // Create a window manager instance
        auto windowManager = std::make_shared<WindowManager>();
        
        // Create the main GUI
        GUI gui(windowManager, GUI::Mode::GTK);
        
        // Set a simple theme
        Theme theme;
        theme.colors.background = 0x282c34;
        theme.colors.foreground = 0xabb2bf;
        theme.colors.accent = 0x61afef;
        gui.SetTheme(theme);
        
        // Create a label
        auto label = std::make_shared<Label>();
        label->SetText("Hello, Havel GUI!");
        label->SetBounds(20, 20, 200, 30);
        
        // Create a button
        auto button = std::make_shared<Button>();
        button->SetText("Click Me!");
        button->SetBounds(20, 60, 120, 30);
        button->SetOnClick([]() {
            std::cout << "Button clicked!" << std::endl;
        });
        
        // Add widgets to the GUI
        gui.AddWidget(label);
        gui.AddWidget(button);
        
        // Show the GUI
        gui.ShowWindow();
        
        // Run the application
        return app->run(*dynamic_cast<Gtk::Window*>(gui.GetNativeWindow()));
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
