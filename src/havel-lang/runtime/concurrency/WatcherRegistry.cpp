#include "WatcherRegistry.hpp"
#include <algorithm>

namespace havel::compiler {

// ============================================================================
// WatcherRegistry Implementation
// ============================================================================

WatcherRegistry::WatcherRegistry() = default;

WatcherRegistry::WatcherId WatcherRegistry::registerWatcher(
    uint32_t condition_func_id,
    uint32_t condition_ip,
    bool condition_result,
    const std::unordered_set<std::string>& dependencies,
    Fiber* fiber
) {
    if (!fiber) {
        return INVALID_WATCHER;
    }
    
    // Allocate next watcher ID
    WatcherId watcher_id = next_watcher_id_++;
    if (watcher_id == INVALID_WATCHER) {
        next_watcher_id_++;  // Skip invalid ID
        watcher_id = next_watcher_id_++;
    }
    
    // Phase 2E: Create and store watcher with condition bytecode reference
    watchers_.emplace(
        watcher_id,
        Watcher(watcher_id, fiber, dependencies, condition_result, condition_func_id, condition_ip));
    
    // Register in reverse index: variable → watcher
    for (const auto& var_name : dependencies) {
        var_to_watchers_[var_name].push_back(watcher_id);
    }
    
    return watcher_id;
}

bool WatcherRegistry::unregisterWatcher(WatcherId watcher_id) {
    auto it = watchers_.find(watcher_id);
    if (it == watchers_.end()) {
        return false;
    }
    
    // Remove from reverse index
    const auto& dependencies = it->second.dependencies;
    for (const auto& var_name : dependencies) {
        auto& watchers_list = var_to_watchers_[var_name];
        auto watcher_it = std::find(watchers_list.begin(), watchers_list.end(), watcher_id);
        if (watcher_it != watchers_list.end()) {
            watchers_list.erase(watcher_it);
        }
        
        // Clean up empty entries
        if (watchers_list.empty()) {
            var_to_watchers_.erase(var_name);
        }
    }
    
    // Remove watcher
    watchers_.erase(it);
    return true;
}

std::vector<Fiber*> WatcherRegistry::onVariableChanged(
    const std::string& var_name,
    std::function<bool(WatcherId)> evaluator
) {
    std::vector<Fiber*> fired_fibers;
    
 // Find all watchers that depend on this variable
 auto var_it = var_to_watchers_.find(var_name);
 if (var_it == var_to_watchers_.end()) {
 return fired_fibers; // No watchers for this variable
 }

 // Copy the list — evaluator may call updateDependencies which modifies var_to_watchers_
 auto watcher_ids = var_it->second;

 // Re-evaluate each watcher that depends on this variable
 for (WatcherId watcher_id : watcher_ids) {
        auto watcher_it = watchers_.find(watcher_id);
        if (watcher_it == watchers_.end()) {
            continue;  // Watcher was unregistered
        }
        
        Watcher& watcher = watcher_it->second;
        
        // Re-evaluate the condition
        bool new_result = evaluator(watcher_id);
        
        // Edge-triggered: only fire on false→true transition
        if (!watcher.last_result && new_result) {
            // Fire! Update state and mark fiber for resumption
            watcher.last_result = new_result;
            fired_fibers.push_back(watcher.fiber);
        } else {
            // Update last result for future comparisons
            watcher.last_result = new_result;
        }
    }
    
    return fired_fibers;
}

std::unordered_set<std::string> WatcherRegistry::getWatcherDependencies(WatcherId watcher_id) const {
    auto it = watchers_.find(watcher_id);
    if (it != watchers_.end()) {
        return it->second.dependencies;
    }
    return {};
}

std::vector<WatcherRegistry::WatcherId> WatcherRegistry::getAllWatchers() const {
    std::vector<WatcherId> result;
    for (const auto& [id, watcher] : watchers_) {
        result.push_back(id);
    }
    return result;
}

}  // namespace havel::compiler
