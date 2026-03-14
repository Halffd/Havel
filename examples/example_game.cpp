// example_game.cpp
// Example: Embedding Havel in a game

#include "../include/Havel.hpp"
#include <iostream>
#include <vector>
#include <string>

// ============================================================================
// Game state
// ============================================================================

struct Entity {
    std::string name;
    double x = 0.0;
    double y = 0.0;
    int hp = 100;
    int score = 0;
};

struct GameState {
    std::vector<Entity> entities;
    int level = 1;
    bool running = true;
    
    havel::VM vm;
    
    GameState() {
        // Initialize VM
        initVM();
    }
    
    void initVM() {
        // Set host context
        vm.setHostContext(this);
        
        // Register game functions
        registerFunctions();
        
        // Load game scripts
        loadScripts();
    }
    
    void registerFunctions() {
        // Entity management
        vm.registerFn("createEntity", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            if (args.size() < 1) return havel::Value();
            
            Entity entity;
            entity.name = args[0].asString();
            entity.x = args.size() > 1 ? args[1].asNumber() : 0.0;
            entity.y = args.size() > 2 ? args[2].asNumber() : 0.0;
            entity.hp = args.size() > 3 ? static_cast<int>(args[3].asNumber()) : 100;
            
            entities.push_back(entity);
            
            // Return entity index
            return havel::Value(static_cast<double>(entities.size() - 1));
        });
        
        vm.registerFn("getEntity", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            if (args.empty()) return havel::Value();
            
            size_t idx = static_cast<size_t>(args[0].asNumber());
            if (idx >= entities.size()) return havel::Value();
            
            // Return entity as object
            vm.load("_ent = {name = \"" + entities[idx].name + 
                    "\", x = " + std::to_string(entities[idx].x) +
                    ", y = " + std::to_string(entities[idx].y) +
                    ", hp = " + std::to_string(entities[idx].hp) + "}");
            return vm.getGlobal("_ent");
        });
        
        vm.registerFn("setEntityPos", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            if (args.size() < 3) return havel::Value();
            
            size_t idx = static_cast<size_t>(args[0].asNumber());
            if (idx >= entities.size()) return havel::Value();
            
            entities[idx].x = args[1].asNumber();
            entities[idx].y = args[2].asNumber();
            
            return havel::Value();
        });
        
        vm.registerFn("damageEntity", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            if (args.size() < 2) return havel::Value();
            
            size_t idx = static_cast<size_t>(args[0].asNumber());
            int damage = static_cast<int>(args[1].asNumber());
            
            if (idx >= entities.size()) return havel::Value();
            
            entities[idx].hp -= damage;
            
            // Check if dead
            if (entities[idx].hp <= 0) {
                std::cout << entities[idx].name << " died!" << std::endl;
            }
            
            return havel::Value();
        });
        
        // Game state
        vm.registerFn("getLevel", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            return havel::Value(static_cast<double>(level));
        });
        
        vm.registerFn("setLevel", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            if (!args.empty()) {
                level = static_cast<int>(args[0].asNumber());
            }
            return havel::Value();
        });
        
        vm.registerFn("endGame", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            running = false;
            return havel::Value();
        });
        
        // Utility
        vm.registerFn("print", [this](havel::VM& vm, const std::vector<havel::Value>& args) {
            for (const auto& arg : args) {
                std::cout << arg.toString() << " ";
            }
            std::cout << std::endl;
            return havel::Value();
        });
    }
    
    void loadScripts() {
        // Load game logic scripts
        vm.load(R"(
            // Game update function
            fn gameUpdate() {
                print("Level: " + getLevel())
                print("Entities: " + getEntityCount())
                
                // Example: damage first entity
                if (getEntityCount() > 0) {
                    damageEntity(0, 10)
                }
            }
            
            fn getEntityCount() {
                return 0  // Placeholder
            }
        )");
    }
    
    void update() {
        // Call game update
        auto result = vm.call("gameUpdate");
        if (!result) {
            std::cerr << "Script error: " << result.error << std::endl;
        }
    }
};

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== Havel Game Integration Example ===" << std::endl;
    
    GameState game;
    
    // Create some entities via script
    game.vm.load(R"(
        let player = createEntity("Hero", 100, 100, 100)
        let enemy = createEntity("Goblin", 150, 150, 50)
        print("Created player and enemy")
    )");
    
    // Game loop
    int frame = 0;
    while (game.running && frame < 5) {
        std::cout << "\n--- Frame " << frame << " ---" << std::endl;
        game.update();
        frame++;
    }
    
    std::cout << "\n=== Game Over ===" << std::endl;
    
    return 0;
}
