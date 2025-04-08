#include "utils/Logger.hpp"
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
#include "core/AutoClicker.hpp"
#include "core/EmergencySystem.hpp"
#include "core/SequenceDetector.hpp"
#include "utils/Notifier.hpp"
#include "window/WindowRules.hpp"
#include "core/MouseGesture.hpp"
#include "core/MacroSystem.hpp"

// Forward declare test_main
int test_main(int argc, char* argv[]);

volatile std::sig_atomic_t gSignalStatus = 0;
volatile bool gShouldExit = false;
std::thread* gForcedExitThread = nullptr;

void SignalHandler(int signal) {
    lo.info("Received signal: " + std::to_string(signal));
    gSignalStatus = signal;
    
    if (signal == SIGINT || signal == SIGTERM) {
        lo.info("Termination signal received. Exiting...");
        gShouldExit = true;
        
        // Force exit after a timeout if clean shutdown doesn't work
        // Use a global thread pointer instead of a static thread variable
        if (gForcedExitThread == nullptr) {
            gForcedExitThread = new std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (gShouldExit) {
                    lo.error("Forced exit due to timeout");
                    _exit(1); // Emergency exit
                }
            });
            gForcedExitThread->detach();
        }

        // Don't call exit(0) immediately; let the main loop exit gracefully
        // This avoids segfaults with the thread still running
    }
}

using namespace H;

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

#ifndef RUN_TESTS
int main(int argc, char* argv[]) {
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
        
        // Load configuration
        auto& config = Configs::Get();
        config.Load("config.json");
        
        // Create IO manager
        auto io = std::make_shared<IO>();
        
        // Create window manager
        auto windowManager = std::make_shared<WindowManager>();
        
        // Create MPV controller
        auto mpv = std::make_shared<MPVController>();
        
        // Create script engine
        auto scriptEngine = std::make_shared<ScriptEngine>(*io, *windowManager);
        
        // Create hotkey manager
        auto hotkeyManager = std::make_shared<HotkeyManager>(*io, *windowManager, *mpv, *scriptEngine);
        
        // If starting up, set initial brightness and gamma
        if (isStartup) {
            lo.info("Setting startup brightness and gamma values");
            hotkeyManager->getBrightnessManager().setStartupValues();
        }
        
        // Register default hotkeys
        hotkeyManager->RegisterDefaultHotkeys();
        hotkeyManager->RegisterMediaHotkeys();
        hotkeyManager->RegisterWindowHotkeys();
        hotkeyManager->RegisterSystemHotkeys();
        
        // Load user hotkey configurations
        hotkeyManager->LoadHotkeyConfigurations();
        
        // Initialize window manager
        int moveSpeed = config.Get<int>("Window.MoveSpeed", 10);
        // windowManager->SetMoveSpeed(moveSpeed);
        
        // Start socket server for external control
        AppServer server(config.Get<int>("Network.Port", 8765));
        server.Start();
        
        // Watch for theme changes
        H::Configs::Get().Watch<std::string>("UI.Theme", [](auto oldVal, auto newVal) {
            lo.info("Theme changed from " + oldVal + " to " + newVal);
        });
        
        // Main loop
        bool running = true;
        auto lastCheck = std::chrono::steady_clock::now();
        auto lastWindowCheck = std::chrono::steady_clock::now();
        
        while (running && !gShouldExit) {
            // Process events
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Check for signals
            if (gSignalStatus != 0) {
                lo.info("Handling signal: " + std::to_string(gSignalStatus));
                if (gSignalStatus == SIGINT || gSignalStatus == SIGTERM) {
                    running = false;
                }
                gSignalStatus = 0;
            }
            
            // Check active window periodically to update hotkey states
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWindowCheck).count() >= 300) {
                // Evaluate if the current window is a gaming window
                bool isGamingWindow = hotkeyManager->evaluateCondition("currentMode == 'gaming'");
                
                if (isGamingWindow) {
                    // Register hotkeys if a gaming window is found
                    hotkeyManager->grabMPVHotkeys();
                } else {
                    // Unregister hotkeys if no gaming window is found
                    hotkeyManager->ungrabMPVHotkeys();
                }
                
                // Also check Koikatu condition for D key overlay
                hotkeyManager->evaluateCondition("Window.Active('class:Koikatu')");
                hotkeyManager->evaluateCondition("Window.Active('name:Koikatu')");
                
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
        
        // Clean up the forced exit thread if it was created
        if (gForcedExitThread) {
            delete gForcedExitThread;
            gForcedExitThread = nullptr;
        }
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        lo.fatal(std::string("Fatal error: ") + e.what());
        return 1;
    }
}
#endif