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

        std::cout << "=== Testing Implicit Function Calls ===\n" << std::endl;

        // Test 1: Command-style syntax with string
        {
            std::cout << "--- Test 1: send \"text\" (implicit call) ---" << std::endl;
            std::string code = R"(
// Note: send won't actually send to system in test, just checking syntax
print "Hello, World!"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 2: Implicit call with number
        {
            std::cout << "\n--- Test 2: print 42 (implicit call with number) ---" << std::endl;
            std::string code = R"(
print 42
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 3: Implicit call with variable
        {
            std::cout << "\n--- Test 3: Explicit vs Implicit ---" << std::endl;
            std::string code = R"(
let msg = "Test message"
print(msg)
print msg
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 4: Implicit call with string interpolation
        {
            std::cout << "\n--- Test 4: Implicit call + interpolation ---" << std::endl;
            std::string code = R"(
let name = "Alice"
print "Welcome, ${name}!"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Implicit Call Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
