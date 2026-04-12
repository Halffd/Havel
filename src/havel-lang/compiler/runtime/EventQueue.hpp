#pragma once

#include <queue>
#include <mutex>
#include <functional>
#include <cstdint>

namespace havel::compiler {

/**
 * EventQueue - Non-blocking callback queue for concurrency system
 *
 * Bridges between:
 * - OS threads (enqueue callbacks, never touch VM)
 * - Timers (enqueue fired callbacks)
 * - Channels (enqueue wake-up notifications)
 * - Main event loop (process all callbacks)
 *
 * Design Invariants:
 * 1. Only main thread calls processAll()
 * 2. Any thread can safely call push()
 * 3. Callbacks never call VM directly
 * 4. processAll() always completes (never yields)
 * 5. Callbacks are executed after they're enqueued (FIFO order)
 *
 * Non-blocking:
 * - push() does not block the VM thread
 * - processAll() is finite (drains current queue, returns)
 * - No condition variables involved
 */
class EventQueue {
public:
    using Callback = std::function<void()>;
    
    EventQueue() = default;
    ~EventQueue() = default;
    
    // Non-copyable
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    
    /**
     * Push a callback to be executed in main event loop
     *
     * Thread-safe (can be called from any thread).
     * Never blocks. Always returns immediately.
     *
     * @param cb Callback to execute (should not block, should not yield)
     */
    void push(Callback cb);
    
    /**
     * Process all enqueued callbacks
     *
     * Should be called from main event loop (not from worker threads).
     * Drains the current queue and executes all callbacks.
     * Does not block or yield.
     *
     * Order guaranteed: FIFO
     */
    void processAll();
    
    /**
     * Get current queue size (for diagnostics)
     *
     * Thread-safe read-only access.
     * Not guaranteed to be accurate due to async enqueuing.
     * Should only be used for logging/debugging.
     */
    uint32_t size() const;
    
    /**
     * Check if queue is empty (for diagnostics)
     */
    bool empty() const;
    
    /**
     * Clear all pending callbacks (for shutdown)
     *
     * Should only be called during shutdown sequence.
     * Discards any pending callbacks without executing them.
     */
    void clear();

private:
    std::queue<Callback> callbacks_;
    mutable std::mutex mutex_;
};

}  // namespace havel::compiler
