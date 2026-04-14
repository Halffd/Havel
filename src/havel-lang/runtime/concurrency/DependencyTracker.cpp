#include "DependencyTracker.hpp"

namespace havel::compiler {

// Thread-local active tracker global
thread_local std::shared_ptr<DependencyTracker> g_active_tracker = nullptr;

// ============================================================================
// DependencyTracker Implementation
// ============================================================================

void DependencyTracker::trackGlobalAccess(const std::string& var_name) {
    global_vars_.insert(var_name);
}

void DependencyTracker::trackLocalAccess(const std::string& var_name) {
    local_vars_.insert(var_name);
}

std::unordered_set<std::string> DependencyTracker::getDependencies() const {
    std::unordered_set<std::string> result = global_vars_;
    result.insert(local_vars_.begin(), local_vars_.end());
    return result;
}

std::unordered_set<std::string> DependencyTracker::getGlobalDependencies() const {
    return global_vars_;
}

std::unordered_set<std::string> DependencyTracker::getLocalDependencies() const {
    return local_vars_;
}

void DependencyTracker::reset() {
    global_vars_.clear();
    local_vars_.clear();
}

bool DependencyTracker::isActive() const {
    return !global_vars_.empty() || !local_vars_.empty();
}

// ============================================================================
// DependencyTrackerScope Implementation
// ============================================================================

DependencyTrackerScope::DependencyTrackerScope(std::shared_ptr<DependencyTracker> tracker)
    : tracker_(tracker) {
    if (tracker_) {
        tracker_->reset();
    }
    g_active_tracker = tracker_;
}

DependencyTrackerScope::~DependencyTrackerScope() {
    g_active_tracker = nullptr;
}

std::unordered_set<std::string> DependencyTrackerScope::getDependencies() const {
    if (tracker_) {
        return tracker_->getDependencies();
    }
    return {};
}

}  // namespace havel::compiler
