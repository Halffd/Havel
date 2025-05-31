#include "x11_includes.h"
#include "system_includes.h"
#include "window/Window.hpp"
#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/DisplayManager.hpp"
#include "window/WindowManager.hpp"
#include "logger.h"
#include <iostream>

namespace havel {
    extern Logger lo;  // Forward declaration of the logger object
}

void testSend(havel::IO& io) {
    std::cout << "Testing Send function...\n";
    io.Send("Hello");
    io.Send("{Shift down}A{Shift up}"); // Test sending 'A' with Shift
    io.Send("{Ctrl down}C{Ctrl up}"); // Test sending Ctrl+C
}

void testControlSend(havel::IO& io) {
    std::cout << "Testing ControlSend function...\n";
    // io.ControlSend("Notepad", "Hello");
    // io.ControlSend("Calculator", "1+2=");
}

void testRegisterHotkey(havel::IO& io) {
    std::cout << "Testing RegisterHotkey function...\n";
    
    io.Hotkey("f1", []() {
        std::cout << "F1 pressed\n";
    });
    
    io.Hotkey("f2", []() {
        std::cout << "F2 pressed\n";
    });
    
    io.Hotkey("f3", []() {
        std::cout << "F3 pressed\n";
    });
    
    io.Hotkey("ctrl+shift+a", []() {
        std::cout << "Ctrl+Shift+A pressed\n";
    });
    
    io.Hotkey("ctrl+shift+b", []() {
        std::cout << "Ctrl+Shift+B pressed\n";
    });
    
    io.Hotkey("ctrl+shift+c", []() {
        std::cout << "Ctrl+Shift+C pressed\n";
    });
    
    io.Hotkey("shift+1", []() {
        std::cout << "Shift+1 pressed\n";
    });
    
    io.Hotkey("shift+2", []() {
        std::cout << "Shift+2 pressed\n";
    });
    
    io.Hotkey("alt+@", []() {
        std::cout << "Alt+@ pressed\n";
    });
    
    // Test with numpad keys
    io.Hotkey("numpad0", []() {
        std::cout << "Numpad 0 pressed\n";
    });
}

// Comment out problematic test functions
/*
void testHotkeyListen(havel::IO& io) {
    std::cout << "Testing HotkeyListen function...\n";
    io.HotkeyListen();
}

void testHandleKeyAction(havel::IO& io) {
    std::cout << "Testing HandleKeyAction function...\n";
    io.HandleKeyAction("down", "a");
}
*/

void testSetTimer(havel::IO& io) {
    std::cout << "Testing SetTimer function...\n";
    io.SetTimer(1000, []() {
        std::cout << "Timer fired\n";
    });
}

void testMsgBox(havel::IO& io) {
    std::cout << "Testing MsgBox function...\n";
    io.MsgBox("Hello, world!");
}

void linux_test(havel::WindowManager& w){
    std::cout << "Linux Test Suite\n";
    
    // Comment out the GetAllWindows call that doesn't exist
    // auto wins = w.GetAllWindows();
    // for (auto win : wins) {
    //     std::cout << "Window: " << win << std::endl;
    // }
    std::cout << "Window manager detected: " << w.GetCurrentWMName() << std::endl;
}

void windowsTest(havel::WindowManager& w){
    std::cout << "Windows Test Suite\n";
    
    // Comment out the GetAllWindows call that doesn't exist
    // auto wins = w.GetAllWindows();
    // for (auto win : wins) {
    //     std::cout << "Window: " << win << std::endl;
    // }
    std::cout << "Windows test completed" << std::endl;
}

void setupAHKHotkeys(havel::IO& io) {
    io.Hotkey("!Up", [](){ havel::WindowManager::MoveWindow(1); });
    io.Hotkey("!Down", [](){ havel::WindowManager::MoveWindow(2); });
    io.Hotkey("!Left", [](){ havel::WindowManager::MoveWindow(3); });
    io.Hotkey("!Right", [](){ havel::WindowManager::MoveWindow(4); });
    
    // Window resizing hotkeys
    io.Hotkey("!+Up", [](){ havel::WindowManager::ResizeWindow(1); });
    io.Hotkey("!+Down", [](){ havel::WindowManager::ResizeWindow(2); });
    io.Hotkey("!+Left", [](){ havel::WindowManager::ResizeWindow(3); });
    io.Hotkey("!+Right", [](){ havel::WindowManager::ResizeWindow(4); });
    
    io.Hotkey("^r", [](){ havel::WindowManager::ToggleAlwaysOnTop(); });
}

void test(havel::IO& io) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Register test hotkeys
    std::cout << "Registering test hotkeys...\n";
    testRegisterHotkey(io);
    
    // Create a Window object
    havel::Window myWindow;
    std::cout << "Created Window object\n";
    
    // Find Firefox window
    havel::wID firefoxWindow = myWindow.Find("firefox");
    if (firefoxWindow) {
        std::cout << "Found Firefox window: " << firefoxWindow << std::endl;
        std::cout << "Window title: " << myWindow.Title(firefoxWindow) << std::endl;
    }
}

int test_main(int argc, char* argv[]) {
    havel::IO io;
    std::cout << "Test main function initialized\n";
    
    havel::WindowManager wm;
    
    setupAHKHotkeys(io);
    test(io);
    
    #ifdef _WIN32
    windowsTest(wm);
    #else
    linux_test(wm);
    #endif
    
    // Test hotkey listening (this will block)
    // testHotkeyListen(io);
    
    return 0;
}