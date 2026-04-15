#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

namespace havel::compiler {

// Forward declarations
class Fiber;

/**
 * WatcherRegistry - Reactive watcher management with edge-triggered firing
 *
 * Phase 2C: Manages "when" statements that fire only on false→true transitions.
 *
 * Key Features:
 * 1. Edge-triggered: Only fires when condition changes from false to true
 * 2. Dependency-aware: Knows which variables matter for each watcher
 * 3. Efficient: Only re-evaluates when relevant variables change
 * 4. No polling: Event-driven via VAR_CHANGED events
 *
 * Architecture:
 * - Watcher: condition + fiber + last_result + dependencies
 * - Registry: maps variable_name → list of watchersReactionListeners
 * - Event handler: var changed → notify watchers → re-evaluate → fire if false→true
 *
 * Example:
 *   when enabled { print "enabled" }
 *   // Creates watcher: enabled (condition) → depends on {enabled}
 *   // Initially evaluates: enabled = false (last_result = false)
 *   // User: enabled = true
 *   // Event: VAR_CHANGED {enabled}
 *   // Handler: enabled in dependencies? → re-evaluate enabled
 *   // Result: true (was false) → FIRE! (edge-triggered)
 *   // New last_result: true
 *   // User: enabled = true again
 *   // Event: VAR_CHANGED {enabled}
 *   // Handler: re-evaluate enabled
 *   // Result: true (still true) → NO FIRE (not false→true edge)
 */
class WatcherRegistry {
public:
    // Unique identifier for a watcher
    using WatcherId = uint32_t;
    constexpr static WatcherId INVALID_WATCHER = 0;
    
    // Callback type for watcher evaluation and firing
    // Takes: condition body (should evaluate and fire if true)
    // Fiber will be resumed by ExecutionEngine event handler
    using WatcherBody = std::function<void(Fiber*)>;
    
    WatcherRegistry();
    ~WatcherRegistry() = default;
    
    /**
     * Register a watcher (when statement)
     *
     * @param condition_func_id Function ID for condition bytecode
     * @param condition_ip Instruction pointer for condition bytecode
     * @param condition_result Initial condition evaluation result
     * @param dependencies Variables used in the condition
     * @param fiber Fiber to resume when watcher fires
     * @return watcher_id for later management
     */
    WatcherId registerWatcher(
        uint32_t condition_func_id,
        uint32_t condition_ip,
        bool condition_result,
        const std::unordered_set<std::string>& dependencies,
        Fiber* fiber
    );
    
    /**
     * Unregister a watcher (cleanup)
     *
     * @param watcher_id ID from registerWatcher()
     * @return true if found and removed, false if not found
     */
    bool unregisterWatcher(WatcherId watcher_id);
    
    /**
     * Notify that a variable changed
     *
     * Called from onVariableChanged() event handler.
     * Re-evaluates all watchers that depend on this variable.
     * Fires watchers with false→true edge transition.
     *
     * @param var_name Name of variable that changed
     * @param evaluator Function to re-evaluate condition for a watcher
     *                  Takes watcher_id, returns new condition result
     * @return List of fibers to resume (fired watchers)
     */
    std::vector<Fiber*> onVariableChanged(
        const std::string& var_name,
        std::function<bool(WatcherId)> evaluator
    );
    
    /**
     * Get dependencies for a watcher (for debugging)
     */
    std::unordered_set<std::string> getWatcherDependencies(WatcherId watcher_id) const;
    
    /**
     * Get all watchers (for debugging)
     */
    std::vector<WatcherId> getAllWatchers() const;
    
    /**
     * Get count of registered watchers
     */
    size_t getWatcherCount() const { return watchers_.size(); }

    // Phase 2E: Internal watcher structure
    struct Watcher {
        WatcherId id;
        Fiber* fiber;
        std::unordered_set<std::string> dependencies;
        bool last_result;  // Last evaluated result (for edge detection)
        
        // Phase 2E: Condition bytecode reference
        uint32_t condition_func_id;  // Function ID in VM bytecode
        uint32_t condition_ip;       // Instruction pointer for condition
        
        Watcher(WatcherId id_, Fiber* f,
                const std::unordered_set<std::string>& deps, bool result,
                uint32_t func_id, uint32_t ip)
            : id(id_), fiber(f), dependencies(deps), last_result(result),
              condition_func_id(func_id), condition_ip(ip) {}
    };
    
    /**
     * Phase 2E: Get watcher by ID (for condition evaluation)
     */
    const Watcher* getWatcher(WatcherId watcher_id) const {
        auto it = watchers_.find(watcher_id);
        return (it != watchers_.end()) ? &it->second : nullptr;
    }

private:
    // All registered watchers (id → watcher)
    std::unordered_map<uint32_t, Watcher> watchers_;
    
    // Reverse index: variable → [watcher_ids that depend on it]
    // Enables efficient lookup of "which watchers care about X?"
    std::unordered_map<std::string, std::vector<uint32_t>> var_to_watchers_;
    
    // Next watcher ID (incremented for each new watcher)
    uint32_t next_watcher_id_ = 1;
};

}  // namespace havel::compiler
