#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::compiler {

// EventPayload - plain data crossing producer threads into the VM loop.
// String-typed fields on purpose: heap VM Values are not thread-safe, and
// generic-event payloads are interpolated as strings in handlers anyway.
// Producers (file watcher, window sources, scripts) fill fields; dispatch
// materializes them into a VM object for the handler's `event` variable.
struct EventPayload {
    std::unordered_map<std::string, std::string> fields;

    bool empty() const { return fields.empty(); }
};

/**
 * EventRuntime - the generic event bus (architecture doc: "on as a
 * first-class event subscription primitive").
 *
 * Separation of concerns:
 *   Producers publish.  The runtime dispatches.  Havel handlers execute.
 *
 * - Producers (FileWatcher, WindowEventSource, scripts via `emit`) call
 *   publish(); it is thread-safe and only queues — it never touches the VM.
 * - The VM's normal execution loop (the engine's goroutine pump) calls
 *   dispatch(); queued events are matched against subscriptions and their
 *   handlers run OUTSIDE backend threads, in VM context.
 * - `on` syntax compiles to a subscription registration (a host function
 *   call); the runtime never sees the parser.
 *
 * Subscription registry: event name -> [(subscription id, handler function
 * index)]. Handler function INDEX (not a heap Value) so the registry is
 * GC-safe; the reload path clears all subscriptions before re-running
 * __on_reload__, so handlers never outlive their compile unit.
 */
class EventRuntime {
public:
    using SubscriptionId = uint64_t;
    constexpr static SubscriptionId INVALID_SUBSCRIPTION = 0;

    using HandlerFn = std::function<void(const EventPayload &)>;

    EventRuntime() = default;
    ~EventRuntime() = default;

    // Subscribe a handler to an event name. Thread-safe.
    // Dotted names by convention ("file.changed", "window.focused",
    // "my.event") — the runtime assigns no meaning to them.
    SubscriptionId subscribe(const std::string &name, HandlerFn handler);

    // Remove one subscription. Returns false when the id is unknown.
    bool unsubscribe(SubscriptionId id);

    // Drop every subscription (script reload: handlers must never outlive
    // their compile unit — the classic handler-infestation failure).
    void clearAll();

    // Queue an event for dispatch. Thread-safe; never touches the VM.
    void publish(const std::string &name, EventPayload payload = {});

    // Run queued events' matching handlers. Called from the VM's pump loop
    // (never from backend threads).
    void dispatch();

    size_t subscriptionCount() const;
    size_t queuedCount() const;

    // Subscription argument (e.g. a watch path for `on file.changed(path)`)
    // — event sources read it to establish their native watcher.
    void setSubscriptionArg(SubscriptionId id, std::string arg);
    std::string getSubscriptionArg(SubscriptionId id) const;

private:
    struct Subscription {
        SubscriptionId id;
        std::string name;
        HandlerFn handler;
    };
    struct QueuedEvent {
        std::string name;
        EventPayload payload;
    };

    mutable std::mutex mutex_;
    // name -> subscriptions (insertion order preserved for dispatch)
    std::unordered_map<std::string, std::vector<Subscription>> subscriptions_;
    std::deque<QueuedEvent> queue_;
    std::unordered_map<SubscriptionId, std::string> subscription_args_;
    std::atomic<SubscriptionId> next_id_{1};
};

} // namespace havel::compiler
