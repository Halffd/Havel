#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main() {
    try {
        havel::IO io;
        havel::WindowManager wm;
        havel::engine::EngineConfig cfg;
        cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
        cfg.verboseOutput = false;
        cfg.enableProfiler = false;
        havel::engine::Engine engine(io, wm, cfg);

        std::cout << "=== Testing Array and Object Literals ===" << std::endl;

        // Test array literals
        {
            std::cout << "\n--- Test 1: Simple Array ---" << std::endl;
            std::string code = "let arr = [1, 2, 3]\nprint(arr)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test object literals
        {
            std::cout << "\n--- Test 2: Simple Object ---" << std::endl;
            std::string code = "let obj = { a: 1, b: 2 }\nprint(obj)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test empty array
        {
            std::cout << "\n--- Test 3: Empty Array ---" << std::endl;
            std::string code = "let arr = []\nprint(arr)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test empty object
        {
            std::cout << "\n--- Test 4: Empty Object ---" << std::endl;
            std::string code = "let obj = {}\nprint(obj)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test nested structures
        {
            std::cout << "\n--- Test 5: Nested Arrays ---" << std::endl;
            std::string code = "let arr = [1, [2, 3], 4]\nprint(arr)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 6: Nested Objects ---" << std::endl;
            std::string code = "let obj = { outer: { inner: 42 } }\nprint(obj)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test mixed structures
        {
            std::cout << "\n--- Test 7: Array in Object ---" << std::endl;
            std::string code = "let obj = { numbers: [1, 2, 3] }\nprint(obj)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 8: Object in Array ---" << std::endl;
            std::string code = "let arr = [{ x: 1 }, { x: 2 }]\nprint(arr)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test expressions in literals
        {
            std::cout << "\n--- Test 9: Expressions in Array ---" << std::endl;
            std::string code = "let arr = [1 + 1, 2 * 3, 10 - 5]\nprint(arr)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 10: Expressions in Object ---" << std::endl;
            std::string code = "let obj = { sum: 1 + 2, product: 3 * 4 }\nprint(obj)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test array indexing
        {
            std::cout << "\n--- Test 11: Array Indexing ---" << std::endl;
            std::string code = "let arr = [10, 20, 30]\nprint(arr[0])\nprint(arr[1])\nprint(arr[2])";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 12: Nested Array Indexing ---" << std::endl;
            std::string code = "let arr = [[1, 2], [3, 4]]\nprint(arr[0])\nprint(arr[1][0])";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 13: Object Property Access ---" << std::endl;
            std::string code = "let obj = { name: \"Alice\", age: 30 }\nprint(obj[\"name\"])\nprint(obj[\"age\"])";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 14: Computed Index ---" << std::endl;
            std::string code = "let arr = [100, 200, 300]\nlet i = 1\nprint(arr[i])\nprint(arr[i + 1])";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
