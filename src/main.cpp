#include "qt.hpp"
#include "gui/HavelApp.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"

#ifndef DISABLE_HAVEL_LANG
#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#endif

using namespace havel;

int main(int argc, char* argv[]) {
    // Initialize config first
    try {
        auto& config = Configs::Get();
        config.EnsureConfigFile();
        config.Load();
        info("Config path: {}", config.getPath());
    } catch (const std::exception& e) {
        error("Critical: Failed to initialize config: {}", e.what());
        return 1;
    }
    XSetIOErrorHandler([](Display*) -> int {
        error("X11 connection lost - exiting gracefully");
        exit(1);
        return 0;
    });

    // Check if first argument is a script file
    std::string scriptFile;
    bool isStartup = false;
    bool debugMode = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--startup") {
            isStartup = true;
        } else if (arg == "--debug" || arg == "-d") {
            debugMode = true;
            Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: havel [script.hv] [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --startup       Run at system startup\n";
            std::cout << "  --debug, -d     Enable debug logging\n";
            std::cout << "  --help, -h      Show this help\n";
            std::cout << "\nIf a .hv script file is provided, it will be executed and the program will exit.\n";
            std::cout << "Otherwise, the GUI system tray app will start.\n";
            return 0;
        } else if (arg.ends_with(".hv") && scriptFile.empty()) {
            scriptFile = arg;
        }
    }

#ifndef DISABLE_HAVEL_LANG
    // If script file provided, execute it and exit
    if (!scriptFile.empty()) {
        info("Running Havel script: {}", scriptFile);
        
        try {
            // Read script file
            std::ifstream file(scriptFile);
            if (!file) {
                std::cerr << "Error: Cannot open script file: " << scriptFile << std::endl;
                return 2;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string code = buffer.str();
            
            // Create IO and WindowManager for script execution
            havel::IO io;
            havel::WindowManager wm;
            
            // Configure engine
            havel::engine::EngineConfig cfg;
            cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
            cfg.verboseOutput = debugMode;
            cfg.enableProfiler = debugMode;
            
            havel::engine::Engine engine(io, wm, cfg);
            
            // Register hotkeys from script
            engine.RegisterHotkeysFromCode(code);
            
            info("Script loaded successfully. Hotkeys registered. Press Ctrl+C to exit.");
            
            // Keep running to handle hotkeys
            // TODO: Implement proper signal handling for graceful exit
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Script execution error: " << e.what() << std::endl;
            return 1;
        }
    }
#else
    if (!scriptFile.empty()) {
        std::cerr << "Error: Havel language support is disabled in this build." << std::endl;
        return 1;
    }
#endif

    // No script file - run GUI app
    QApplication app(argc, argv);
    app.setApplicationName("Havel");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Havel");
    app.setQuitOnLastWindowClosed(false); // Keep running in tray

    try {
        HavelApp havelApp(isStartup);
        
        if (!havelApp.isInitialized()) {
            std::cerr << "Failed to initialize HavelApp" << std::endl;
            return 1;
        }
        
        info("Havel started successfully - running in system tray");
        return app.exec();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        error(std::string("Fatal error: ") + e.what());
        return 1;
    }
}
