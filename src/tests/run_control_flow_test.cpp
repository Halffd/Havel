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

        std::cout << "=== Testing Control Flow ===\n" << std::endl;

        // Test if/else
        {
            std::cout << "--- Test 1: If Statement (true) ---" << std::endl;
            std::string code = "let x = 10\nif x > 5 {\n  print(\"x is greater than 5\")\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 2: If Statement (false) ---" << std::endl;
            std::string code = "let x = 3\nif x > 5 {\n  print(\"won't print\")\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 3: If-Else Statement ---" << std::endl;
            std::string code = "let x = 3\nif x > 5 {\n  print(\"x > 5\")\n} else {\n  print(\"x <= 5\")\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 4: If-Elif-Else Chain ---" << std::endl;
            std::string code = "let x = 5\nif x < 5 {\n  print(\"less\")\n} else if x == 5 {\n  print(\"equal\")\n} else {\n  print(\"greater\")\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test while loops
        {
            std::cout << "\n--- Test 5: While Loop (count to 5) ---" << std::endl;
            std::string code = "let i = 0\nwhile i < 5 {\n  print(i)\n  i = i + 1\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 6: While Loop (never executes) ---" << std::endl;
            std::string code = "let i = 10\nwhile i < 5 {\n  print(\"won't print\")\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 7: Nested While Loops ---" << std::endl;
            std::string code = "let i = 0\nwhile i < 3 {\n  let j = 0\n  while j < 2 {\n    print(i)\n    print(j)\n    j = j + 1\n  }\n  i = i + 1\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test ternary operator
        {
            std::cout << "\n--- Test 8: Ternary Operator (true) ---" << std::endl;
            std::string code = "let x = 10\nlet result = x > 5 ? \"big\" : \"small\"\nprint(result)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 9: Ternary Operator (false) ---" << std::endl;
            std::string code = "let x = 3\nlet result = x > 5 ? \"big\" : \"small\"\nprint(result)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 10: Nested Ternary ---" << std::endl;
            std::string code = "let x = 5\nlet result = x < 5 ? \"less\" : x == 5 ? \"equal\" : \"greater\"\nprint(result)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 11: Ternary with Expressions ---" << std::endl;
            std::string code = "let a = 3\nlet b = 7\nlet max = a > b ? a : b\nprint(max)";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        // Test combinations
        {
            std::cout << "\n--- Test 12: While with If-Else ---" << std::endl;
            std::string code = "let i = 0\nwhile i < 5 {\n  if i == 2 {\n    print(\"two\")\n  } else {\n    print(i)\n  }\n  i = i + 1\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        {
            std::cout << "\n--- Test 13: Ternary in While Condition ---" << std::endl;
            std::string code = "let i = 0\nlet limit = 3\nwhile i < (limit > 5 ? 10 : 3) {\n  print(i)\n  i = i + 1\n}";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
        }

        std::cout << "\n=== All Control Flow Tests Complete! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
