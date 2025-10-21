#include "havel-lang/runtime/Engine.h"
#include "core/IO.hpp"
#include "window/WindowManager.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <functional>

// Test function type
using TestFunc = std::function<int()>;

// Test registry
static std::map<std::string, TestFunc> tests;

// Helper to register tests
#define REGISTER_TEST(name) \
    static int test_##name(); \
    static bool registered_##name = (tests[#name] = test_##name, true); \
    static int test_##name()

// ===================================================================
// INTERPOLATION TESTS
// ===================================================================

REGISTER_TEST(interpolation_basic) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Interpolation: Basic ${} ---" << std::endl;
    std::string code = R"havel(
let name = "Havel"
let res = "Hello, ${name}!"
print(res)
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(interpolation_bash_style) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Interpolation: Bash-style $var ---" << std::endl;
    std::string code = R"havel(
let name = "Havel"
print "Hello, $name!"
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// ARRAY TESTS
// ===================================================================

REGISTER_TEST(array_basic) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Array: Basic operations ---" << std::endl;
    std::string code = R"havel(
let arr = [1, 2, 3, 4, 5]
print arr
print arr[0]
print arr[4]
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(array_map) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Array: map function ---" << std::endl;
    std::string code = R"havel(
let arr = [1, 2, 3, 4, 5]
fn double(x) { return x * 2 }
let doubled = map(arr, double)
print doubled
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(array_filter) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Array: filter function ---" << std::endl;
    std::string code = R"havel(
let arr = [1, 2, 3, 4, 5, 6]
fn is_even(x) { return x % 2 == 0 }
let evens = filter(arr, is_even)
print evens
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(array_join) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Array: join ---" << std::endl;
    std::string code = R"havel(
let arr = ["hello", "world", "test"]
print join(arr, " ")
print join(arr, ", ")
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// STRING TESTS
// ===================================================================

REGISTER_TEST(string_split) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- String: split ---" << std::endl;
    std::string code = R"havel(
let text = "hello,world,test"
let parts = split(text, ",")
print parts
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(string_methods) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- String: upper/lower/trim ---" << std::endl;
    std::string code = R"havel(
let text = "  Hello World  "
print upper(text)
print lower(text)
print trim(text)
print length(text)
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// CONTROL FLOW TESTS
// ===================================================================

REGISTER_TEST(control_flow_if) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Control Flow: if/else ---" << std::endl;
    std::string code = R"havel(
let x = 10
if (x > 5) {
    print "x is greater than 5"
} else {
    print "x is not greater than 5"
}
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(control_flow_loop) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Control Flow: for loop ---" << std::endl;
    std::string code = R"havel(
let arr = [1, 2, 3]
for item in arr {
    print item
}
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// MODES TESTS
// ===================================================================

REGISTER_TEST(modes_basic) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Modes: Basic definition ---" << std::endl;
    std::string code = R"havel(
modes {
    normal: true,
    gaming: false
}
print "Current mode: ${__current_mode__}"
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(modes_conditional) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Modes: Conditional execution ---" << std::endl;
    std::string code = R"havel(
modes {
    gaming: false
}
__current_mode__ = "gaming"

on mode gaming {
    print "Gaming mode active!"
}
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// HOTKEY TESTS
// ===================================================================

REGISTER_TEST(hotkey_basic) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Hotkey: Basic binding ---" << std::endl;
    std::string code = R"havel(
F1 => { print "F1 pressed" }
log "Hotkey registered"
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// PIPELINE TESTS
// ===================================================================

REGISTER_TEST(pipeline_basic) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Pipeline: Basic piping ---" << std::endl;
    std::string code = R"havel(
let text = "hello world"
let result = text | upper
print result
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// BUILTIN FUNCTIONS TESTS
// ===================================================================

REGISTER_TEST(builtin_debug) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Builtin: debug ---" << std::endl;
    std::string code = R"havel(
debug = true
debug.print "Debug message 1"
debug.print "Debug message 2"
debug = false
debug.print "This should not print"
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(builtin_io) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Builtin: IO functions ---" << std::endl;
    std::string code = R"havel(
io.block()
io.unblock()
io.grab()
io.ungrab()
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(builtin_brightness) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Builtin: Brightness Manager ---" << std::endl;
    std::string code = R"havel(
let brightness = brightnessManager.getBrightness()
print "Current brightness: ${brightness}"
brightnessManager.setBrightness(0.8)
brightnessManager.increaseBrightness(0.1)
brightnessManager.decreaseBrightness(0.05)
)havel";
    engine.ExecuteCode(code);
    return 0;
}

REGISTER_TEST(builtin_window) {
    havel::IO io;
    havel::WindowManager wm;
    havel::engine::EngineConfig cfg;
    cfg.mode = havel::engine::ExecutionMode::INTERPRETER;
    cfg.verboseOutput = false;
    havel::engine::Engine engine(io, wm, cfg);
    
    std::cout << "--- Builtin: Window functions ---" << std::endl;
    std::string code = R"havel(
let title = window.getTitle()
print "Active window: ${title}"
)havel";
    engine.ExecuteCode(code);
    return 0;
}

// ===================================================================
// MAIN
// ===================================================================

int main(int argc, char* argv[]) {
    std::cout << "=== Havel Language Test Suite ===\n" << std::endl;
    
    // Parse command line arguments
    std::vector<std::string> testNames;
    bool listTests = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list" || arg == "-l") {
            listTests = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options] [test_names...]\n";
            std::cout << "Options:\n";
            std::cout << "  --list, -l     List all available tests\n";
            std::cout << "  --help, -h     Show this help message\n";
            std::cout << "\nIf no test names provided, all tests will run.\n";
            return 0;
        } else {
            testNames.push_back(arg);
        }
    }
    
    // List tests if requested
    if (listTests) {
        std::cout << "Available tests:" << std::endl;
        for (const auto& [name, _] : tests) {
            std::cout << "  " << name << std::endl;
        }
        return 0;
    }
    
    // Determine which tests to run
    std::vector<std::string> testsToRun;
    if (testNames.empty()) {
        // Run all tests
        for (const auto& [name, _] : tests) {
            testsToRun.push_back(name);
        }
    } else {
        testsToRun = testNames;
    }
    
    // Run tests
    int passed = 0;
    int failed = 0;
    
    for (const auto& testName : testsToRun) {
        auto it = tests.find(testName);
        if (it == tests.end()) {
            std::cerr << "❌ Test not found: " << testName << std::endl;
            failed++;
            continue;
        }
        
        try {
            std::cout << "\n=== Running: " << testName << " ===" << std::endl;
            int result = it->second();
            if (result == 0) {
                std::cout << "✅ PASS: " << testName << std::endl;
                passed++;
            } else {
                std::cout << "❌ FAIL: " << testName << " (exit code: " << result << ")" << std::endl;
                failed++;
            }
        } catch (const std::exception& e) {
            std::cout << "❌ EXCEPTION in " << testName << ": " << e.what() << std::endl;
            failed++;
        }
    }
    
    // Summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;
    
    return failed > 0 ? 1 : 0;
}
