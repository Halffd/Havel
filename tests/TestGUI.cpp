#include <gtest/gtest.h>
#include "../src/gui/GUI.hpp"
#include "../src/gui/Widget.hpp"
#include "../src/gui/Button.hpp"
#include "../src/gui/Label.hpp"
#include <gtkmm/application.h>

using namespace havel;

class TestGUI : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a test theme
        theme.colors.background = 0x282c34;
        theme.colors.foreground = 0xabb2bf;
        theme.metrics.padding = 8;
        theme.metrics.margin = 4;
        
        // Create a simple window manager (mock)
        windowManager = std::make_shared<WindowManager>();
        
        // Create GUI instance
        gui = std::make_unique<GUI>(windowManager, GUI::Mode::GTK);
        gui->SetTheme(theme);
    }
    
    void TearDown() override {
        gui.reset();
    }
    
    Theme theme;
    std::shared_ptr<WindowManager> windowManager;
    std::unique_ptr<GUI> gui;
};

TEST_F(TestGUI, TestWindowCreation) {
    // Test that the GUI initializes correctly
    ASSERT_NE(gui, nullptr);
    
    // Test that the main window is created
    // Note: This is a simple test - in a real test, we would use a mock or test harness
    SUCCEED();
}

TEST_F(TestGUI, TestWidgetManagement) {
    // Create a test widget
    auto button = std::make_shared<Button>();
    button->SetText("Test Button");
    
    // Add the widget to the GUI
    gui->AddWidget(button);
    
    // Verify the widget was added
    // Note: In a real test, we would verify the widget's state
    SUCCEED();
}

// Main function to run the tests
int main(int argc, char** argv) {
    // Initialize GTK
    auto app = Gtk::Application::create(argc, argv, "com.havel.test");
    
    // Run the tests
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
