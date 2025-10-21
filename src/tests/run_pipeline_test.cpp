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

        std::cout << "=== Testing Pipeline Operator ===\n" << std::endl;

        // Test 1: Simple pipeline
        {
            std::cout << "--- Test 1: Simple pipeline with upper ---" << std::endl;
            std::string code = R"(
let text = "hello world"
let result = text | upper
print(result)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 2: Chained pipeline
        {
            std::cout << "\n--- Test 2: Chained pipeline ---" << std::endl;
            std::string code = R"(
let text = "  hello world  "
let result = text | trim | upper
print(result)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 3: Pipeline with function calls
        {
            std::cout << "\n--- Test 3: Pipeline with function calls ---" << std::endl;
            std::string code = R"(
let text = "hello world"
let result = text | upper | replace("WORLD", "UNIVERSE")
print(result)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 4: Pipeline with clipboard (simulated)
        {
            std::cout << "\n--- Test 4: Pipeline with text transformation ---" << std::endl;
            std::string code = R"(
let text = "test text"
let result = text | upper | trim
print("Result: " + result)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test 5: Pipeline with multiple transformations
        {
            std::cout << "\n--- Test 5: Multiple pipeline transformations ---" << std::endl;
            std::string code = R"(
let text = "  Hello World  "
let result = text | trim | lower | upper
print(result)
)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Pipeline Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
