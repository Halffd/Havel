#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include "../core/IO.hpp"
#include "../core/DisplayManager.hpp"
#include "../window/WindowManager.hpp"

bool running = true;

void signal_handler(int signal) {
    std::cout << "Caught signal " << signal << ", exiting..." << std::endl;
    running = false;
}

void test_volume_up() {
    std::cout << "Volume Up hotkey triggered!" << std::endl;
}

void test_volume_down() {
    std::cout << "Volume Down hotkey triggered!" << std::endl;
}

void test_mute() {
    std::cout << "Mute hotkey triggered!" << std::endl;
}

void test_play_pause() {
    std::cout << "Play/Pause hotkey triggered!" << std::endl;
}

void test_always_on_top() {
    std::cout << "Always On Top hotkey triggered!" << std::endl;
    H::WindowManager::ToggleAlwaysOnTop();
}

void test_alt_tab() {
    std::cout << "Alt+Tab hotkey triggered!" << std::endl;
    H::WindowManager::AltTab();
}

int main() {
    // Set up signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting hotkey test program..." << std::endl;
    
    // Initialize the IO system
    H::IO io;
    
    // Register hotkeys
    std::cout << "Registering hotkeys..." << std::endl;
    
    // Media control hotkeys
    io.Hotkey("^F9", test_volume_down);
    io.Hotkey("^F10", test_volume_up);
    io.Hotkey("^F11", test_mute);
    io.Hotkey("^F12", test_play_pause);
    
    // Window management hotkeys
    io.Hotkey("^+a", test_always_on_top);
    io.Hotkey("^!t", test_alt_tab);
    
    std::cout << "Hotkey registration complete." << std::endl;
    std::cout << "Available hotkeys:" << std::endl;
    std::cout << "  Ctrl+F9 = Volume Down" << std::endl;
    std::cout << "  Ctrl+F10 = Volume Up" << std::endl;
    std::cout << "  Ctrl+F11 = Mute" << std::endl;
    std::cout << "  Ctrl+F12 = Play/Pause" << std::endl;
    std::cout << "  Ctrl+Shift+A = Toggle Always On Top" << std::endl;
    std::cout << "  Ctrl+Alt+T = Alt+Tab" << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    
    // Main loop
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Shutting down..." << std::endl;
    return 0;
} 