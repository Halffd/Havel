#include "system_includes.h"
#include "window/Window.hpp"
#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/HotkeyManager.hpp"
#include "core/DisplayManager.hpp"
#include "window/WindowManager.hpp"
#include "window/WindowManagerDetector.hpp"
#include "utils/Logger.hpp"
#include "x11.h"
#include <iostream>

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
    
    io.Hotkey("^+a", []() {
        std::cout << "Ctrl+Shift+A pressed\n";
    });
    
    io.Hotkey("^+b", []() {
        std::cout << "Ctrl+Shift+B pressed\n";
    });
    
    io.Hotkey("^+c", []() {
        std::cout << "Ctrl+Shift+C pressed\n";
    });
    
    io.Hotkey("+1", []() {
        std::cout << "Shift+1 pressed\n";
    });
    
    io.Hotkey("+2", []() {
        std::cout << "Shift+2 pressed\n";
    });
    
    io.Hotkey("!+2", []() {
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
    std::cout << "Session Type: " << WindowManagerDetector::sessionType << std::endl;
    std::cout << "Session Name: " << WindowManagerDetector::sessionName << std::endl;
    std::cout << "Is Wayland: " << w.IsWayland() << std::endl;
    std::cout << "Is X11: " << w.IsX11() << std::endl;
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
    io.Hotkey("!Up", [](){ havel::WindowManager::MoveToCorners(1); });
    io.Hotkey("!Down", [](){ havel::WindowManager::MoveToCorners(2); });
    io.Hotkey("!Left", [](){ havel::WindowManager::MoveToCorners(3); });
    io.Hotkey("!Right", [](){ havel::WindowManager::MoveToCorners(4); });
    
    // Window resizing hotkeys
    io.Hotkey("!+Up", [](){ havel::WindowManager::ResizeToCorner(1); });
    io.Hotkey("!+Down", [](){ havel::WindowManager::ResizeToCorner(2); });
    io.Hotkey("!+Left", [](){ havel::WindowManager::ResizeToCorner(3); });
    io.Hotkey("!+Right", [](){ havel::WindowManager::ResizeToCorner(4); });
    
    io.Hotkey("^r", [](){ havel::WindowManager::ToggleAlwaysOnTop(); });
}

void test(havel::IO& io) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Register test hotkeys
    std::cout << "Registering test hotkeys...\n";
    testRegisterHotkey(io);
    
    // Create a Window object
    havel::Window myWindow = havel::Window();
    std::cout << "Created Window object\n";
    
    // Find Firefox window
    wID firefoxWindow = myWindow.Find("firefox");
    if (firefoxWindow) {
        std::cout << "Found Firefox window: " << firefoxWindow << std::endl;
        std::cout << "Window title: " << myWindow.Title(firefoxWindow) << std::endl;
    }
}

int main(int argc, char* argv[]) {
    havel::BrightnessManager mgr;
    
    // === BASIC RGB GAMMA ===
    mgr.setGammaRGB(1.0, 0.8, 0.6);              // Warm all monitors
    mgr.setGammaRGB("DP-1", 1.2, 1.0, 0.8);     // Cool specific monitor
    
    // === KELVIN TEMPERATURE (MUCH EASIER) ===
    mgr.setTemperature(3000);          // Warm 3000K all monitors
    mgr.setTemperature("HDMI-1", 6500); // Daylight 6500K specific monitor
    
    // === COMBINED OPERATIONS ===
    mgr.setBrightnessAndTemperature(0.7, 4000);           // Dim + warm all
    mgr.setBrightnessAndTemperature("DP-2", 0.9, 5500);  // Bright + neutral specific
    
    // === TEMPERATURE INCREMENTS ===
    mgr.increaseTemperature();           // +200K default, all monitors
    mgr.decreaseTemperature("DP-1");     // -200K default, specific monitor
    mgr.increaseTemperature(500);        // +500K custom, all monitors
    mgr.decreaseTemperature("HDMI-1", 100); // -100K custom, specific monitor
    
    // === DAY/NIGHT AUTOMATION - THE KILLER FEATURE ===
    BrightnessManager::DayNightSettings dayNight;
    dayNight.dayBrightness = 1.0;
    dayNight.nightBrightness = 0.3;
    dayNight.dayTemperature = 6500;    // Cool daylight
    dayNight.nightTemperature = 2700;  // Warm candlelight
    dayNight.dayStartHour = 7;         // 7 AM
    dayNight.nightStartHour = 20;      // 8 PM
    dayNight.checkInterval = std::chrono::minutes(10); // Check every 10 mins
    
    mgr.enableDayNightMode(dayNight);   // SET AND FORGET! 🚀
    
    // Manual overrides
    mgr.switchToNight();           // Force night mode all monitors
    mgr.switchToDay("DP-1");       // Force day mode specific monitor
    
    // Configuration on the fly
    mgr.setDaySettings(0.95, 6200);    // Slightly dimmer day
    mgr.setNightSettings(0.2, 2400);   // Very dim, very warm night
    mgr.setDayNightTiming(6, 21);      // 6 AM to 9 PM day cycle
    
    // Check current state
    if (mgr.isDay()) {
        info("Currently in day mode");
    } else {
        info("Currently in night mode");
    }
    
    // Disable when done
    mgr.disableDayNightMode();
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