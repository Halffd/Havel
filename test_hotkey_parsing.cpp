#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::cout << "=== Testing Hotkey Parsing ===" << std::endl;
    
    // Read the hotkeys file
    std::ifstream file("hotkeys_batch_1.hv");
    if (!file.is_open()) {
        std::cerr << "❌ Failed to open hotkeys_batch_1.hv" << std::endl;
        return 1;
    }
    
    std::string code((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    file.close();
    
    std::cout << "📖 Read hotkeys file content:" << std::endl;
    std::cout << code << std::endl;
    
    try {
        // Create engine
        havel::IO io;
        havel::WindowManager wm;
        havel::engine::EngineConfig cfg;
        cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
        cfg.verboseOutput = true;
        havel::engine::Engine engine(io, wm, cfg);
        
        std::cout << "\n🔧 Parsing and executing hotkeys..." << std::endl;
        
        try {
            // Execute the code
            auto result = engine.ExecuteCode(code);
            std::cout << "✅ SUCCESS: Hotkeys parsed and executed successfully!" << std::endl;
            std::cout << "📋 Result: Hotkey registration completed (returned null)" << std::endl;
            return 0;
        } catch (const std::exception& execException) {
            std::cout << "❌ EXECUTION FAILED: " << execException.what() << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "💥 Exception during parsing: " << e.what() << std::endl;
        return 1;
    }
}