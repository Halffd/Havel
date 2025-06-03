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
#include "../core/IO.hpp"
#include "../core/DisplayManager.hpp"
#include "../utils/Logger.hpp"

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
    std::atomic<int> callback_invocations(0);
    std::atomic<bool> callback_executed(false);
    std::mutex callback_mutex;
    
    void reset() {
        callback_invocations = 0;
        callback_executed = false;
    }
    
    void simple_callback() {
        callback_invocations++;
        callback_executed = true;
    }
    
    void delayed_callback() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        callback_invocations++;
        callback_executed = true;
    }
    
    void mutex_callback() {
        std::lock_guard<std::mutex> lock(callback_mutex);
        callback_invocations++;
        callback_executed = true;
    }
}

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

// Mock X11 event generator
class MockEventGenerator {
public:
    static XEvent create_key_press_event(KeySym keysym, unsigned int modifiers) {
        XEvent event;
        memset(&event, 0, sizeof(event));
        
        event.type = KeyPress;
        event.xkey.keycode = XKeysymToKeycode(havel::DisplayManager::GetDisplay(), keysym);
        event.xkey.state = modifiers;
        
        return event;
    }
    
    static void simulate_key_press(KeySym keysym, unsigned int modifiers) {
        // This is a simplified simulation - in a real environment, 
        // we would need to inject events into the X11 event queue
        XEvent event = create_key_press_event(keysym, modifiers);
        havel::IO::HandleKeyEvent(event);
    }
};

// Test cases

// 1. Basic functionality tests
bool test_io_initialization() {
    havel::IO io;
    // Basic initialization test - if it doesn't crash, that's a good start
    return true;
}

bool test_hotkey_registration() {
    havel::IO io;
    MockCallbacks::reset();
    
    // Register a simple hotkey
    bool result = io.Hotkey("ctrl+a", MockCallbacks::simple_callback);
    
    TEST_ASSERT(result);
    return true;
}

bool test_multiple_hotkey_registration() {
    havel::IO io;
    MockCallbacks::reset();
    
    // Register multiple hotkeys
    bool result1 = io.Hotkey("ctrl+a", MockCallbacks::simple_callback);
    bool result2 = io.Hotkey("alt+b", MockCallbacks::simple_callback);
    bool result3 = io.Hotkey("win+c", MockCallbacks::simple_callback);
    
    TEST_ASSERT(result1 && result2 && result3);
    return true;
}

// 2. Race condition tests
bool test_concurrent_hotkey_registration() {
    havel::IO io;
    std::atomic<int> success_count(0);
    
    // Launch multiple threads to register hotkeys concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.push_back(std::thread([&io, i, &success_count]() {
            std::string hotkey = "ctrl+" + std::to_string(i);
            if (io.Hotkey(hotkey, MockCallbacks::simple_callback)) {
                success_count++;
            }
        }));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    TEST_ASSERT(success_count == 5);
    return true;
}

bool test_hotkey_suspend_resume_race() {
    havel::IO io;
    MockCallbacks::reset();
    
    // Register a hotkey
    int id = 1;
    bool reg_result = io.Hotkey("ctrl+z", MockCallbacks::simple_callback, id);
    TEST_ASSERT(reg_result);
    
    // Create threads that suspend and resume the hotkey concurrently
    std::thread suspend_thread([&io, id]() {
        for (int i = 0; i < 10; i++) {
            io.Suspend(id);
            TestTimer::wait_for(5);
        }
    });
    
    std::thread resume_thread([&io, id]() {
        for (int i = 0; i < 10; i++) {
            io.Resume(id);
            TestTimer::wait_for(5);
        }
    });
    
    // Wait for threads to complete
    suspend_thread.join();
    resume_thread.join();
    
    // Final state should be deterministic (either suspended or resumed)
    // We're just testing that there's no crash or deadlock
    return true;
}

bool test_callback_execution_race() {
    havel::IO io;
    MockCallbacks::reset();
    std::mutex m;
    std::condition_variable cv;
    bool callback_ready = false;
    
    // Register a hotkey with a callback that uses a mutex
    bool reg_result = io.Hotkey("ctrl+m", []() {
        MockCallbacks::mutex_callback();
    });
    TEST_ASSERT(reg_result);
    
    // Create threads that try to acquire the same mutex
    std::thread contention_thread([&m, &cv, &callback_ready]() {
        std::unique_lock<std::mutex> lock(MockCallbacks::callback_mutex);
        {
            std::lock_guard<std::mutex> notify_lock(m);
            callback_ready = true;
            cv.notify_one();
        }
        // Hold the lock for a while
        TestTimer::wait_for(100);
    });
    
    // Wait for contention thread to acquire the mutex
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&callback_ready]() { return callback_ready; });
    }
    
    // Simulate key press that would trigger the callback
    // The callback will be blocked by the contention_thread
    MockEventGenerator::simulate_key_press(XK_m, ControlMask);
    
    // Wait for contention thread to complete
    contention_thread.join();
    
    // Wait a bit to allow the callback to execute
    TestTimer::wait_for(200);
    
    // The callback should eventually execute
    TEST_ASSERT(MockCallbacks::callback_executed);
    return true;
}

// 3. Complexity reduction tests
bool test_isolated_hotkey_functionality() {
    havel::IO io;
    MockCallbacks::reset();
    
    // Test individual components of hotkey functionality
    
    // 1. Parse modifiers
    int modifiers = io.ParseModifiers("ctrl+alt+shift");
    TEST_ASSERT(modifiers == (ControlMask | Mod1Mask | ShiftMask));
    
    // 2. Register hotkey
    bool reg_result = io.Hotkey("ctrl+i", MockCallbacks::simple_callback);
    TEST_ASSERT(reg_result);
    
    // 3. Suspend hotkey
    bool suspend_result = io.Suspend(1); // Assuming ID 1 is assigned
    TEST_ASSERT(suspend_result);
    
    // 4. Resume hotkey
    bool resume_result = io.Resume(1);
    TEST_ASSERT(resume_result);
    
    return true;
}

bool test_boundary_conditions() {
    havel::IO io;
    
    // Test empty hotkey string
    bool empty_result = io.Hotkey("", MockCallbacks::simple_callback);
    TEST_ASSERT(!empty_result); // Should fail
    
    // Test invalid hotkey string
    bool invalid_result = io.Hotkey("invalid+key+combination", MockCallbacks::simple_callback);
    TEST_ASSERT(!invalid_result); // Should fail
    
    // Test null callback
    bool null_result = io.Hotkey("ctrl+n", nullptr);
    TEST_ASSERT(!null_result); // Should fail
    
    // Test invalid ID for suspend/resume
    bool invalid_suspend = io.Suspend(-1);
    TEST_ASSERT(!invalid_suspend); // Should fail
    
    bool invalid_resume = io.Resume(9999);
    TEST_ASSERT(!invalid_resume); // Should fail
    
    return true;
}

bool test_resource_cleanup() {
    {
        havel::IO io;
        MockCallbacks::reset();
        
        // Register a hotkey
        bool reg_result = io.Hotkey("ctrl+r", MockCallbacks::simple_callback);
        TEST_ASSERT(reg_result);
        
        // Let the IO object go out of scope
    }
    
    // Create a new IO object
    havel::IO new_io;
    
    // Try to register the same hotkey again
    bool new_reg_result = new_io.Hotkey("ctrl+r", MockCallbacks::simple_callback);
    
    // This should succeed because the previous IO object should have cleaned up
    TEST_ASSERT(new_reg_result);
    
    return true;
}

// 4. Asynchronous operation tests
bool test_delayed_callback_execution() {
    havel::IO io;
    MockCallbacks::reset();
    
    // Register a hotkey with a delayed callback
    bool reg_result = io.Hotkey("ctrl+d", MockCallbacks::delayed_callback);
    TEST_ASSERT(reg_result);
    
    // Simulate key press
    MockEventGenerator::simulate_key_press(XK_d, ControlMask);
    
    // The callback should not have executed immediately
    TEST_ASSERT(MockCallbacks::callback_invocations == 0);
    
    // Wait for the delayed callback to complete
    bool executed = TestTimer::wait_for_condition(
        []() { return MockCallbacks::callback_executed; },
        200 // 200ms timeout
    );
    TEST_ASSERT(executed);
    TEST_ASSERT(MockCallbacks::callback_invocations == 1);
    
    return true;
}

bool test_multiple_concurrent_callbacks() {
    havel::IO io;
    MockCallbacks::reset();
    std::atomic<int> callback_count(0);
    
    // Register multiple hotkeys with callbacks that increment the same counter
    for (int i = 0; i < 5; i++) {
        std::string hotkey = "ctrl+" + std::to_string(i);
        io.Hotkey(hotkey, [&callback_count]() {
            callback_count++;
        });
    }
    
    // Simulate multiple key presses concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.push_back(std::thread([i]() {
            KeySym keysym = XK_0 + i;
            MockEventGenerator::simulate_key_press(keysym, ControlMask);
        }));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    // Wait for callbacks to execute
    TestTimer::wait_for(100);
    
    // All callbacks should have executed
    TEST_ASSERT(callback_count == 5);
    
    return true;
}

// Main function to run all tests
int main() {
    // Set up signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Starting IO module unit tests..." << std::endl;
    
    // Basic functionality tests
    RUN_TEST(test_io_initialization);
    RUN_TEST(test_hotkey_registration);
    RUN_TEST(test_multiple_hotkey_registration);
    
    // Race condition tests
    RUN_TEST(test_concurrent_hotkey_registration);
    RUN_TEST(test_hotkey_suspend_resume_race);
    RUN_TEST(test_callback_execution_race);
    
    // Complexity reduction tests
    RUN_TEST(test_isolated_hotkey_functionality);
    RUN_TEST(test_boundary_conditions);
    RUN_TEST(test_resource_cleanup);
    
    // Asynchronous operation tests
    RUN_TEST(test_delayed_callback_execution);
    RUN_TEST(test_multiple_concurrent_callbacks);
    
    // Print test summary
    std::cout << "\nTest Summary:" << std::endl;
    std::cout << "  Total tests: " << total_tests << std::endl;
    std::cout << "  Passed: " << passed_tests << std::endl;
    std::cout << "  Failed: " << failed_tests << std::endl;
    
    return failed_tests > 0 ? 1 : 0;
}
