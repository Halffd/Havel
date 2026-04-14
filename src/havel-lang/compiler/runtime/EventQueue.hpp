#pragma once

#include <queue>
#include <mutex>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace havel::compiler {

/**
 * EventType - Unified event system for all async sources
 *
 * All async operations (threads, timers, channels, variables) push events.
 * Main loop dispatches events to registered handlers.
 * Everything flows through the same queue, enabling backpressure/ordering.
 */
enum class EventType : uint8_t {
    THREAD_COMPLETE = 0,    // Thread finished execution
    VAR_CHANGED = 1,        // Global/local variable changed
    TIMER_FIRE = 2,         // Timer/timeout fired
    CHANNEL_SEND = 3,       // Channel message ready
    CHANNEL_RECV = 4,       // Channel receiver ready
    LEGACY_CALLBACK = 255,  // Backward compat: callback in ptr field
    // Future: FILE_READY, NETWORK_RECV, etc.
};

/**
 * Event - Typed event with flexible payload
 *
 * Simple design for fast dispatch:
 * - type: identifies the event kind
 * - data: 2 uint32_t for most cases (thread_id, var_id, timer_id, etc.)
 * - ptr: for complex payloads (Channel*, Value*, etc.)
 */
struct Event {
    EventType type;
    uint32_t data1 = 0;
    uint32_t data2 = 0;
    void* ptr = nullptr;
    
    Event(EventType t) : type(t) {}
    Event(EventType t, uint32_t d1) : type(t), data1(d1) {}
    Event(EventType t, uint32_t d1, uint32_t d2) : type(t), data1(d1), data2(d2) {}
    Event(EventType t, uint32_t d1, void* p) : type(t), data1(d1), ptr(p) {}
};

/**
 * EventQueue - Unified non-blocking event queue
 *
 * Bridges between:
 * - OS threads (push events, never touch VM)
 * - Timers (push timer events)
 * - Channels (push channel events)
 * - Variables (push change events)
 * - Main event loop (dispatch events to handlers)
 *
 * Design Invariants:
 * 1. Only main thread calls processAll()
 * 2. Any thread can safely call push()
 * 3. Event handlers never call VM directly
 * 4. processAll() always completes (finite)
 * 5. Events processed FIFO order
 *
 * Non-blocking:
 * - push() does not block the VM thread
 * - processAll() drains queue and returns
 * - No condition variables involved
 */
class EventQueue {
public:
    using EventHandler = std::function<void(const Event&)>;
    using Callback = std::function<void()>;  // Legacy callback type
    
    EventQueue() = default;
    ~EventQueue() = default;
    
    // Non-copyable
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    
    /**
     * Push an event to be handled in main event loop
     *
     * Thread-safe (can be called from any thread).
     * Never blocks. Always returns immediately.
     *
     * @param event The event to enqueue
     */
    void push(const Event& event);
    
    /**
     * DEPRECATED: Push a callback (legacy API - use push(Event) instead)
     *
     * Wraps callback in a LEGACY_CALLBACK event for backward compatibility.
     * Should only be used during migration from callback-based to event-based API.
     * Will be removed once all callers updated.
     *
     * @param cb Callback to execute
     */
    void push(Callback cb);
    
    /**
     * Register a handler for a specific event type
     *
     * Handlers are called during processAll() in order.
     * Only one handler per event type (replaces previous).
     * Handler should be fast and not block.
     *
     * @param type The event type to handle
     * @param handler Function to call when event occurs
     */
    void onEvent(EventType type, EventHandler handler);
    
    /**
     * Process all enqueued events and dispatch to handlers
     *
     * Should be called from main event loop (single-threaded context).
     * Drains current queue and executes all event handlers.
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
     */
    uint32_t size() const;
    
    /**
     * Check if queue is empty (for diagnostics)
     */
    bool empty() const;
    
    /**
     * Clear all pending events (for shutdown)
     *
     * Should only be called during shutdown sequence.
     * Discards any pending events without executing handlers.
     */
    void clear();

private:
    std::queue<Event> events_;
    std::unordered_map<uint8_t, EventHandler> handlers_;  // EventType -> Handler
    mutable std::mutex mutex_;
};

}  // namespace havel::compiler
