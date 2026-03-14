// example_embed.cpp
// Example: Embedding Havel in a C++ application

#include "../include/Havel.hpp"
#include <iostream>
#include <cmath>

int main() {
    using namespace havel;
    
    // Create VM instance
    VM vm;
    
    // =========================================================================
    // 1. Register native functions
    // =========================================================================
    
    // Simple function
    vm.registerFn("print", [](VM& vm, const std::vector<Value>& args) {
        for (const auto& arg : args) {
            std::cout << arg.toString() << " ";
        }
        std::cout << std::endl;
        return Value();
    });
    
    // Math function
    vm.registerFn("sqrt", [](VM& vm, const std::vector<Value>& args) {
        if (args.empty()) return Value();
        return Value(std::sqrt(args[0].asNumber()));
    });
    
    // =========================================================================
    // 2. Load Havel code
    // =========================================================================
    
    auto result = vm.load(R"(
        // Define a struct with operators
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
            
            op -(other) {
                return Vec2(x - other.x, y - other.y)
            }
            
            op *(scalar) {
                return Vec2(x * scalar, y * scalar)
            }
            
            toString() {
                return "Vec2(" + x + ", " + y + ")"
            }
        }
        
        // Helper function
        fn distance(v) {
            return sqrt(v.x * v.x + v.y * v.y)
        }
        
        // Test code
        let v1 = Vec2(3, 4)
        let v2 = Vec2(1, 2)
        
        let v3 = v1 + v2
        print("v1 + v2 = " + v3.toString())
        
        let v4 = v1 - v2
        print("v1 - v2 = " + v4.toString())
        
        let v5 = v1 * 2
        print("v1 * 2 = " + v5.toString())
        
        print("Distance: " + distance(v1))
    )", "example.hv");
    
    if (!result) {
        std::cerr << "Error: " << result.error << std::endl;
        return 1;
    }
    
    // =========================================================================
    // 3. Call Havel functions from C++
    // =========================================================================
    
    // Call distance function
    auto v1 = vm.getGlobal("v1");
    auto distFn = vm.getGlobal("distance");
    
    auto distResult = vm.call(distFn, {v1});
    if (distResult) {
        std::cout << "Distance from C++: " << distResult->asNumber() << std::endl;
    }
    
    // =========================================================================
    // 4. Work with arrays and objects
    // =========================================================================
    
    result = vm.load(R"(
        let numbers = [1, 2, 3, 4, 5]
        let player = {name = "Hero", hp = 100, pos = Vec2(10, 20)}
    )");
    
    Array numbers(vm.getGlobal("numbers"));
    std::cout << "Array size: " << numbers.size() << std::endl;
    std::cout << "First element: " << numbers.get(0).asNumber() << std::endl;
    
    Object player(vm.getGlobal("player"));
    std::cout << "Player name: " << player.get("name").asString() << std::endl;
    std::cout << "Player HP: " << player.get("hp").asNumber() << std::endl;
    
    // =========================================================================
    // 5. Error handling
    // =========================================================================
    
    result = vm.load(R"(
        let bad = thisWillFail
    )");
    
    if (!result) {
        std::cout << "Caught error: " << result.error << std::endl;
    }
    
    // =========================================================================
    // 6. Host context (for game/window manager integration)
    // =========================================================================
    
    struct GameContext {
        int score = 0;
        std::string level = "1";
    };
    
    GameContext ctx;
    vm.setHostContext(&ctx);
    
    vm.registerFn("getScore", [&ctx](VM& vm, const std::vector<Value>& args) {
        return Value(static_cast<double>(ctx.score));
    });
    
    vm.registerFn("setScore", [&ctx](VM& vm, const std::vector<Value>& args) {
        if (!args.empty()) {
            ctx.score = static_cast<int>(args[0].asNumber());
        }
        return Value();
    });
    
    result = vm.load(R"(
        print("Score: " + getScore())
        setScore(100)
        print("New score: " + getScore())
    )");
    
    std::cout << "Final C++ score: " << ctx.score << std::endl;
    
    return 0;
}
