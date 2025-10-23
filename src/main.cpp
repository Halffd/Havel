#include "qt.hpp"
#include "gui/HavelApp.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifndef DISABLE_HAVEL_LANG
#include "havel-lang/runtime/Engine.h"
#include "havel-lang/runtime/Interpreter.hpp"
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
        if (arg == "--startup" || arg == "-s") {
            isStartup = true;
        } else if (arg == "--debug" || arg == "-d") {
            debugMode = true;
            Logger::getInstance().setLogLevel(Logger::LOG_DEBUG);
        } else if (arg == "--repl" || arg == "-r") {
            // REPL mode flag
            scriptFile = "--repl";
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: havel [script.hv] [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --startup       Run at system startup\n";
            std::cout << "  --debug, -d     Enable debug logging\n";
            std::cout << "  --repl, -r      Start interactive REPL\n";
            std::cout << "  --help, -h      Show this help\n";
            std::cout << "\nIf a .hv script file is provided, it will be executed.\n";
            std::cout << "If no script is provided, the GUI tray application starts.\n";
            return 0;
        } else {
            if(!scriptFile.empty()) {
                error("Error: Only one script file can be provided. Got {} and {}", scriptFile, arg);
                return 1;
            } else if(!arg.ends_with(".hv")) {
                warning("Script file {} does not end with .hv extension.", arg);
            }
            scriptFile = arg;
        } 
    }

#ifndef DISABLE_HAVEL_LANG
    // REPL mode
    if (scriptFile == "--repl") {
        info("Starting Havel REPL...");
        
        havel::IO io;
        havel::WindowManager wm;
        havel::Interpreter interpreter(io, wm);
        
        std::cout << "Havel Language REPL v1.0\n";
        std::cout << "Type 'exit' or 'quit' to exit, 'help' for help\n\n";
        
        std::string line;
        std::string multiline;
        int braceCount = 0;
        
        // REPL log file
        std::string home = std::getenv("HOME") ? std::getenv("HOME") : std::string(".");
        std::string logPath = home + "/.havel_repl.log";
        std::ofstream replLog(logPath, std::ios::app);
        
        while (true) {
            // Prompt
            std::string prompt = (braceCount > 0) ? "... " : ">>> ";
            
#ifdef HAVE_READLINE
            char* input = readline(prompt.c_str());
            if (!input) break; // EOF
            line = std::string(input);
            free(input);
            if (!line.empty()) add_history(line.c_str());
#else
            std::cout << prompt;
            if (!std::getline(std::cin, line)) break;
#endif
            
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) {
                if (braceCount == 0) continue;
                line = "";
            } else {
                line = line.substr(start);
            }
            
            // Log input
            if (replLog.is_open()) {
                replLog << line << '\n';
                replLog.flush();
            }
            
            // Commands
            if (braceCount == 0) {
                if (line == "exit" || line == "quit") {
                    std::cout << "Goodbye!\n";
                    return 0;
                }
                if (line == "help") {
                    std::cout << "Available commands:\n";
                    std::cout << "  exit, quit  - Exit REPL\n";
                    std::cout << "  help        - Show this help\n";
                    std::cout << "  clear       - Clear screen\n";
                    std::cout << "\nType any Havel expression or statement to evaluate.\n";
                    continue;
                }
                if (line == "clear") {
                    std::cout << "\033[2J\033[1;1H"; // ANSI clear screen
                    continue;
                }
                if (line.empty()) {
                    continue;
                }
            }
            
            // Track braces for multi-line input
            for (char c : line) {
                if (c == '{') braceCount++;
                else if (c == '}') braceCount--;
            }
            
            multiline += line + "\n";
            
            // Execute when braces are balanced
            if (braceCount == 0 && !multiline.empty()) {
                try {
                    auto result = interpreter.Execute(multiline);
                    
                    // Display result if not null/void
                    if (std::holds_alternative<havel::HavelValue>(result)) {
                        auto val = std::get<havel::HavelValue>(result);
                        if (!std::holds_alternative<std::nullptr_t>(val)) {
                            std::cout << "=> " << havel::Interpreter::ValueToString(val) << "\n";
                        }
                    } else if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                        std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << "\n";
                }
                
                multiline.clear();
            }
        }
        
        return 0;
    }
    
    // If script file provided, execute it
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
            havel::Interpreter interpreter(io, wm);
            
            if (debugMode) {
                std::cout << "=== Executing script: " << scriptFile << " ===\n";
            }
            
            // Execute script
            auto result = interpreter.Execute(code);
            
            // Check for errors
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Runtime Error: " << std::get<havel::HavelRuntimeError>(result).what() << "\n";
                return 1;
            }
            
            if (debugMode) {
                std::cout << "=== Script executed successfully ===\n";
            }
            
            // Check if script contains hotkeys by looking for => operator
            bool hasHotkeys = code.find("=>") != std::string::npos;
            
            if (hasHotkeys) {
                // For scripts with hotkeys, keep running
                havel::engine::EngineConfig cfg;
                cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
                cfg.verboseOutput = debugMode;
                
                havel::engine::Engine engine(io, wm, cfg);
                engine.RegisterHotkeysFromCode(code);
                
                info("Script loaded. Hotkeys registered. Press Ctrl+C to exit.");
                
                // Keep running to handle hotkeys
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } else {
                // Script has no hotkeys, exit after execution
                if (debugMode) {
                    std::cout << "No hotkeys detected, exiting.\n";
                }
                return 0;
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
