// Quick test for EventQueue basic functionality
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>

// Mock the EventQueue (inline for testing)
#include <queue>
#include <mutex>
#include <functional>
#include <cstdint>

namespace havel::compiler {

class EventQueue {
public:
    using Callback = std::function<void()>;
    
    EventQueue() = default;
    ~EventQueue() = default;
    
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    
    void push(Callback cb) {
        if (!cb) return;
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push(std::move(cb));
    }
    
    void processAll() {
        while (true) {
            Callback cb;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (callbacks_.empty()) break;
                cb = std::move(callbacks_.front());
                callbacks_.pop();
            }
            try {
                cb();
            } catch (...) {}
        }
    }
    
    uint32_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<uint32_t>(callbacks_.size());
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return callbacks_.empty();
    }
    
private:
    std::queue<Callback> callbacks_;
    mutable std::mutex mutex_;
};

}

using namespace havel::compiler;

int main() {
    std::cout << "Testing EventQueue..." << std::endl;
    
    // Test 1: Empty queue
    EventQueue q;
    assert(q.empty());
    assert(q.size() == 0);
    std::cout << "✓ Test 1: Empty queue" << std::endl;
    
    // Test 2: Push and process
    int counter = 0;
    q.push([&counter] { counter++; });
    assert(q.size() == 1);
    q.processAll();
    assert(counter == 1);
    assert(q.empty());
    std::cout << "✓ Test 2: Push and process" << std::endl;
    
    // Test 3: Multiple callbacks (FIFO)
    std::string order;
    q.push([&order] { order += "A"; });
    q.push([&order] { order += "B"; });
    q.push([&order] { order += "C"; });
    q.processAll();
    assert(order == "ABC");
    std::cout << "✓ Test 3: FIFO ordering" << std::endl;
    
    // Test 4: Thread safety (push from multiple threads)
    std::atomic<int> thread_counter{0};
    std::thread t1([&q, &thread_counter] {
        for (int i = 0; i < 100; i++) {
            q.push([&thread_counter] { thread_counter++; });
        }
    });
    std::thread t2([&q, &thread_counter] {
        for (int i = 0; i < 100; i++) {
            q.push([&thread_counter] { thread_counter++; });
        }
    });
    t1.join();
    t2.join();
    assert(q.size() == 200);
    q.processAll();
    assert(thread_counter == 200);
    std::cout << "✓ Test 4: Thread-safe push" << std::endl;
    
    // Test 5: Non-blocking processAll (doesn't block even if empty)
    auto start = std::chrono::steady_clock::now();
    q.processAll();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    assert(elapsed.count() < 10);  // Should be nearly instant
    std::cout << "✓ Test 5: processAll non-blocking" << std::endl;
    
    // Test 6: Null callback ignored
    q.push(nullptr);
    assert(q.size() == 0);  // Should have been ignored
    std::cout << "✓ Test 6: Null callbacks ignored" << std::endl;
    
    std::cout << "\n✅ All EventQueue tests passed!" << std::endl;
    return 0;
}
