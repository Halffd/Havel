#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
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

        std::cout << "=== Testing Special Blocks (Config, Devices, Modes) ===\n" << std::endl;

        // Test Config block
        {
            std::cout << "--- Test 1: Config Block ---" << std::endl;
            std::string code = R"(
config {
    debug: true,
    logLevel: "verbose",
    timeout: 30
}

print(__config__)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test Devices block
        {
            std::cout << "\n--- Test 2: Devices Block ---" << std::endl;
            std::string code = R"(
devices {
    mouse: "logitech",
    keyboard: "corsair",
    monitor: 1
}

print(__devices__)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test Modes block
        {
            std::cout << "\n--- Test 3: Modes Block ---" << std::endl;
            std::string code = R"(
modes {
    gaming: true,
    work: false,
    relaxed: false
}

print(__modes__)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test all special blocks together
        {
            std::cout << "\n--- Test 4: All Special Blocks Together ---" << std::endl;
            std::string code = R"(
config {
    windowSize: 800,
    theme: "dark"
}

devices {
    inputMethod: "keyboard_mouse"
}

modes {
    active: "gaming"
}

print("Config:")
print(__config__)
print("Devices:")
print(__devices__)
print("Modes:")
print(__modes__)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test accessing special block values
        {
            std::cout << "\n--- Test 5: Accessing Special Block Properties ---" << std::endl;
            std::string code = R"(
config {
    maxRetries: 5,
    url: "https://example.com"
}

let retries = __config__["maxRetries"]
let url = __config__["url"]

print("Max Retries:")
print(retries)
print("URL:")
print(url)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test special blocks with arrays
        {
            std::cout << "\n--- Test 6: Special Blocks with Arrays ---" << std::endl;
            std::string code = R"(
devices {
    monitors: [1, 2, 3],
    keyboards: ["main", "backup"]
}

print("Devices with arrays:")
print(__devices__)
print("First monitor:")
print(__devices__["monitors"][0])
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test special blocks with expressions
        {
            std::cout << "\n--- Test 7: Special Blocks with Expressions ---" << std::endl;
            std::string code = R"(
let baseTimeout = 10

config {
    timeout: baseTimeout * 2,
    maxConnections: 5 + 5,
    enabled: 1 > 0
}

print("Config with expressions:")
print(__config__)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test multiple special blocks of the same type (last one wins)
        {
            std::cout << "\n--- Test 8: Multiple Config Blocks (Last One Wins) ---" << std::endl;
            std::string code = R"(
config {
    value: 1
}

print("First config:")
print(__config__)

config {
    value: 2
}

print("Second config (overwrites first):")
print(__config__)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Special Blocks Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
