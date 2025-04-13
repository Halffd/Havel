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
    std::atomic<int> callback_count(0);
    std::mutex callback_mutex;
    std::condition_variable callback_cv;
    std::map<std::string, int> hotkey_counts;
    
    void reset() {
        std::lock_guard<std::mutex> lock(callback_mutex);
        callback_count = 0;
        hotkey_counts.clear();
    }
    
    std::function<void()> create_callback(const std::string& name) {
        return [name]() {
            std::lock_guard<std::mutex> lock(callback_mutex);
            callback_count++;
            hotkey_counts[name]++;
            std::cout << "Hotkey triggered: " << name << " (count: " << hotkey_counts[name] << ")" << std::endl;
            callback_cv.notify_all();
        };
    }
    
    bool wait_for_callbacks(int expected_count, int timeout_ms = 1000) {
        std::unique_lock<std::mutex> lock(callback_mutex);
        return callback_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [expected_count]() { return callback_count >= expected_count; });
    }
    
    int get_callback_count() {
        return callback_count;
    }
    
    int get_hotkey_count(const std::string& name) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        return hotkey_counts[name];
    }
}

// Controlled timing for race condition testing
class ControlledTimer {
private:
    std::atomic<bool> running;
    std::thread timer_thread;
    std::mutex mutex;
    std::condition_variable cv;
    std::function<void()> callback;
    int delay_ms;
    
public:
    ControlledTimer(std::function<void()> cb, int delay_milliseconds)
        : running(false), callback(cb), delay_ms(delay_milliseconds) {}
    
    ~ControlledTimer() {
        stop();
    }
    
    void start() {
        if (running) return;
        
        running = true;
        timer_thread = std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            if (running) {
                callback();
            }
        });
    }
    
    void stop() {
        running = false;
        if (timer_thread.joinable()) {
            timer_thread.join();
        }
    }
};

// Simulated X11 event generator with controlled timing
class TimedEventGenerator {
private:
    Display* display;
    
public:
    TimedEventGenerator() {
        display = H::DisplayManager::GetDisplay();
        if (!display) {
            std::cerr << "Failed to get X11 display" << std::endl;
        }
    }
    
    ~TimedEventGenerator() {
        // Don't close the display, it's managed by DisplayManager
    }
    
    // Generate a key press event with specific timing
    void generate_timed_key_press(KeySym keysym, unsigned int modifiers, int delay_ms) {
        if (!display) return;
        
        ControlledTimer timer([this, keysym, modifiers]() {
            XEvent event;
            std::memset(&event, 0, sizeof(event));
            
            event.type = KeyPress;
            event.xkey.display = display;
            event.xkey.window = DefaultRootWindow(display);
            event.xkey.root = DefaultRootWindow(display);
            event.xkey.subwindow = None;
            event.xkey.time = CurrentTime;
            event.xkey.state = modifiers;
            event.xkey.keycode = XKeysymToKeycode(display, keysym);
            event.xkey.same_screen = True;
            
            H::IO::HandleKeyEvent(event);
        }, delay_ms);
        
        timer.start();
    }
    
    // Generate a sequence of key press events with specific timing
    void generate_timed_sequence(std::vector<std::pair<KeySym, unsigned int>> keys, 
                                std::vector<int> delays) {
        if (!display) return;
        
        if (keys.size() != delays.size()) {
            std::cerr << "Keys and delays must have the same size" << std::endl;
            return;
        }
        
        for (size_t i = 0; i < keys.size(); i++) {
            generate_timed_key_press(keys[i].first, keys[i].second, delays[i]);
        }
    }
};

// Test cases for race conditions

// Test rapid hotkey registration and unregistration
bool test_rapid_registration() {
    H::IO io;
    MockCallbacks::reset();
    
    const int num_threads = 10;
    const int hotkeys_per_thread = 5;
    std::atomic<int> registration_success(0);
    
    // Launch multiple threads to register hotkeys rapidly
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.push_back(std::thread([&io, t, &registration_success]() {
            for (int i = 0; i < hotkeys_per_thread; i++) {
                std::string hotkey = "ctrl+" + std::to_string(t) + "_" + std::to_string(i);
                if (io.Hotkey(hotkey, []() { /* Empty callback */ })) {
                    registration_success++;
                }
            }
        }));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Successfully registered " << registration_success << " hotkeys out of " 
              << (num_threads * hotkeys_per_thread) << std::endl;
    
    // We expect most registrations to succeed, but not necessarily all due to race conditions
    TEST_ASSERT(registration_success > 0);
    
    return true;
}

// Test concurrent hotkey triggering
bool test_concurrent_triggering() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register a set of hotkeys
    const int num_hotkeys = 5;
    std::vector<std::string> hotkeys;
    
    for (int i = 0; i < num_hotkeys; i++) {
        std::string hotkey = "ctrl+" + std::to_string(i);
        hotkeys.push_back(hotkey);
        io.Hotkey(hotkey, MockCallbacks::create_callback(hotkey));
    }
    
    // Create event generator
    TimedEventGenerator eventGen;
    
    // Generate concurrent key press events
    for (int i = 0; i < num_hotkeys; i++) {
        KeySym keysym = XK_0 + i;
        eventGen.generate_timed_key_press(keysym, ControlMask, 10); // Almost simultaneous
    }
    
    // Wait for all callbacks to be executed
    bool all_triggered = MockCallbacks::wait_for_callbacks(num_hotkeys);
    
    std::cout << "Triggered " << MockCallbacks::get_callback_count() << " callbacks out of " 
              << num_hotkeys << std::endl;
    
    TEST_ASSERT(all_triggered);
    TEST_ASSERT(MockCallbacks::get_callback_count() == num_hotkeys);
    
    return true;
}

// Test rapid modifier key state changes
bool test_rapid_modifier_changes() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register hotkeys with different modifier combinations
    std::string hotkey1 = "ctrl+a";
    std::string hotkey2 = "ctrl+shift+a";
    std::string hotkey3 = "ctrl+alt+a";
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    io.Hotkey(hotkey3, MockCallbacks::create_callback(hotkey3));
    
    // Create event generator
    TimedEventGenerator eventGen;
    
    // Generate a sequence of key presses with rapidly changing modifier states
    std::vector<std::pair<KeySym, unsigned int>> keys = {
        {XK_a, ControlMask},                     // ctrl+a
        {XK_a, ControlMask | ShiftMask},         // ctrl+shift+a
        {XK_a, ControlMask | Mod1Mask},          // ctrl+alt+a
        {XK_a, ControlMask},                     // ctrl+a again
        {XK_a, ControlMask | ShiftMask | Mod1Mask} // ctrl+shift+alt+a (not registered)
    };
    
    std::vector<int> delays = {10, 20, 30, 40, 50}; // Rapid sequence
    
    eventGen.generate_timed_sequence(keys, delays);
    
    // Wait for callbacks to execute
    bool all_triggered = MockCallbacks::wait_for_callbacks(4); // 4 expected triggers
    
    std::cout << "Total callbacks: " << MockCallbacks::get_callback_count() << std::endl;
    std::cout << "ctrl+a: " << MockCallbacks::get_hotkey_count(hotkey1) << std::endl;
    std::cout << "ctrl+shift+a: " << MockCallbacks::get_hotkey_count(hotkey2) << std::endl;
    std::cout << "ctrl+alt+a: " << MockCallbacks::get_hotkey_count(hotkey3) << std::endl;
    
    TEST_ASSERT(all_triggered);
    TEST_ASSERT(MockCallbacks::get_callback_count() == 4);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey1) == 2); // Triggered twice
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey2) == 1);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey3) == 1);
    
    return true;
}

// Test interleaved key press events
bool test_interleaved_key_presses() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register hotkeys
    std::string hotkey1 = "ctrl+x";
    std::string hotkey2 = "alt+y";
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    
    // Create event generator
    TimedEventGenerator eventGen;
    
    // Generate interleaved key press events
    // ctrl down, alt down, x down, y down, ctrl up, alt up
    std::vector<std::pair<KeySym, unsigned int>> keys = {
        {XK_Control_L, 0},            // Ctrl down
        {XK_Alt_L, ControlMask},      // Alt down (with Ctrl still down)
        {XK_x, ControlMask | Mod1Mask}, // x down (with both Ctrl and Alt down)
        {XK_y, ControlMask | Mod1Mask}, // y down (with both Ctrl and Alt down)
    };
    
    std::vector<int> delays = {10, 20, 30, 40};
    
    eventGen.generate_timed_sequence(keys, delays);
    
    // Wait for callbacks to execute
    bool callbacks_executed = MockCallbacks::wait_for_callbacks(2);
    
    std::cout << "Total callbacks: " << MockCallbacks::get_callback_count() << std::endl;
    std::cout << "ctrl+x: " << MockCallbacks::get_hotkey_count(hotkey1) << std::endl;
    std::cout << "alt+y: " << MockCallbacks::get_hotkey_count(hotkey2) << std::endl;
    
    // Both hotkeys should be triggered
    TEST_ASSERT(callbacks_executed);
    TEST_ASSERT(MockCallbacks::get_callback_count() == 2);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey1) == 1);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey2) == 1);
    
    return true;
}

// Test suspend/resume during key press
bool test_suspend_during_key_press() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register a hotkey
    std::string hotkey = "ctrl+s";
    int hotkey_id = 1;
    
    io.Hotkey(hotkey, MockCallbacks::create_callback(hotkey), hotkey_id);
    
    // Create event generator
    TimedEventGenerator eventGen;
    
    // Set up a thread to suspend the hotkey while key press events are being generated
    std::thread suspend_thread([&io, hotkey_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(25)); // Suspend after some events
        io.Suspend(hotkey_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Keep suspended for a while
        io.Resume(hotkey_id);
    });
    
    // Generate a sequence of key press events
    for (int i = 0; i < 10; i++) {
        eventGen.generate_timed_key_press(XK_s, ControlMask, i * 10); // Every 10ms
    }
    
    // Wait for thread to complete
    suspend_thread.join();
    
    // Wait for callbacks to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    int callback_count = MockCallbacks::get_callback_count();
    std::cout << "Total callbacks: " << callback_count << std::endl;
    
    // Some key presses should be ignored during suspension
    TEST_ASSERT(callback_count > 0 && callback_count < 10);
    
    return true;
}

// Test for race conditions in modifier state tracking
bool test_modifier_state_race() {
    H::IO io;
    MockCallbacks::reset();
    
    // Register hotkeys with different modifier combinations
    std::string hotkey1 = "ctrl+m";
    std::string hotkey2 = "shift+m";
    std::string hotkey3 = "ctrl+shift+m";
    
    io.Hotkey(hotkey1, MockCallbacks::create_callback(hotkey1));
    io.Hotkey(hotkey2, MockCallbacks::create_callback(hotkey2));
    io.Hotkey(hotkey3, MockCallbacks::create_callback(hotkey3));
    
    // Create event generator
    TimedEventGenerator eventGen;
    
    // Generate a sequence of key presses with rapidly changing modifier states
    // This simulates pressing and releasing modifiers very quickly
    std::vector<std::pair<KeySym, unsigned int>> keys = {
        {XK_Control_L, 0},            // Ctrl down
        {XK_Shift_L, ControlMask},    // Shift down (with Ctrl down)
        {XK_m, ControlMask | ShiftMask}, // m down (with both Ctrl and Shift down)
        {XK_Shift_L, ControlMask},    // Shift up (Ctrl still down)
        {XK_m, ControlMask},          // m down (with only Ctrl down)
        {XK_Control_L, 0},            // Ctrl up
        {XK_Shift_L, 0},              // Shift down
        {XK_m, ShiftMask},            // m down (with only Shift down)
    };
    
    std::vector<int> delays = {5, 10, 15, 20, 25, 30, 35, 40}; // Very rapid sequence
    
    eventGen.generate_timed_sequence(keys, delays);
    
    // Wait for callbacks to execute
    bool callbacks_executed = MockCallbacks::wait_for_callbacks(3);
    
    std::cout << "Total callbacks: " << MockCallbacks::get_callback_count() << std::endl;
    std::cout << "ctrl+m: " << MockCallbacks::get_hotkey_count(hotkey1) << std::endl;
    std::cout << "shift+m: " << MockCallbacks::get_hotkey_count(hotkey2) << std::endl;
    std::cout << "ctrl+shift+m: " << MockCallbacks::get_hotkey_count(hotkey3) << std::endl;
    
    // All three hotkeys should be triggered
    TEST_ASSERT(callbacks_executed);
    TEST_ASSERT(MockCallbacks::get_callback_count() == 3);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey1) == 1);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey2) == 1);
    TEST_ASSERT(MockCallbacks::get_hotkey_count(hotkey3) == 1);
    
    return true;
}

// Main function to run all tests
int main() {
    // Set up signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting IO race condition tests..." << std::endl;
    
    // Run tests
    RUN_TEST(test_rapid_registration);
    RUN_TEST(test_concurrent_triggering);
    RUN_TEST(test_rapid_modifier_changes);
    RUN_TEST(test_interleaved_key_presses);
    RUN_TEST(test_suspend_during_key_press);
    RUN_TEST(test_modifier_state_race);
    
    // Print test summary
    std::cout << "\nTest Summary:" << std::endl;
    std::cout << "  Total tests: " << total_tests << std::endl;
    std::cout << "  Passed: " << passed_tests << std::endl;
    std::cout << "  Failed: " << failed_tests << std::endl;
    
    return failed_tests > 0 ? 1 : 0;
}
