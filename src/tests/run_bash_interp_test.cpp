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

        std::cout << "=== Testing Bash-Style Interpolation ===\n" << std::endl;

        // Test 1: Simple $var
        {
            std::cout << "--- Test 1: $var syntax ---" << std::endl;
            std::string code = R"(
let name = "Havel"
print "Hello, $name!"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 2: Multiple $vars
        {
            std::cout << "\n--- Test 2: Multiple $vars ---" << std::endl;
            std::string code = R"(
let name = "Alice"
let age = 25
print "Hello $name, you are $age years old"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 3: Mixed $var and ${expr}
        {
            std::cout << "\n--- Test 3: Mixed $var and ${expr} ---" << std::endl;
            std::string code = R"(
let number = 10
print "Value: $number, doubled: ${number * 2}"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 4: Bash-style with math
        {
            std::cout << "\n--- Test 4: Expression interpolation ---" << std::endl;
            std::string code = R"(
let a = 5
let b = 7
print "Math: $a + $b = ${a + b}"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 5: With implicit calls
        {
            std::cout << "\n--- Test 5: Implicit call + $var ---" << std::endl;
            std::string code = R"(
let user = "Bob"
print "Welcome, $user!"
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Bash-Style Interpolation Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
