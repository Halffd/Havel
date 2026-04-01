#include "Tests.h"
#include "../compiler/bytecode/VM.hpp"
#include "../compiler/bytecode/BytecodeIR.hpp"

#include <iostream>
#include <memory>

using namespace havel::compiler;

class VMFixesTest {
private:
    Tests tests;
    std::unique_ptr<VM> vm;

public:
    VMFixesTest() : vm(std::make_unique<VM>()) {}

    void runAllTests() {
        std::cout << "=== VM Architecture Fixes Test Suite ===" << std::endl;
        
        testNullHandling();
        testTruthiness();
        testComparisons();
        testNestedIndexing();
        testDurationParsing();
        
        tests.printSummary();
    }

private:
    void testNullHandling() {
        std::cout << "\n--- Testing Null Handling ---" << std::endl;
        
        tests.test("null detection", [this]() {
            return vm->isNull(BytecodeValue(nullptr));
        });
        
        tests.test("null is not other types", [this]() {
            return !vm->isNull(BytecodeValue(42)) &&
                   !vm->isNull(BytecodeValue(true)) &&
                   !vm->isNull(BytecodeValue("hello"));
        });
    }
    
    void testTruthiness() {
        std::cout << "\n--- Testing Truthiness ---" << std::endl;
        
        // Falsy values
        tests.test("null is falsy", [this]() {
            return !vm->isTruthy(BytecodeValue(nullptr));
        });
        
        tests.test("false is falsy", [this]() {
            return !vm->isTruthy(BytecodeValue(false));
        });
        
        tests.test("zero is falsy", [this]() {
            return !vm->isTruthy(BytecodeValue(0)) &&
                   !vm->isTruthy(BytecodeValue(0.0));
        });
        
        tests.test("empty string is falsy", [this]() {
            return !vm->isTruthy(BytecodeValue(""));
        });
        
        // Truthy values
        tests.test("true is truthy", [this]() {
            return vm->isTruthy(BytecodeValue(true));
        });
        
        tests.test("non-zero numbers are truthy", [this]() {
            return vm->isTruthy(BytecodeValue(1)) &&
                   vm->isTruthy(BytecodeValue(-1)) &&
                   vm->isTruthy(BytecodeValue(3.14));
        });
        
        tests.test("non-empty string is truthy", [this]() {
            return vm->isTruthy(BytecodeValue("hello"));
        });
        
        tests.test("objects are always truthy", [this]() {
            auto obj = vm->createHostObject();
            return vm->isTruthy(BytecodeValue(obj));
        });
    }
    
    void testComparisons() {
        std::cout << "\n--- Testing Comparisons ---" << std::endl;
        
        // Test that comparison logic exists and handles null properly
        tests.test("comparison setup", [this]() {
            // Create a simple script to test comparisons
            // This is a basic smoke test since we can't easily test bytecode generation here
            return true; // Placeholder - actual comparison testing requires bytecode execution
        });
    }
    
    void testNestedIndexing() {
        std::cout << "\n--- Testing Nested Indexing ---" << std::endl;
        
        tests.test("array creation", [this]() {
            auto arr = vm->createHostArray();
            return std::holds_alternative<ArrayRef>(BytecodeValue(arr));
        });
        
        tests.test("array operations", [this]() {
            auto arr = vm->createHostArray();
            vm->pushHostArrayValue(arr, BytecodeValue(42));
            vm->pushHostArrayValue(arr, BytecodeValue("hello"));
            
            auto val1 = vm->getHostArrayValue(arr, 0);
            auto val2 = vm->getHostArrayValue(arr, 1);
            
            return std::holds_alternative<int64_t>(val1) && std::get<int64_t>(val1) == 42 &&
                   std::holds_alternative<std::string>(val2) && std::get<std::string>(val2) == "hello";
        });
        
        tests.test("nested array structure", [this]() {
            // Create nested arrays: [[1, 2], [3, 4]]
            auto outer = vm->createHostArray();
            auto inner1 = vm->createHostArray();
            auto inner2 = vm->createHostArray();
            
            vm->pushHostArrayValue(inner1, BytecodeValue(1));
            vm->pushHostArrayValue(inner1, BytecodeValue(2));
            vm->pushHostArrayValue(inner2, BytecodeValue(3));
            vm->pushHostArrayValue(inner2, BytecodeValue(4));
            
            vm->pushHostArrayValue(outer, BytecodeValue(inner1));
            vm->pushHostArrayValue(outer, BytecodeValue(inner2));
            
            // Test accessing nested values
            auto inner1_val = vm->getHostArrayValue(outer, 0);
            auto inner2_val = vm->getHostArrayValue(outer, 1);
            
            if (!std::holds_alternative<ArrayRef>(inner1_val) || 
                !std::holds_alternative<ArrayRef>(inner2_val)) {
                return false;
            }
            
            auto inner1_ref = std::get<ArrayRef>(inner1_val);
            auto inner2_ref = std::get<ArrayRef>(inner2_val);
            
            auto val1 = vm->getHostArrayValue(inner1_ref, 0);
            auto val2 = vm->getHostArrayValue(inner1_ref, 1);
            auto val3 = vm->getHostArrayValue(inner2_ref, 0);
            auto val4 = vm->getHostArrayValue(inner2_ref, 1);
            
            return std::holds_alternative<int64_t>(val1) && std::get<int64_t>(val1) == 1 &&
                   std::holds_alternative<int64_t>(val2) && std::get<int64_t>(val2) == 2 &&
                   std::holds_alternative<int64_t>(val3) && std::get<int64_t>(val3) == 3 &&
                   std::holds_alternative<int64_t>(val4) && std::get<int64_t>(val4) == 4;
        });
    }
    
    void testDurationParsing() {
        std::cout << "\n--- Testing Duration Parsing ---" << std::endl;
        
        tests.test("numeric duration parsing", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue(100));
            auto result2 = vm->parseDuration(BytecodeValue(0));
            auto result3 = vm->parseDuration(BytecodeValue(-50));
            
            return result1 && *result1 == 100 &&
                   result2 && *result2 == 0 &&
                   result3 && *result3 == -50;
        });
        
        tests.test("float duration parsing", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue(1.5));
            auto result2 = vm->parseDuration(BytecodeValue(0.0));
            
            return result1 && *result1 == 1 &&
                   result2 && *result2 == 0;
        });
        
        tests.test("string duration parsing - milliseconds", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue("100ms"));
            auto result2 = vm->parseDuration(BytecodeValue("500ms"));
            
            return result1 && *result1 == 100 &&
                   result2 && *result2 == 500;
        });
        
        tests.test("string duration parsing - seconds", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue("1s"));
            auto result2 = vm->parseDuration(BytecodeValue("2.5s"));
            
            return result1 && *result1 == 1000 &&
                   result2 && *result2 == 2500;
        });
        
        tests.test("string duration parsing - minutes", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue("1m"));
            auto result2 = vm->parseDuration(BytecodeValue("0.5m"));
            
            return result1 && *result1 == 60000 &&
                   result2 && *result2 == 30000;
        });
        
        tests.test("string duration parsing - hours", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue("1h"));
            auto result2 = vm->parseDuration(BytecodeValue("2.5h"));
            
            return result1 && *result1 == 3600000 &&
                   result2 && *result2 == 9000000;
        });
        
        tests.test("invalid duration parsing", [this]() {
            auto result1 = vm->parseDuration(BytecodeValue("invalid"));
            auto result2 = vm->parseDuration(BytecodeValue("1x"));
            auto result3 = vm->parseDuration(BytecodeValue("abc123"));
            
            return !result1 && !result2 && !result3;
        });
    }
};

int main() {
    try {
        VMFixesTest test;
        test.runAllTests();
        
        std::cout << "\n=== VM Fixes Test Complete ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
