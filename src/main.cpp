#include "./utils/Logger.hpp"
#include <iostream>
#include <thread>
#include <memory>
#include "window/WindowManager.hpp"
#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/ScriptEngine.hpp"
#include <csignal>
#include "core/HotkeyManager.hpp"
// #include "media/MPVController.hpp"  // Comment out this include since we already have core/MPVController.hpp
#include "core/SocketServer.hpp"
#include <atomic>
#include <iostream>

// Forward declare test_main
int test_main(int argc, char* argv[]);

// ONLY use atomic types in signal handlers
std::atomic<bool> gShouldExit(false);
std::atomic<int> gSignalStatus(0);

// Minimal, signal-safe handler
void SignalHandler(int signal) {
    // ONLY async-signal-safe operations allowed here!
    gSignalStatus.store(signal);
    
    if (signal == SIGINT || signal == SIGTERM) {
        gShouldExit.store(true);
    }
    
    // That's it! No logging, no threads, no complex logic!
}

void shutdown() {
    lo.info("Starting graceful shutdown...");
    
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
            lo.error("Shutdown timeout reached, forcing exit");
            shutdownThread.detach(); // Let it die
            _exit(1);
        }
    }
    
    shutdownThread.join();
    lo.info("Graceful shutdown complete");
}
using namespace havel;

// Simple socket server for external control
class AppServer : public SocketServer {
public:
    AppServer(int port) : SocketServer(port) {}
    
    void HandleCommand(const std::string& cmd) override {
        lo.debug("Socket command received: " + cmd);
        if (cmd == "toggle_mute") ToggleMute();
        else if (cmd.find("volume:") == 0) {
            int vol = std::stoi(cmd.substr(7));
            AdjustVolume(vol);
        }
    }
    
    void ToggleMute() { /* ... */ }
    void AdjustVolume(int) { /* ... */ }
};
void print_hotkeys() {
    static int counter = 0;
    counter++;
    lo.info("Hotkeys " + std::to_string(counter));
    for (const auto& [id, hotkey] : IO::hotkeys) {
        lo.info("Hotkey ID: " + std::to_string(id) + ", alias: " + hotkey.alias + ", keycode: " + std::to_string(hotkey.key) + ", modifiers: " + std::to_string(hotkey.modifiers) + ", action: " + hotkey.action + ", enabled: " + std::to_string(hotkey.enabled) + ", blockInput: " + std::to_string(hotkey.blockInput) + ", exclusive: " + std::to_string(hotkey.exclusive) + ", success: " + std::to_string(hotkey.success) + ", suspend: " + std::to_string(hotkey.suspend));
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

    // Set up signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    try {
        lo.info("Starting HvC...");
        
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
        hotkeyManager->initDebugSettings();
        hotkeyManager->loadDebugSettings();
        hotkeyManager->applyDebugSettings();
        
        // If starting up, set initial brightness and gamma
        if (isStartup) {
            lo.info("Setting startup brightness and gamma values");
            hotkeyManager->getBrightnessManager().setStartupValues();
        }
        
        // Register default hotkeys
        hotkeyManager->RegisterDefaultHotkeys();
        print_hotkeys();
        hotkeyManager->RegisterMediaHotkeys();
        print_hotkeys();
        hotkeyManager->RegisterWindowHotkeys();
        print_hotkeys();
        hotkeyManager->RegisterSystemHotkeys();
        print_hotkeys();
        
        // Load user hotkey configurations
        hotkeyManager->LoadHotkeyConfigurations();
        print_hotkeys();
        // Start socket server for external control
        AppServer server(config.Get<int>("Network.Port", 8765));
        server.Start();
        
        // Watch for theme changes
        havel::Configs::Get().Watch<std::string>("UI.Theme", [](auto oldVal, auto newVal) {
            lo.info("Theme changed from " + oldVal + " to " + newVal);
        });
        
        // Main loop
        bool running = true;
        auto lastCheck = std::chrono::steady_clock::now();
        auto lastWindowCheck = std::chrono::steady_clock::now();
        print_hotkeys();
        while (running && !gShouldExit.load()) {
            // Process events
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Check for signals
            int signal = gSignalStatus.load();
            if (signal != 0) {
                lo.info("Handling signal: " + std::to_string(signal));
                if (signal == SIGINT || signal == SIGTERM) {
                    lo.info("Termination signal received. Exiting...");
                    running = false;
                }
                gSignalStatus.store(0); // Reset
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
        }
        
        // Cleanup
        lo.info("Stopping server...");
        server.Stop();
        
        lo.info("Shutting down HvC...");
        
        shutdown();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        lo.fatal(std::string("Fatal error: ") + e.what());
        return 1;
    }
}
#endif