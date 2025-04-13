#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <functional>
#include <cassert>
#include <signal.h>
#include <cstring> // For memset
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "../core/IO.hpp"
#include "../core/DisplayManager.hpp"

// Test framework macros
#define TEST_ASSERT(condition) do { \
    if (!(condition)) { \
        std::cerr << "ASSERTION FAILED: " << #condition << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    std::cout << "Running test: " << #test_func << "... "; \
    bool result = test_func(); \
    if (result) { \
        std::cout << "PASSED" << std::endl; \
        passed_tests++; \
    } else { \
        std::cout << "FAILED" << std::endl; \
        failed_tests++; \
    } \
    total_tests++; \
} while(0)

// Global variables for test control
std::atomic<bool> running(true);
std::mutex test_mutex;
std::condition_variable test_cv;
int passed_tests = 0;
int failed_tests = 0;
int total_tests = 0;

// Signal handler for clean shutdown
void signal_handler(int signal) {
    std::cout << "Caught signal " << signal << ", exiting tests..." << std::endl;
    running = false;
    test_cv.notify_all();
}

// Mock callbacks for testing
namespace MockCallbacks {
    struct HotkeyResult {
        std::string hotkey_name;
        bool triggered = false;
        int count = 0;
    };
    
    std::mutex results_mutex;
    std::map<std::string, HotkeyResult> results;
    
    void reset() {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.clear();
    }
    
    void register_hotkey(const std::string& name) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results[name] = HotkeyResult{name};
    }
    
    std::function<void()> create_callback(const std::string& name) {
        return [name]() {
            std::lock_guard<std::mutex> lock(results_mutex);
            if (results.find(name) != results.end()) {
                results[name].triggered = true;
                results[name].count++;
                std::cout << "Hotkey triggered: " << name << " (count: " << results[name].count << ")" << std::endl;
            }
        };
    }
    
    bool was_triggered(const std::string& name) {
        std::lock_guard<std::mutex> lock(results_mutex);
        if (results.find(name) != results.end()) {
            return results[name].triggered;
        }
        return false;
    }
    
    int get_count(const std::string& name) {
        std::lock_guard<std::mutex> lock(results_mutex);
        if (results.find(name) != results.end()) {
            return results[name].count;
        }
        return 0;
    }
}

// Mock X11 event generator
class MockX11EventGenerator {
private:
    Display* display;
    Window root;
    
public:
    MockX11EventGenerator() {
        display = H::DisplayManager::GetDisplay();
        if (!display) {
            std::cerr << "Failed to get X11 display" << std::endl;
            return;
        }
        root = DefaultRootWindow(display);
    }
    
    ~MockX11EventGenerator() {
        // Don't close the display, it's managed by DisplayManager
    }
    
    // Create a key press event with the given keysym and modifiers
    XEvent create_key_press_event(KeySym keysym, unsigned int modifiers) {
        XEvent event;
        std::memset(&event, 0, sizeof(event));
        
        event.type = KeyPress;
        event.xkey.display = display;
        event.xkey.window = root;
        event.xkey.root = root;
        event.xkey.subwindow = None;
        event.xkey.time = CurrentTime;
        event.xkey.x = 1;
        event.xkey.y = 1;
        event.xkey.x_root = 1;
        event.xkey.y_root = 1;
        event.xkey.state = modifiers;
        event.xkey.keycode = XKeysymToKeycode(display, keysym);
        event.xkey.same_screen = True;
        
        return event;
    }
    
    // Simulate a key press event
    void simulate_key_press(KeySym keysym, unsigned int modifiers) {
        if (!display) return;
        
        XEvent event = create_key_press_event(keysym, modifiers);
        
        // Directly call the IO handler to simulate the event
        // This bypasses the actual X11 event queue
        H::IO::HandleKeyEvent(event);
    }
    
    // Simulate a sequence of modifier key presses followed by a regular key
    void simulate_modifier_sequence(std::vector<KeySym> modifiers, KeySym key) {
        if (!display) return;
        
        unsigned int state = 0;
        
        // Press each modifier key
        for (KeySym mod : modifiers) {
            // Update state based on modifier
            if (mod == XK_Shift_L || mod == XK_Shift_R) {
                state |= ShiftMask;
            } else if (mod == XK_Control_L || mod == XK_Control_R) {
                state |= ControlMask;
            } else if (mod == XK_Alt_L || mod == XK_Alt_R) {
                state |= Mod1Mask;
            } else if (mod == XK_Super_L || mod == XK_Super_R) {
                state |= Mod4Mask;
            }
            
            // Simulate modifier key press
            XEvent mod_event = create_key_press_event(mod, state);
            H::IO::HandleKeyEvent(mod_event);
        }
        
        // Finally press the actual key with all modifiers
        XEvent key_event = create_key_press_event(key, state);
        H::IO::HandleKeyEvent(key_event);
    }
};

// Timer utilities for testing race conditions
class TestTimer {
public:
    static void wait_for(int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
    
    static bool wait_for_condition(std::function<bool()> condition, int timeout_ms, int check_interval_ms = 10) {
        auto start_time = std::chrono::steady_clock::now();
        while (!condition()) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count() > timeout_ms) {
                return false; // Timeout
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        }
        return true; // Condition met
    }
};

// Test cases for modifier key issues

// Test basic modifier key detection
bool test_basic_modifier_detection() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register hotkeys with different modifiers
    std::string hotkey1 = "ctrl+a";
    std::string hotkey2 = "alt+b";
    std::string hotkey3 = "shift+c";
    std::string hotkey4 = "win+d";
    
    MockCallbacks::register_hotkey(hotkey1);
    MockCallbacks::register_hotkey(hotkey2);
    MockCallbacks::register_hotkey(hotkey3);
    MockCallbacks::register_hotkey(hotkey4);
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    io.Hotkey(hotkey3, MockCallbacks::create_callback(hotkey3));
    io.Hotkey(hotkey4, MockCallbacks::create_callback(hotkey4));
    
    // Create mock event generator
    MockX11EventGenerator eventGen;
    
    // Simulate key presses
    eventGen.simulate_key_press(XK_a, ControlMask);
    eventGen.simulate_key_press(XK_b, Mod1Mask);
    eventGen.simulate_key_press(XK_c, ShiftMask);
    eventGen.simulate_key_press(XK_d, Mod4Mask);
    
    // Wait for callbacks to execute
    TestTimer::wait_for(100);
    
    // Check if all hotkeys were triggered
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey1));
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey2));
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey3));
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey4));
    
    return true;
}

// Test multiple modifier keys
bool test_multiple_modifiers() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register hotkeys with multiple modifiers
    std::string hotkey1 = "ctrl+alt+a";
    std::string hotkey2 = "shift+win+b";
    std::string hotkey3 = "ctrl+shift+alt+c";
    
    MockCallbacks::register_hotkey(hotkey1);
    MockCallbacks::register_hotkey(hotkey2);
    MockCallbacks::register_hotkey(hotkey3);
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    io.Hotkey(hotkey3, MockCallbacks::create_callback(hotkey3));
    
    // Create mock event generator
    MockX11EventGenerator eventGen;
    
    // Simulate key presses with multiple modifiers
    eventGen.simulate_key_press(XK_a, ControlMask | Mod1Mask);
    eventGen.simulate_key_press(XK_b, ShiftMask | Mod4Mask);
    eventGen.simulate_key_press(XK_c, ControlMask | ShiftMask | Mod1Mask);
    
    // Wait for callbacks to execute
    TestTimer::wait_for(100);
    
    // Check if all hotkeys were triggered
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey1));
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey2));
    TEST_ASSERT(MockCallbacks::was_triggered(hotkey3));
    
    return true;
}

// Test for the issue where modifiers are being detected as single keys
bool test_modifier_detection_issue() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register a hotkey with modifiers
    std::string hotkey = "ctrl+shift+g";
    MockCallbacks::register_hotkey(hotkey);
    io.Hotkey(hotkey, MockCallbacks::create_callback(hotkey));
    
    // Also register a hotkey for just 'g' to test if it's triggered incorrectly
    std::string single_key = "g";
    MockCallbacks::register_hotkey(single_key);
    io.Hotkey(single_key, MockCallbacks::create_callback(single_key));
    
    // Create mock event generator
    MockX11EventGenerator eventGen;
    
    // Simulate pressing ctrl+shift+g
    // First, simulate the sequence of key presses
    eventGen.simulate_modifier_sequence({XK_Control_L, XK_Shift_L}, XK_g);
    
    // Wait for callbacks to execute
    TestTimer::wait_for(100);
    
    // Check which hotkey was triggered
    bool ctrl_shift_g_triggered = MockCallbacks::was_triggered(hotkey);
    bool g_triggered = MockCallbacks::was_triggered(single_key);
    
    std::cout << "ctrl+shift+g triggered: " << ctrl_shift_g_triggered << std::endl;
    std::cout << "g triggered: " << g_triggered << std::endl;
    
    // The test passes if ctrl+shift+g was triggered and g was not
    // If both were triggered or only g was triggered, there's an issue
    TEST_ASSERT(ctrl_shift_g_triggered && !g_triggered);
    
    return true;
}

// Test for the issue with Alt+key combinations
bool test_alt_key_combinations() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register Alt+key hotkeys
    std::string hotkey1 = "alt+x";
    std::string hotkey2 = "alt+left";
    std::string hotkey3 = "alt+right";
    
    MockCallbacks::register_hotkey(hotkey1);
    MockCallbacks::register_hotkey(hotkey2);
    MockCallbacks::register_hotkey(hotkey3);
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    io.Hotkey(hotkey3, MockCallbacks::create_callback(hotkey3));
    
    // Also register the single keys to check for incorrect triggering
    std::string single1 = "x";
    std::string single2 = "left";
    std::string single3 = "right";
    
    MockCallbacks::register_hotkey(single1);
    MockCallbacks::register_hotkey(single2);
    MockCallbacks::register_hotkey(single3);
    
    io.Hotkey(single1, MockCallbacks::create_callback(single1));
    io.Hotkey(single2, MockCallbacks::create_callback(single2));
    io.Hotkey(single3, MockCallbacks::create_callback(single3));
    
    // Create mock event generator
    MockX11EventGenerator eventGen;
    
    // Simulate Alt+key presses
    eventGen.simulate_key_press(XK_x, Mod1Mask);
    eventGen.simulate_key_press(XK_Left, Mod1Mask);
    eventGen.simulate_key_press(XK_Right, Mod1Mask);
    
    // Wait for callbacks to execute
    TestTimer::wait_for(100);
    
    // Check which hotkeys were triggered
    bool alt_x_triggered = MockCallbacks::was_triggered(hotkey1);
    bool alt_left_triggered = MockCallbacks::was_triggered(hotkey2);
    bool alt_right_triggered = MockCallbacks::was_triggered(hotkey3);
    
    bool x_triggered = MockCallbacks::was_triggered(single1);
    bool left_triggered = MockCallbacks::was_triggered(single2);
    bool right_triggered = MockCallbacks::was_triggered(single3);
    
    std::cout << "alt+x triggered: " << alt_x_triggered << ", x triggered: " << x_triggered << std::endl;
    std::cout << "alt+left triggered: " << alt_left_triggered << ", left triggered: " << left_triggered << std::endl;
    std::cout << "alt+right triggered: " << alt_right_triggered << ", right triggered: " << right_triggered << std::endl;
    
    // The test passes if the Alt+key combinations were triggered and the single keys were not
    TEST_ASSERT(alt_x_triggered && !x_triggered);
    TEST_ASSERT(alt_left_triggered && !left_triggered);
    TEST_ASSERT(alt_right_triggered && !right_triggered);
    
    return true;
}

// Test for the issue with Win+function key combinations
bool test_win_function_key_combinations() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register Win+function key hotkeys
    std::string hotkey1 = "win+f1";
    std::string hotkey2 = "win+f2";
    std::string hotkey3 = "win+f3";
    
    MockCallbacks::register_hotkey(hotkey1);
    MockCallbacks::register_hotkey(hotkey2);
    MockCallbacks::register_hotkey(hotkey3);
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    io.Hotkey(hotkey3, MockCallbacks::create_callback(hotkey3));
    
    // Also register the single function keys
    std::string single1 = "f1";
    std::string single2 = "f2";
    std::string single3 = "f3";
    
    MockCallbacks::register_hotkey(single1);
    MockCallbacks::register_hotkey(single2);
    MockCallbacks::register_hotkey(single3);
    
    io.Hotkey(single1, MockCallbacks::create_callback(single1));
    io.Hotkey(single2, MockCallbacks::create_callback(single2));
    io.Hotkey(single3, MockCallbacks::create_callback(single3));
    
    // Create mock event generator
    MockX11EventGenerator eventGen;
    
    // Simulate Win+function key presses
    eventGen.simulate_key_press(XK_F1, Mod4Mask);
    eventGen.simulate_key_press(XK_F2, Mod4Mask);
    eventGen.simulate_key_press(XK_F3, Mod4Mask);
    
    // Wait for callbacks to execute
    TestTimer::wait_for(100);
    
    // Check which hotkeys were triggered
    bool win_f1_triggered = MockCallbacks::was_triggered(hotkey1);
    bool win_f2_triggered = MockCallbacks::was_triggered(hotkey2);
    bool win_f3_triggered = MockCallbacks::was_triggered(hotkey3);
    
    bool f1_triggered = MockCallbacks::was_triggered(single1);
    bool f2_triggered = MockCallbacks::was_triggered(single2);
    bool f3_triggered = MockCallbacks::was_triggered(single3);
    
    std::cout << "win+f1 triggered: " << win_f1_triggered << ", f1 triggered: " << f1_triggered << std::endl;
    std::cout << "win+f2 triggered: " << win_f2_triggered << ", f2 triggered: " << f2_triggered << std::endl;
    std::cout << "win+f3 triggered: " << win_f3_triggered << ", f3 triggered: " << f3_triggered << std::endl;
    
    // The test passes if the Win+function key combinations were triggered and the single keys were not
    TEST_ASSERT(win_f1_triggered && !f1_triggered);
    TEST_ASSERT(win_f2_triggered && !f2_triggered);
    TEST_ASSERT(win_f3_triggered && !f3_triggered);
    
    return true;
}

// Test for the issue with modifier state handling
bool test_modifier_state_handling() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register hotkeys with various modifier combinations
    std::vector<std::string> hotkeys = {
        "ctrl+shift+g",
        "ctrl+alt+r",
        "ctrl+alt+q",
        "win+f1",
        "alt+f6",
        "alt+l",
        "alt+k",
        "alt+left",
        "alt+right"
    };
    
    for (const auto& hotkey : hotkeys) {
        MockCallbacks::register_hotkey(hotkey);
        io.Hotkey(hotkey, MockCallbacks::create_callback(hotkey));
    }
    
    // Create mock event generator
    MockX11EventGenerator eventGen;
    
    // Test each hotkey with proper modifier state
    for (const auto& hotkey : hotkeys) {
        // Parse the hotkey string to get key and modifiers
        size_t last_plus = hotkey.find_last_of('+');
        std::string key_str = hotkey.substr(last_plus + 1);
        std::string mod_str = hotkey.substr(0, last_plus);
        
        // Convert key string to KeySym
        KeySym key_sym = 0;
        if (key_str == "g") key_sym = XK_g;
        else if (key_str == "r") key_sym = XK_r;
        else if (key_str == "q") key_sym = XK_q;
        else if (key_str == "f1") key_sym = XK_F1;
        else if (key_str == "f6") key_sym = XK_F6;
        else if (key_str == "l") key_sym = XK_l;
        else if (key_str == "k") key_sym = XK_k;
        else if (key_str == "left") key_sym = XK_Left;
        else if (key_str == "right") key_sym = XK_Right;
        
        // Determine modifiers
        unsigned int modifiers = 0;
        if (mod_str.find("ctrl") != std::string::npos) modifiers |= ControlMask;
        if (mod_str.find("shift") != std::string::npos) modifiers |= ShiftMask;
        if (mod_str.find("alt") != std::string::npos) modifiers |= Mod1Mask;
        if (mod_str.find("win") != std::string::npos) modifiers |= Mod4Mask;
        
        // Simulate key press with modifiers
        std::cout << "Testing hotkey: " << hotkey << " (KeySym: " << key_sym << ", Modifiers: " << modifiers << ")" << std::endl;
        eventGen.simulate_key_press(key_sym, modifiers);
        
        // Wait for callback to execute
        TestTimer::wait_for(50);
    }
    
    // Check if all hotkeys were triggered
    bool all_triggered = true;
    for (const auto& hotkey : hotkeys) {
        bool triggered = MockCallbacks::was_triggered(hotkey);
        std::cout << "Hotkey " << hotkey << " triggered: " << triggered << std::endl;
        all_triggered &= triggered;
    }
    
    TEST_ASSERT(all_triggered);
    return true;
}

// Main function to run all tests
int main() {
    // Set up signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting IO modifier key tests..." << std::endl;
    
    // Run tests
    RUN_TEST(test_basic_modifier_detection);
    RUN_TEST(test_multiple_modifiers);
    RUN_TEST(test_modifier_detection_issue);
    RUN_TEST(test_alt_key_combinations);
    RUN_TEST(test_win_function_key_combinations);
    RUN_TEST(test_modifier_state_handling);
    
    // Print test summary
    std::cout << "\nTest Summary:" << std::endl;
    std::cout << "  Total tests: " << total_tests << std::endl;
    std::cout << "  Passed: " << passed_tests << std::endl;
    std::cout << "  Failed: " << failed_tests << std::endl;
    
    return failed_tests > 0 ? 1 : 0;
}
