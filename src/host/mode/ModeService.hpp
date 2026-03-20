/*
 * ModeService.hpp
 *
 * Pure C++ mode service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for mode operations.
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>

namespace havel { class ModeManager; }  // Forward declaration

namespace havel::host {

/**
 * ModeService - Pure mode business logic
 *
 * Provides system-level mode operations without any language runtime coupling.
 * All methods return simple C++ types (bool, int, string, vector, etc.)
 */
class ModeService {
public:
    explicit ModeService(std::shared_ptr<havel::ModeManager> manager);
    ~ModeService() = default;

    // =========================================================================
    // Mode queries
    // =========================================================================

    /// Get current mode name
    /// @return mode name (empty if none)
    std::string getCurrentMode() const;

    /// Get previous mode name
    /// @return previous mode name
    std::string getPreviousMode() const;

    /// Get time spent in a mode
    /// @param modeName Mode name (empty for current mode)
    /// @return duration in milliseconds
    std::chrono::milliseconds getModeTime(const std::string& modeName = "") const;

    /// Get number of transitions for a mode
    /// @param modeName Mode name (empty for current mode)
    /// @return transition count
    int getModeTransitions(const std::string& modeName = "") const;

    /// Get all defined mode names
    /// @return vector of mode names
    std::vector<std::string> getModeNames() const;

    // =========================================================================
    // Mode control
    // =========================================================================

    /// Set mode explicitly
    /// @param modeName Mode name
    void setMode(const std::string& modeName);

    // =========================================================================
    // Signal queries
    // =========================================================================

    /// Check if a signal is active
    /// @param signalName Signal name
    /// @return true if signal is active
    bool isSignalActive(const std::string& signalName) const;

private:
    std::shared_ptr<havel::ModeManager> m_manager;  // Shared ownership
};

} // namespace havel::host
