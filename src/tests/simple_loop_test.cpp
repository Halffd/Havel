#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/parser/Parser.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <iostream>

int main() {
    try {
        havel::IO io;
        havel::WindowManager wm;
        havel::Interpreter interpreter(io, wm);

        std::cout << "=== Testing Loop Constructs ===\n" << std::endl;

        // Test 1: Range expression
        {
            std::cout << "Test 1: Range Expression" << std::endl;
            std::string code = "let range = 0..5\nprint(range)";
            auto result = interpreter.Execute(code);
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << std::endl;
                return 1;
            }
            std::cout << "✓ Range works\n" << std::endl;
        }

        // Test 2: For-in loop with range
        {
            std::cout << "Test 2: For-In Loop with Range" << std::endl;
            std::string code = "for i in 0..3 {\n  print(i)\n}";
            auto result = interpreter.Execute(code);
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << std::endl;
                return 1;
            }
            std::cout << "✓ For-in loop works\n" << std::endl;
        }

        // Test 3: Break statement
        {
            std::cout << "Test 3: Break Statement" << std::endl;
            std::string code = "for i in 0..10 {\n  if (i == 5) {\n    break\n  }\n  print(i)\n}";
            auto result = interpreter.Execute(code);
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << std::endl;
                return 1;
            }
            std::cout << "✓ Break works\n" << std::endl;
        }

        // Test 4: Continue statement
        {
            std::cout << "Test 4: Continue Statement" << std::endl;
            std::string code = "for i in 0..5 {\n  if (i == 2) {\n    continue\n  }\n  print(i)\n}";
            auto result = interpreter.Execute(code);
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << std::endl;
                return 1;
            }
            std::cout << "✓ Continue works\n" << std::endl;
        }

        // Test 5: Loop statement
        {
            std::cout << "Test 5: Loop Statement (infinite loop with break)" << std::endl;
            std::string code = "let counter = 0\nloop {\n  if (counter == 3) {\n    break\n  }\n  print(counter)\n  counter = counter + 1\n}";
            auto result = interpreter.Execute(code);
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << std::endl;
                return 1;
            }
            std::cout << "✓ Loop statement works\n" << std::endl;
        }

        // Test 6: Logging functions
        {
            std::cout << "Test 6: Logging Functions" << std::endl;
            std::string code = "log(\"This is a log\")\nwarn(\"This is a warning\")\nerror(\"This is an error\")";
            auto result = interpreter.Execute(code);
            if (std::holds_alternative<havel::HavelRuntimeError>(result)) {
                std::cerr << "Error: " << std::get<havel::HavelRuntimeError>(result).what() << std::endl;
                return 1;
            }
            std::cout << "✓ Logging functions work\n" << std::endl;
        }

        std::cout << "\n=== All Tests Passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
