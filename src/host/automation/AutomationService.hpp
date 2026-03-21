/*
 * AutomationService.hpp
 *
 * Automation service for auto-clicker, auto-runner, and chained tasks.
 * 
 * Uses AutomationManager internally, but provides a simpler service interface.
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace havel { 
    class AutomationManager; 
    class IO; 
    namespace automation { class AutomationManager; }
}

namespace havel::host {

/**
 * AutomationService - Automation task management
 * 
 * Provides high-level automation:
 * - Auto-clicker: Repeatedly click mouse button
 * - Auto-runner: Repeatedly move mouse in direction
 * - Auto-key-presser: Repeatedly press key
 * - Chained tasks: Sequence of actions with delays
 */
class AutomationService {
public:
    AutomationService(std::shared_ptr<IO> io);
    ~AutomationService();

    // =========================================================================
    // Auto-clicker
    // =========================================================================

    /// Create auto-clicker task
    /// @param name Task name
    /// @param button Mouse button ("left", "right", "middle")
    /// @param intervalMs Click interval in milliseconds
    /// @return true if created
    bool createAutoClicker(const std::string& name, 
                           const std::string& button = "left", 
                           int intervalMs = 100);

    // =========================================================================
    // Auto-runner
    // =========================================================================

    /// Create auto-runner task (repeatedly move mouse)
    /// @param name Task name
    /// @param direction Direction ("up", "down", "left", "right")
    /// @param intervalMs Move interval in milliseconds
    /// @return true if created
    bool createAutoRunner(const std::string& name,
                          const std::string& direction = "right",
                          int intervalMs = 50);

    // =========================================================================
    // Auto-key-presser
    // =========================================================================

    /// Create auto-key-presser task
    /// @param name Task name
    /// @param key Key to press
    /// @param intervalMs Press interval in milliseconds
    /// @return true if created
    bool createAutoKeyPresser(const std::string& name,
                              const std::string& key,
                              int intervalMs = 100);

    // =========================================================================
    // Task control
    // =========================================================================

    /// Get task by name
    /// @param name Task name
    /// @return true if task exists
    bool hasTask(const std::string& name) const;

    /// Remove task by name
    void removeTask(const std::string& name);

    /// Stop all tasks
    void stopAll();

    /// Get list of task names
    std::vector<std::string> getTaskNames() const;

private:
    std::shared_ptr<havel::automation::AutomationManager> m_manager;
};

} // namespace havel::host
