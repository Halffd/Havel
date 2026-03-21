/*
 * MapManagerService.hpp
 *
 * Input mapping service (JoyToKey-like functionality).
 * Provides key remapping, autofire, turbo, combos, and macros.
 * 
 * Uses MapManager internally, but provides a simpler service interface.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace havel { 
    class MapManager; 
    class IO; 
}

namespace havel::host {

/**
 * MapManagerService - Input mapping and remapping
 * 
 * Provides JoyToKey-like functionality:
 * - Key remapping (key to key, mouse to key, etc.)
 * - Autofire (rapid key presses)
 * - Turbo mode
 * - Combos (multi-key sequences)
 * - Macros (record and playback)
 * - Profiles (multiple mapping sets)
 * - Conditional mappings (based on window/process)
 */
class MapManagerService {
public:
    MapManagerService(std::shared_ptr<IO> io);
    ~MapManagerService();

    // =========================================================================
    // Basic remapping
    // =========================================================================

    /// Map a key to another key
    /// @param sourceKey Source key name (e.g., "a", "F1", "button1")
    /// @param targetKey Target key name
    /// @param id Optional mapping ID
    /// @return true if created
    bool map(const std::string& sourceKey, const std::string& targetKey, int id = 0);

    /// Remap keys bidirectionally
    /// @param key1 First key
    /// @param key2 Second key
    /// @return true if created
    bool remap(const std::string& key1, const std::string& key2);

    /// Unmap a key
    /// @param sourceKey Source key to unmap
    /// @return true if removed
    bool unmap(const std::string& sourceKey);

    /// Clear all mappings
    void clearAll();

    // =========================================================================
    // Autofire
    // =========================================================================

    /// Enable autofire for a key
    /// @param sourceKey Source key
    /// @param intervalMs Fire interval in milliseconds
    /// @return true if enabled
    bool enableAutofire(const std::string& sourceKey, int intervalMs = 100);

    /// Disable autofire for a key
    bool disableAutofire(const std::string& sourceKey);

    /// Set autofire rate
    bool setAutofireRate(const std::string& sourceKey, int rateMs);

    // =========================================================================
    // Turbo mode
    // =========================================================================

    /// Enable turbo mode for a key
    /// @param sourceKey Source key
    /// @param intervalMs Turbo interval in milliseconds
    /// @return true if enabled
    bool enableTurbo(const std::string& sourceKey, int intervalMs = 50);

    /// Disable turbo mode
    bool disableTurbo(const std::string& sourceKey);

    // =========================================================================
    // Profiles
    // =========================================================================

    /// Create a new profile
    /// @param name Profile name
    /// @return true if created
    bool createProfile(const std::string& name);

    /// Switch to profile
    /// @param name Profile name
    /// @return true if switched
    bool switchProfile(const std::string& name);

    /// Get current profile name
    std::string getCurrentProfile() const;

    /// Get list of profile names
    std::vector<std::string> getProfileNames() const;

    // =========================================================================
    // Conditional mappings
    // =========================================================================

    /// Add conditional mapping (active when window matches)
    /// @param sourceKey Source key
    /// @param targetKey Target key
    /// @param windowPattern Window title/class pattern (regex)
    /// @return true if created
    bool addConditionalMapping(const std::string& sourceKey,
                                const std::string& targetKey,
                                const std::string& windowPattern);

    // =========================================================================
    // Status
    // =========================================================================

    /// Check if a key is mapped
    bool isMapped(const std::string& sourceKey) const;

    /// Get number of mappings
    int getMappingCount() const;

private:
    std::shared_ptr<havel::MapManager> m_manager;
};

} // namespace havel::host
