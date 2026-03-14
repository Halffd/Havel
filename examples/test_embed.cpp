// test_embed.cpp
// Test for Havel embedding API

#include "../include/Havel.hpp"
#include <iostream>
#include <cassert>

void test_basic() {
    std::cout << "Test: Basic VM operations..." << std::endl;
    
    havel::VM vm;
    
    // Test load
    auto result = vm.load("let x = 42");
    assert(result.ok);
    
    // Test getGlobal
    auto x = vm.getGlobal("x");
    assert(x.isNumber());
    assert(x.asNumber() == 42.0);
    
    // Test setGlobal
    vm.setGlobal("y", havel::Value(100));
    auto y = vm.getGlobal("y");
    assert(y.isNumber());
    assert(y.asNumber() == 100.0);
    
    std::cout << "  PASSED" << std::endl;
}

void test_function_call() {
    std::cout << "Test: Function calling..." << std::endl;
    
    havel::VM vm;
    
    // Define function
    auto result = vm.load("fn add(a, b) { return a + b }");
    assert(result.ok);
    
    // Call function
    std::vector<havel::Value> args = {havel::Value(5), havel::Value(3)};
    auto callResult = vm.call(std::string("add"), args);
    assert(callResult.ok);
    assert(callResult->isNumber());
    assert(callResult->asNumber() == 8.0);
    
    std::cout << "  PASSED" << std::endl;
}

void test_native_function() {
    std::cout << "Test: Native function registration..." << std::endl;
    
    havel::VM vm;
    
    // Register native function
    vm.registerFn("double", [](havel::VM& vm, const std::vector<havel::Value>& args) {
        if (args.empty()) return havel::Value();
        return havel::Value(args[0].asNumber() * 2);
    });
    
    // Call native function
    std::vector<havel::Value> args = {havel::Value(21)};
    auto result = vm.call(std::string("double"), args);
    assert(result.ok);
    assert(result->isNumber());
    assert(result->asNumber() == 42.0);
    
    std::cout << "  PASSED" << std::endl;
}

void test_strings() {
    std::cout << "Test: String operations..." << std::endl;
    
    havel::VM vm;
    
    auto result = vm.load(R"(
        let greeting = "Hello, " + "World!"
    )");
    assert(result.ok);
    
    auto greeting = vm.getGlobal("greeting");
    assert(greeting.isString());
    assert(greeting.asString() == "Hello, World!");
    
    std::cout << "  PASSED" << std::endl;
}

void test_arrays() {
    std::cout << "Test: Arrays..." << std::endl;
    
    havel::VM vm;
    
    auto result = vm.load("let arr = [1, 2, 3, 4, 5]");
    assert(result.ok);
    
    havel::Array arr(vm.getGlobal("arr"));
    assert(arr.size() == 5);
    assert(arr.get(0).asNumber() == 1.0);
    assert(arr.get(4).asNumber() == 5.0);
    
    arr.push(havel::Value(6));
    assert(arr.size() == 6);
    
    std::cout << "  PASSED" << std::endl;
}

void test_objects() {
    std::cout << "Test: Objects..." << std::endl;
    
    havel::VM vm;
    
    auto result = vm.load(R"(
        let player = {
            name = "Hero"
            hp = 100
            pos = {x = 10, y = 20}
        }
    )");
    assert(result.ok);
    
    havel::Object player(vm.getGlobal("player"));
    assert(player.has("name"));
    assert(player.get("name").asString() == "Hero");
    assert(player.get("hp").asNumber() == 100.0);
    
    player.set("hp", havel::Value(80));
    assert(player.get("hp").asNumber() == 80.0);
    
    std::cout << "  PASSED" << std::endl;
}

void test_structs() {
    std::cout << "Test: Structs..." << std::endl;
    
    havel::VM vm;
    
    auto result = vm.load("struct Vec2 { x y init(x,y) { this.x=x this.y=y } toString() { return \"Vec2(\"+x+\",\"+y+\")\" } } let v = Vec2(3, 4)");
    assert(result.ok);
    
    havel::Struct v(vm.getGlobal("v"));
    assert(v.getType() == "Vec2");
    assert(v.getField("x").asNumber() == 3.0);
    assert(v.getField("y").asNumber() == 4.0);
    
    std::cout << "  PASSED" << std::endl;
}

void test_operators() {
    std::cout << "Test: Operator overloading..." << std::endl;
    
    havel::VM vm;
    
    auto result = vm.load(R"(
        struct Vec2 {
            x
            y
            
            init(x, y) {
                this.x = x
                this.y = y
            }
            
            op +(other) {
                return Vec2(x + other.x, y + other.y)
            }
        }
        
        let v1 = Vec2(1, 2)
        let v2 = Vec2(3, 4)
        let v3 = v1 + v2
    )");
    assert(result.ok);
    
    havel::Struct v3(vm.getGlobal("v3"));
    assert(v3.getField("x").asNumber() == 4.0);
    assert(v3.getField("y").asNumber() == 6.0);
    
    std::cout << "  PASSED" << std::endl;
}

void test_error_handling() {
    std::cout << "Test: Error handling..." << std::endl;
    
    havel::VM vm;
    
    // Test syntax error
    auto result = vm.load("let x = ");
    assert(!result.ok);
    assert(!result.error.empty());
    
    // Test runtime error
    result = vm.load("let y = undefinedVariable");
    assert(!result.ok);
    
    // Test function not found
    std::vector<havel::Value> noArgs;
    auto callResult = vm.call(std::string("nonexistent"), noArgs);
    assert(!callResult.ok);
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "=== Havel Embedding API Tests ===" << std::endl;
    std::cout << std::endl;
    
    test_basic();
    test_function_call();
    test_native_function();
    test_strings();
    test_arrays();
    test_objects();
    test_structs();
    test_operators();
    test_error_handling();
    
    std::cout << std::endl;
    std::cout << "=== All tests PASSED! ===" << std::endl;
    
    return 0;
}
