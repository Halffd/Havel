// example_wm.cpp
// Example: Embedding Havel in a window manager

#include "../include/Havel.hpp"
#include <iostream>
#include <string>
#include <map>

// ============================================================================
// Window Manager state
// ============================================================================

struct WMWindow {
    int id;
    std::string title;
    std::string appClass;
    int x = 0, y = 0;
    int width = 800, height = 600;
    bool focused = false;
    bool floating = false;
};

struct WMState {
    std::map<int, WMWindow> windows;
    int focusedWindowId = -1;
    int currentWorkspace = 1;
    std::string currentMode = "default";
    
    havel::VM vm;
    
    WMState() {
        initVM();
    }
    
    void initVM() {
        vm.setHostContext(this);
        registerFunctions();
        loadScripts();
    }
    
    void registerFunctions() {
        // Window management
        vm.registerFn("getWindows", [this](havel::VM& vm, auto& args) {
            // Return list of window IDs
            vm.load("_wins = []");
            auto wins = vm.getGlobal("_wins");
            havel::Array arr(wins);
            for (const auto& [id, win] : windows) {
                arr.push(havel::Value(static_cast<double>(id)));
            }
            return wins;
        });
        
        vm.registerFn("getFocusedWindow", [this](havel::VM& vm, auto& args) {
            if (focusedWindowId < 0) return havel::Value();
            
            auto it = windows.find(focusedWindowId);
            if (it == windows.end()) return havel::Value();
            
            // Return window info as object
            std::ostringstream oss;
            oss << "_win = {id = " << it->second.id
                << ", title = \"" << it->second.title << "\""
                << ", class = \"" << it->second.appClass << "\""
                << ", x = " << it->second.x
                << ", y = " << it->second.y
                << ", width = " << it->second.width
                << ", height = " << it->second.height
                << ", floating = " << (it->second.floating ? "true" : "false")
                << "}";
            vm.load(oss.str());
            return vm.getGlobal("_win");
        });
        
        vm.registerFn("focusWindow", [this](havel::VM& vm, auto& args) {
            if (args.empty()) return havel::Value();
            
            int id = static_cast<int>(args[0].asNumber());
            if (windows.find(id) != windows.end()) {
                focusedWindowId = id;
                for (auto& [wid, win] : windows) {
                    win.focused = (wid == id);
                }
                std::cout << "Focused window " << id << std::endl;
            }
            return havel::Value();
        });
        
        vm.registerFn("closeWindow", [this](havel::VM& vm, auto& args) {
            if (args.empty()) return havel::Value();
            
            int id = static_cast<int>(args[0].asNumber());
            windows.erase(id);
            std::cout << "Closed window " << id << std::endl;
            return havel::Value();
        });
        
        vm.registerFn("moveWindow", [this](havel::VM& vm, auto& args) {
            if (args.size() < 3) return havel::Value();
            
            int id = static_cast<int>(args[0].asNumber());
            if (windows.find(id) != windows.end()) {
                windows[id].x = static_cast<int>(args[1].asNumber());
                windows[id].y = static_cast<int>(args[2].asNumber());
            }
            return havel::Value();
        });
        
        vm.registerFn("resizeWindow", [this](havel::VM& vm, auto& args) {
            if (args.size() < 3) return havel::Value();
            
            int id = static_cast<int>(args[0].asNumber());
            if (windows.find(id) != windows.end()) {
                windows[id].width = static_cast<int>(args[1].asNumber());
                windows[id].height = static_cast<int>(args[2].asNumber());
            }
            return havel::Value();
        });
        
        vm.registerFn("toggleFloating", [this](havel::VM& vm, auto& args) {
            if (focusedWindowId < 0) return havel::Value();
            
            auto it = windows.find(focusedWindowId);
            if (it != windows.end()) {
                it->second.floating = !it->second.floating;
                std::cout << "Window " << focusedWindowId 
                          << " floating: " << it->second.floating << std::endl;
            }
            return havel::Value();
        });
        
        // Workspace management
        vm.registerFn("getWorkspace", [this](havel::VM& vm, auto& args) {
            return havel::Value(static_cast<double>(currentWorkspace));
        });
        
        vm.registerFn("setWorkspace", [this](havel::VM& vm, auto& args) {
            if (!args.empty()) {
                currentWorkspace = static_cast<int>(args[0].asNumber());
                std::cout << "Switched to workspace " << currentWorkspace << std::endl;
            }
            return havel::Value();
        });
        
        // Mode management
        vm.registerFn("getMode", [this](havel::VM& vm, auto& args) {
            return havel::Value(currentMode);
        });
        
        vm.registerFn("setMode", [this](havel::VM& vm, auto& args) {
            if (!args.empty()) {
                currentMode = args[0].asString();
                std::cout << "Mode: " << currentMode << std::endl;
            }
            return havel::Value();
        });
        
        // Utility
        vm.registerFn("print", [this](havel::VM& vm, auto& args) {
            for (const auto& arg : args) {
                std::cout << arg.toString() << " ";
            }
            std::cout << std::endl;
            return havel::Value();
        });
        
        vm.registerFn("spawn", [this](havel::VM& vm, auto& args) {
            if (args.empty()) return havel::Value();
            
            std::string cmd = args[0].asString();
            std::cout << "Spawning: " << cmd << std::endl;
            // In real WM: system(cmd.c_str()) or fork/exec
            return havel::Value();
        });
    }
    
    void loadScripts() {
        // Load WM configuration scripts
        vm.load(R"(
            // Keybindings
            fn onModEnter() {
                print("Mod+Enter pressed")
                spawn("alacritty")
            }
            
            fn onModQ() {
                print("Mod+Q pressed - closing window")
                let win = getFocusedWindow()
                if (win) {
                    closeWindow(win.id)
                }
            }
            
            fn onModF() {
                print("Mod+F pressed - toggling floating")
                toggleFloating()
            }
            
            fn onModJ() {
                print("Mod+J - focus next")
                // Focus next window logic
            }
            
            fn onModK() {
                print("Mod+K - focus previous")
                // Focus previous window logic
            }
            
            // Window rules
            fn onWindowCreated(win) {
                print("Window created: " + win.title)
                
                // Example: float certain windows
                if (win.class == "floatme") {
                    toggleFloating()
                }
            }
        )");
    }
    
    void handleKeyPress(const std::string& keyCombo) {
        // Call appropriate handler based on key combo
        std::string handler;
        
        if (keyCombo == "Mod+Enter") handler = "onModEnter";
        else if (keyCombo == "Mod+q") handler = "onModQ";
        else if (keyCombo == "Mod+f") handler = "onModF";
        else if (keyCombo == "Mod+j") handler = "onModJ";
        else if (keyCombo == "Mod+k") handler = "onModK";
        
        if (!handler.empty()) {
            auto result = vm.call(handler);
            if (!result) {
                std::cerr << "Script error: " << result.error << std::endl;
            }
        }
    }
    
    void onWindowCreated(int id, const std::string& title, const std::string& appClass) {
        windows[id] = WMWindow{id, title, appClass};
        
        // Notify script
        vm.load("_newWin = {id = " + std::to_string(id) + 
                ", title = \"" + title + "\"" +
                ", class = \"" + appClass + "\"}");
        vm.call("onWindowCreated", {vm.getGlobal("_newWin")});
    }
};

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== Havel Window Manager Integration Example ===" << std::endl;
    
    WMState wm;
    
    // Simulate window creation
    wm.onWindowCreated(1, "Alacritty", "Terminal");
    wm.onWindowCreated(2, "Firefox", "Browser");
    wm.onWindowCreated(3, "VSCode", "Editor");
    
    // Focus first window
    wm.handleKeyPress("Mod+j");
    
    // Simulate key presses
    std::cout << "\n--- Simulating key presses ---" << std::endl;
    wm.handleKeyPress("Mod+Enter");  // Spawn terminal
    wm.handleKeyPress("Mod+q");      // Close window
    wm.handleKeyPress("Mod+f");      // Toggle floating
    
    std::cout << "\n=== WM Example Complete ===" << std::endl;
    
    return 0;
}
