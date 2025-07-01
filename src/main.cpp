#include "./utils/Logger.hpp"
#include <iostream>
#include <thread>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
#include <csignal>
#include "core/util/SignalWatcher.hpp"
#include "window/WindowManager.hpp"
#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/ScriptEngine.hpp"
#include "core/HotkeyManager.hpp"
#include "core/SocketServer.hpp"
#include <atomic>
#include <iostream>

using namespace havel;
// Forward declare test_main
int test_main(int argc, char* argv[]);

using namespace Havel::Util;

void shutdown() {
    info("Starting graceful shutdown...");
    
    // Start shutdown process
    std::atomic<bool> shutdownComplete(false);
    
    std::thread shutdownThread([&shutdownComplete]() {
        // Do your cleanup here
        shutdownComplete.store(true);
    });
    
    // Wait with timeout
    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(3);
    
    while (!shutdownComplete.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (std::chrono::steady_clock::now() - start > timeout) {
            error("Shutdown timeout reached, forcing exit");
            shutdownThread.detach(); // Let it die
            _exit(1);
        }
    }
    
    shutdownThread.join();
    info("Graceful shutdown complete");
}
using namespace havel;

void print_hotkeys() {
    static int counter = 0;
    counter++;
    info("Hotkeys " + std::to_string(counter));
    for (const auto& [id, hotkey] : IO::hotkeys) {
        info("Hotkey ID: " + std::to_string(id) + ", alias: " + hotkey.alias + ", keycode: " + std::to_string(hotkey.key) + ", modifiers: " + std::to_string(hotkey.modifiers) + ", action: " + hotkey.action + ", enabled: " + std::to_string(hotkey.enabled) + ", blockInput: " + std::to_string(hotkey.blockInput) + ", exclusive: " + std::to_string(hotkey.exclusive) + ", success: " + std::to_string(hotkey.success) + ", suspend: " + std::to_string(hotkey.suspend));
    }
}
#ifndef RUN_TESTS
int main(int argc, char* argv[]){
    // Ensure config file exists and load config
    auto& config = Configs::Get();
    try {
        config.EnsureConfigFile();
        config.Load();
    } catch (const std::exception& e) {
        std::cerr << "Critical: Failed to initialize config: " << e.what() << std::endl;
        return 1;
    }

    // Block signals in all threads (including main)
    try {
        blockAllSignals();
    } catch (const std::exception& e) {
        error(std::string("Failed to set up signal blocking: ") + e.what());
        return EXIT_FAILURE;
    }
    
    // Start signal watcher
    SignalWatcher signalWatcher;
    signalWatcher.start();
    
    try {
        info("Starting HvC...");
        
        // Check for startup argument
        bool isStartup = false;
        for (int i = 1; i < argc; i++) {
            if (std::string(argv[i]) == "--startup") {
                isStartup = true;
                break;
            }
        }
        
        // Create IO manager
        auto io = std::make_shared<IO>();
        
        // Create window manager
        auto windowManager = std::make_shared<WindowManager>();
        
        // Create MPV controller
        auto mpv = std::make_shared<MPVController>();
        mpv->Initialize();
        // Create script engine
        auto scriptEngine = std::make_shared<ScriptEngine>(*io, *windowManager);
        
        // Create hotkey manager
        auto hotkeyManager = std::make_shared<HotkeyManager>(*io, *windowManager, *mpv, *scriptEngine);
        
        // Initialize and load debug settings
        hotkeyManager->loadDebugSettings();
        hotkeyManager->applyDebugSettings();
        
        // If starting up, set initial brightness and gamma
        if (isStartup) {
            info("Setting startup brightness and gamma values");
            hotkeyManager->getBrightnessManager().setStartupValues();
        }
        
        // Register default hotkeys
        hotkeyManager->RegisterDefaultHotkeys();
        hotkeyManager->RegisterMediaHotkeys();
        hotkeyManager->RegisterWindowHotkeys();
        hotkeyManager->RegisterSystemHotkeys();
        
        // Load user hotkey configurations
        hotkeyManager->LoadHotkeyConfigurations();
        print_hotkeys();
        
        // Watch for theme changes
        havel::Configs::Get().Watch<std::string>("UI.Theme", [](auto oldVal, auto newVal) {
            info("Theme changed from " + oldVal + " to " + newVal);
        });
        
        // Set up X11 display for event handling
        Display* display = XOpenDisplay(NULL);
        if (!display) {
            error("Failed to open X11 display");
            return 1;
        }
        int x11_fd = ConnectionNumber(display);
        
        // Main loop with better signal handling
        bool running = true;
        auto lastCheck = std::chrono::steady_clock::now();
        auto lastWindowCheck = std::chrono::steady_clock::now();
        print_hotkeys();
        
        while (running) {
            // Check if we should exit (due to signal)
            if (signalWatcher.shouldExitNow()) {
                info("Termination signal received. Exiting...");
                running = false;
                break;
            }
            
            // Check active window periodically to update hotkey states
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWindowCheck).count() >= 300) {
                hotkeyManager->checkHotkeyStates();
                // Evaluate if the current window is a gaming window
                bool isGamingWindow = hotkeyManager->evaluateCondition("currentMode == 'gaming'");

                if (isGamingWindow) {
                    hotkeyManager->grabGamingHotkeys();
                } else {
                    hotkeyManager->ungrabGamingHotkeys();
                }
                lastWindowCheck = now;
            }
            
            // Check for config changes periodically
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck).count() >= 5) {
                // Check if config file was modified
                // config.CheckModified()
                
                // Update window manager with new config
                // int newMoveSpeed = config.Get<int>("Window.MoveSpeed", 10);
                // windowManager->SetMoveSpeed(newMoveSpeed);
                
                lastCheck = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Cleanup
        info("Cleaning up...");
        
        try {
            // Shutdown MPV
            mpv->Shutdown();
            
            // Close the X11 display if it's still open
            if (display) {
                XCloseDisplay(display);
                display = nullptr;
            }
            
            // Stop the signal watcher
            signalWatcher.stop();
            
            info("HvC shutdown complete");
        } catch (const std::exception& e) {
            error(std::string("Error during cleanup: ") + e.what());
            return EXIT_FAILURE;
        }
        
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        fatal(std::string("Fatal error: ") + e.what());
        return 1;
    }
}
#endif