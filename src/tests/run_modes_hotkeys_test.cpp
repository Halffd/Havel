#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    try {
        havel::IO io;
        havel::WindowManager wm;
        havel::engine::EngineConfig cfg;
        cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
        cfg.verboseOutput = false;
        cfg.enableProfiler = false;
        havel::engine::Engine engine(io, wm, cfg);

        std::cout << "=== Testing Modes, Hotkeys, and Conditional Hotkeys ===\n" << std::endl;

        // Test 1: Basic modes definition
        {
            std::cout << "--- Test 1: Basic Modes Definition ---" << std::endl;
            std::string code = R"havel(
modes {
    normal: true,
    gaming: false,
    work: false
}

print "Current mode: ${__current_mode__}"
print "All modes: ${__modes__}"
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 2: Mode switching with on/off statements
        {
            std::cout << "--- Test 2: Mode Switching ---" << std::endl;
            std::string code = R"havel(
modes {
    normal: true,
    gaming: false
}

print "Initial mode: ${__current_mode__}"

// Switch to gaming mode
__previous_mode__ = __current_mode__
__current_mode__ = "gaming"
print "Switched to: ${__current_mode__}"
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 3: On mode statement
        {
            std::cout << "--- Test 3: On Mode Statement ---" << std::endl;
            std::string code = R"havel(
modes {
    normal: true,
    gaming: false
}

__current_mode__ = "gaming"

on mode gaming {
    print "Gaming mode is active!"
}

on mode normal {
    print "Normal mode is active"
} else {
    print "Normal mode is NOT active"
}
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 4: Off mode statement
        {
            std::cout << "--- Test 4: Off Mode Statement ---" << std::endl;
            std::string code = R"havel(
modes {
    normal: true,
    gaming: false
}

__previous_mode__ = "gaming"
__current_mode__ = "normal"

off mode gaming {
    print "Left gaming mode, now in normal mode"
}

off mode work {
    print "This should not execute"
}
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 5: Basic hotkey binding
        {
            std::cout << "--- Test 5: Basic Hotkey Binding ---" << std::endl;
            std::string code = R"havel(
let hotkey_pressed = false

F1 => {
    hotkey_pressed = true
    print "F1 was pressed!"
}

log "Hotkey registered (F1)"
log "hotkey_pressed ="
log hotkey_pressed
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << "Note: Hotkey is registered but won't trigger without actual key press" << std::endl;
            std::cout << std::endl;
        }

        // Test 6: Mode-aware configuration
        {
            std::cout << "--- Test 6: Mode-Aware Configuration ---" << std::endl;
            std::string code = R"havel(
modes {
    gaming: {
        class: ["steam", "lutris", "wine"],
        title: [".*game.*"],
        sensitivity: 2.0
    },
    work: {
        class: ["code", "sublime", "vim"],
        title: [".*\\.rs", ".*\\.cpp"],
        sensitivity: 1.0
    }
}

// Access mode-specific config
let gaming_class = __mode_gaming_class
let gaming_sensitivity = __mode_gaming_sensitivity
let work_class = __mode_work_class

print "Gaming apps: ${gaming_class}"
print "Gaming sensitivity: ${gaming_sensitivity}"
print "Work apps: ${work_class}"
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 7: Multiple hotkeys with mode context
        {
            std::cout << "--- Test 7: Multiple Hotkeys ---" << std::endl;
            std::string code = R"havel(
modes {
    normal: true,
    gaming: false
}

let action_log = []

F1 => {
    action_log = [1]
    print "F1 pressed"
}

F2 => {
    action_log = [2]
    print "F2 pressed"
}

^+a => {
    action_log = [3]
    print "Ctrl+Shift+A pressed"
}

print "Registered 3 hotkeys: F1, F2, Ctrl+Shift+A"
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 8: Conditional mode execution with variables
        {
            std::cout << "--- Test 8: Conditional Mode Execution ---" << std::endl;
            std::string code = R"havel(
modes {
    dev: true,
    prod: false
}

let debug_enabled = false

on mode dev {
    debug_enabled = true
    print "Debug enabled in dev mode"
}

on mode prod {
    debug_enabled = false
    print "Debug disabled in prod mode"
}

print "Debug status: ${debug_enabled}"
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 9: Mode switching workflow simulation
        {
            std::cout << "--- Test 9: Mode Switching Workflow ---" << std::endl;
            std::string code = R"havel(
modes {
    idle: true,
    focus: false,
    break: false
}

fn switch_mode(new_mode) {
    __previous_mode__ = __current_mode__
    __current_mode__ = new_mode
    print "Switched from ${__previous_mode__} to ${new_mode}"
}

print "Initial: ${__current_mode__}"
switch_mode("focus")
switch_mode("break")
switch_mode("idle")
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        // Test 10: Complex mode with nested objects
        {
            std::cout << "--- Test 10: Complex Mode Configuration ---" << std::endl;
            std::string code = R"havel(
modes {
    gaming: {
        class: ["steam", "lutris"],
        title: [".*Counter.*", ".*Dota.*"],
        config: {
            dpi: 1600,
            polling_rate: 1000
        },
        hotkeys: ["F1", "F2", "F3"]
    }
}

print "Gaming mode config:"
print __modes__["gaming"]
)havel";
            std::cout << "Code: " << code << std::endl;
            engine.ExecuteCode(code);
            std::cout << std::endl;
        }

        std::cout << "=== All Modes and Hotkeys Tests Complete! ===\n" << std::endl;
        
        // Brief sleep to allow hotkey registration to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
