#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "window/WindowManager.hpp"
#include <iostream>

int main() {
    try {
        havel::IO io;
        havel::WindowManager wm;
        havel::engine::EngineConfig cfg;
        cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
        cfg.verboseOutput = false;
        cfg.enableProfiler = false;
        havel::engine::Engine engine(io, wm, cfg);

        std::cout << "=== Testing Config & Devices Integration ===\n" << std::endl;

        // Test 1: Config block writes to Configs
        {
            std::cout << "--- Test 1: Config block ---" << std::endl;
            std::string code = R"(
config {
    volume: 50,
    brightness: 100,
    theme: "dark"
}
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            
            // Verify it was written to Configs
            auto& config = havel::Configs::Get();
            std::cout << "Havel.volume = " << config.Get<int>("Havel.volume", 0) << std::endl;
            std::cout << "Havel.brightness = " << config.Get<int>("Havel.brightness", 0) << std::endl;
            std::cout << "Havel.theme = " << config.Get<std::string>("Havel.theme", "") << std::endl;
        }

        // Test 2: Devices block writes to Configs
        {
            std::cout << "\n--- Test 2: Devices block ---" << std::endl;
            std::string code = R"(
devices {
    keyboard: "INSTANT Keyboard",
    mouse: "USB Mouse",
    mouseSensitivity: 0.5,
    ignoreMouse: false
}
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            
            // Verify it was written to Configs
            auto& config = havel::Configs::Get();
            std::cout << "Device.Keyboard = " << config.Get<std::string>("Device.Keyboard", "") << std::endl;
            std::cout << "Device.Mouse = " << config.Get<std::string>("Device.Mouse", "") << std::endl;
            std::cout << "Mouse.Sensitivity = " << config.Get<double>("Mouse.Sensitivity", 0.0) << std::endl;
        }

        // Test 3: Access config values from script
        {
            std::cout << "\n--- Test 3: Access config values ---" << std::endl;
            std::string code = R"(
config {
    testValue: 42
}

print "Config test value: ${__config__["testValue"]}"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Config Integration Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
