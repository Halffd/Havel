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

        std::cout << "=== Testing String Interpolation ===\n" << std::endl;

        // Test 1: Simple interpolation
        {
            std::cout << "--- Test 1: Simple ${} ---" << std::endl;
            std::string code = R"(
let name = "Havel"
let res = "Hello, ${name}!"
print(res)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 2: Expression inside ${}
        {
            std::cout << "\n--- Test 2: Expression in ${} ---" << std::endl;
            std::string code = R"(
let a = 5
let b = 7
print("Sum: ${a + b}")
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 3: Multiple segments
        {
            std::cout << "\n--- Test 3: Multiple segments ---" << std::endl;
            std::string code = R"(
let who = "world"
print("Hello, ${who}. The time is ${1+1} o'clock.")
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 4: Interpolation with pipeline
        {
            std::cout << "\n--- Test 4: Interpolation + Pipeline ---" << std::endl;
            std::string code = R"(
let who = "world"
let msg = "hello ${who}"
print(msg | upper)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Interpolation Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
