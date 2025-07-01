#include "./utils/Logger.hpp"
#include <iostream>
#include <thread>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
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

using namespace havel;
// Forward declare test_main
int test_main(int argc, char* argv[]);

// SignalHandler class for proper signal handling
class SignalHandler {
private:
    static int signal_pipe[2];
    static std::atomic<bool> gShouldExit;
    static std::atomic<int> gSignalStatus;
    
public:
    static void Initialize() {
        if (pipe(signal_pipe) == -1) {
            throw std::runtime_error("Failed to create signal pipe");
        }
        
        // Make write end non-blocking
        int flags = fcntl(signal_pipe[1], F_GETFL);
        fcntl(signal_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }
    
    static void Handler(int signal) {
        // Write signal number to pipe to wake up select()
        fprintf(stderr, "SIGNAL HANDLER CALLED: %d\n", signal);
    
        char sig = static_cast<char>(signal);
        ssize_t result = write(signal_pipe[1], &sig, 1);
        if (result == -1) {
            // Pipe is broken or full
            _exit(1);  // Emergency exit from signal handler
        }
        // Set the signal status
        gSignalStatus.store(signal, std::memory_order_relaxed);
        
        // Set the exit flag for termination signals
        if (signal == SIGINT || signal == SIGTERM) {
            gShouldExit.store(true, std::memory_order_relaxed);
        }
    }
    
    static int GetReadFd() { return signal_pipe[0]; }
    static bool ShouldExit() { return gShouldExit.load(std::memory_order_relaxed); }
    static int GetSignalStatus() { return gSignalStatus.load(std::memory_order_relaxed); }
    static void ClearSignalStatus() { gSignalStatus.store(0, std::memory_order_relaxed); }
    
    static void Cleanup() {
        close(signal_pipe[0]);
        close(signal_pipe[1]);
    }
};

// Static member definitions
int SignalHandler::signal_pipe[2];
std::atomic<bool> SignalHandler::gShouldExit(false);
std::atomic<int> SignalHandler::gSignalStatus(0);

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

// Simple socket server for external control
class AppServer : public SocketServer {
public:
    AppServer(int port) : SocketServer(port) {}
    
    void HandleCommand(const std::string& cmd) override {
        debug("Socket command received: " + cmd);
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

    // Initialize signal handling with self-pipe
    try {
        SignalHandler::Initialize();
    } catch (const std::exception& e) {
        error(std::string("Failed to initialize signal handling: ") + e.what());
        return EXIT_FAILURE;
    }
    
    // Set up signal handlers using sigaction
    struct sigaction sa;
    sa.sa_handler = SignalHandler::Handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart interrupted system calls
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        error("Failed to set up SIGINT handler");
        SignalHandler::Cleanup();
        return EXIT_FAILURE;
    }
    
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        error("Failed to set up SIGTERM handler");
        SignalHandler::Cleanup();
        return EXIT_FAILURE;
    }
    
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
        // Start socket server for external control
        AppServer server(config.Get<int>("Network.Port", 8765));
        server.Start();
        
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
        
        while (running && !SignalHandler::ShouldExit()) {
            // Use select to wait for X11 events, signals, or timeout
            fd_set in_fds;
            struct timeval tv;
            FD_ZERO(&in_fds);
            FD_SET(x11_fd, &in_fds);
            FD_SET(SignalHandler::GetReadFd(), &in_fds);
            
            int max_fd = std::max(x11_fd, SignalHandler::GetReadFd());
            tv.tv_usec = 100000;
            tv.tv_sec = 0;
            
            int result = select(max_fd + 1, &in_fds, NULL, NULL, &tv);
            
            // Check if signal pipe has data (means signal was received)
            if (FD_ISSET(SignalHandler::GetReadFd(), &in_fds)) {
                char signal_data;
                ssize_t bytes_read = read(SignalHandler::GetReadFd(), &signal_data, 1);
                if (bytes_read > 0) {
                    info("Signal received: " + std::to_string(static_cast<int>(signal_data)));
                    if (signal_data == SIGINT || signal_data == SIGTERM) {
                        info("Termination signal received. Exiting...");
                        running = false;
                        break;
                    }
                }
            }
            
            // Process X11 events if any are available
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                // Process the event here if needed
                // For now, we'll just discard it since we're not handling specific events
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
        info("Cleaning up...");
        
        try {
            // Stop the server
            server.Stop();
            
            // Shutdown MPV
            mpv->Shutdown();
            
            // Close the X11 display if it's still open
            if (display) {
                XCloseDisplay(display);
                display = nullptr;
            }
            
            // Clean up signal handling resources
            SignalHandler::Cleanup();
            
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