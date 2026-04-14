#pragma once

#include <string>
#include <unordered_set>
#include <vector>
#include <memory>
#include <cstdint>

namespace havel::compiler {

/**
 * DependencyTracker - Track variable dependencies during condition evaluation
 *
 * Phase 2B: Enables reactive watchers by recording which variables are accessed.
 *
 * Usage:
 *   1. Create tracker: auto tracker = std::make_shared<DependencyTracker>();
 *   2. On scope entry: set_active_tracker(tracker)
 *   3. Evaluate condition: condition()
 *   4. During eval, any getGlobal/getLocal calls notify tracker
 *   5. On scope exit: dependencies = tracker->getDependencies()
 *
 * This enables: "only re-evaluate when X or Y changes" instead of always checking.
 */
class DependencyTracker {
public:
    DependencyTracker() = default;
    ~DependencyTracker() = default;
    
    // Track access to a global variable
    void trackGlobalAccess(const std::string& var_name);
    
    // Track access to a local variable
    void trackLocalAccess(const std::string& var_name);
    
    // Get all accessed variables
    std::unordered_set<std::string> getDependencies() const;
    
    // Get only global variable dependencies
    std::unordered_set<std::string> getGlobalDependencies() const;
    
    // Get only local variable dependencies
    std::unordered_set<std::string> getLocalDependencies() const;
    
    // Clear all tracked dependencies
    void reset();
    
    // Check if tracking is active
    bool isActive() const;

private:
    std::unordered_set<std::string> global_vars_;
    std::unordered_set<std::string> local_vars_;
};

/**
 * DependencyTrackerScope - RAII guard for active tracker management
 *
 * Automatically sets/clears the active tracker for a scope.
 * Ensures cleanup even if exceptions occur.
 */
class DependencyTrackerScope {
public:
    explicit DependencyTrackerScope(std::shared_ptr<DependencyTracker> tracker);
    ~DependencyTrackerScope();
    
    // Get the tracked dependencies
    std::unordered_set<std::string> getDependencies() const;
    
private:
    std::shared_ptr<DependencyTracker> tracker_;
};

/**
 * Global active tracker - set during condition evaluation
 *
 * This is a global thread-local variable set by DependencyTrackerScope.
 * When a variable is accessed (getGlobal/getLocal), it notifies via trackAccess.
 */
extern thread_local std::shared_ptr<DependencyTracker> g_active_tracker;

/**
 * Notify active tracker of variable access
 *
 * Called from VM::getGlobal() and VM::getLocal() to record dependencies.
 */
inline void trackGlobalAccess(const std::string& var_name) {
    if (g_active_tracker) {
        g_active_tracker->trackGlobalAccess(var_name);
    }
}

inline void trackLocalAccess(const std::string& var_name) {
    if (g_active_tracker) {
        g_active_tracker->trackLocalAccess(var_name);
    }
}

}  // namespace havel::compiler
