#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include "../IO.hpp"

namespace havel {

/**
 * MapManager - Advanced input mapping system similar to JoyToKey
 * 
 * Features:
 * - Multiple profiles (e.g., "Gaming", "Desktop", "Browser")
 * - Conditional mappings (activate based on active window, process, etc.)
 * - Autofire (rapid key/button presses)
 * - Turbo mode (hold to repeat)
 * - Combos (multi-key sequences)
 * - Macros (record and playback key sequences)
 * - Mouse movement mapping (joystick to mouse)
 * - Sensitivity per mapping
 * - Toggle/Hold modes
 * - Profile switching via hotkeys
 */

// Forward declarations
class IO;

// Mapping types
enum class MappingType {
    KeyToKey,           // Keyboard key to keyboard key
    KeyToMouse,         // Keyboard key to mouse button/movement
    MouseToKey,         // Mouse button to keyboard key
    MouseToMouse,       // Mouse button to mouse button
    JoyToKey,           // Joystick button to keyboard key
    JoyToMouse,         // Joystick button to mouse button
    JoyAxisToMouse,     // Joystick axis to mouse movement
    JoyAxisToKey,       // Joystick axis to keyboard key
    Combo,              // Multi-key combo to action
    Macro               // Recorded sequence
};

// Action types
enum class ActionType {
    Press,              // Single press
    Hold,               // Hold while source is held
    Toggle,             // Toggle on/off
    Autofire,           // Rapid fire while held
    Turbo,              // Turbo mode (faster autofire)
    Macro,              // Execute macro
    MouseMove,          // Move mouse
    MouseScroll         // Scroll wheel
};

// Condition types for conditional mappings
enum class ConditionType {
    Always,             // Always active
    WindowTitle,        // Active when window title matches
    WindowClass,        // Active when window class matches
    ProcessName,        // Active when process is running
    Custom              // Custom condition function
};

// Mapping condition
struct MappingCondition {
    ConditionType type = ConditionType::Always;
    std::string pattern;                        // Pattern to match (regex)
    std::function<bool()> customCheck;          // Custom condition function
    
    bool Evaluate() const;
};

// Single key/button mapping
struct Mapping {
    std::string id;                             // Unique identifier
    std::string name;                           // Display name
    bool enabled = true;
    
    // Source input
    MappingType type;
    std::string sourceKey;                      // Source key/button name
    int sourceCode = 0;                         // Evdev code
    
    // Target action
    ActionType actionType;
    std::vector<std::string> targetKeys;        // Target key(s) for action
    std::vector<int> targetCodes;               // Evdev codes
    
    // Autofire settings
    bool autofire = false;
    int autofireInterval = 100;                 // Milliseconds between presses
    bool turbo = false;
    int turboInterval = 50;                     // Faster interval for turbo
    
    // Mouse movement settings (for joystick axis)
    bool mouseMovement = false;
    float sensitivity = 1.0f;
    float deadzone = 0.15f;                     // Ignore small movements
    bool acceleration = false;
    
    // Toggle mode
    bool toggleMode = false;
    bool toggleState = false;
    
    // Macro settings
    std::vector<std::pair<std::string, int>> macroSequence;  // (key, delay_ms)
    
    // Conditions
    std::vector<MappingCondition> conditions;
    
    // Internal state
    std::chrono::steady_clock::time_point lastFireTime;
    std::atomic<bool> active{false};
    
    // Check if mapping should be active
    bool ShouldActivate() const;
};

// Profile - collection of mappings
struct Profile {
    std::string id;
    std::string name;
    std::string description;
    bool enabled = true;
    
    std::vector<Mapping> mappings;
    
    // Profile-level settings
    float globalSensitivity = 1.0f;
    bool enableAutofire = true;
    bool enableMacros = true;
    
    // Find mapping by source key
    Mapping* FindMapping(const std::string& sourceKey);
    const Mapping* FindMapping(const std::string& sourceKey) const;
};

/**
 * MapManager - Manages input mapping profiles
 */
class MapManager {
public:
    MapManager(IO* io);
    ~MapManager();
    
    // Profile management
    void AddProfile(const Profile& profile);
    void RemoveProfile(const std::string& profileId);
    Profile* GetProfile(const std::string& profileId);
    const Profile* GetProfile(const std::string& profileId) const;
    std::vector<std::string> GetProfileIds() const;
    
    void SetActiveProfile(const std::string& profileId);
    std::string GetActiveProfileId() const { return activeProfileId; }
    Profile* GetActiveProfile();
    
    // Mapping management
    void AddMapping(const std::string& profileId, const Mapping& mapping);
    void RemoveMapping(const std::string& profileId, const std::string& mappingId);
    void UpdateMapping(const std::string& profileId, const Mapping& mapping);
    
    Mapping* GetMapping(const std::string& profileId, const std::string& mappingId);
    std::vector<Mapping*> GetMappings(const std::string& profileId);
    
    // Enable/disable
    void EnableProfile(const std::string& profileId, bool enable);
    void EnableMapping(const std::string& profileId, const std::string& mappingId, bool enable);
    
    // Apply mappings to IO system
    void ApplyProfile(const std::string& profileId);
    void ApplyActiveProfile();
    void ClearAllMappings();
    
    // Profile switching
    void NextProfile();
    void PreviousProfile();
    void SetProfileSwitchHotkey(const std::string& hotkey);
    
    // Macro recording
    void StartMacroRecording(const std::string& macroName);
    void StopMacroRecording();
    bool IsMacroRecording() const { return macroRecording; }
    void SaveMacro(const std::string& profileId, const std::string& mappingId);
    
    // Save/Load profiles
    void SaveProfiles(const std::string& filepath);
    void LoadProfiles(const std::string& filepath);
    void SaveProfile(const std::string& profileId, const std::string& filepath);
    void LoadProfile(const std::string& filepath);
    
    // Export/Import (JSON format)
    std::string ExportProfileToJson(const std::string& profileId) const;
    void ImportProfileFromJson(const std::string& json);
    
    // Statistics
    struct MappingStats {
        int activationCount = 0;
        std::chrono::steady_clock::time_point lastActivation;
        int64_t totalDurationMs = 0;
    };
    
    const MappingStats* GetMappingStats(const std::string& profileId, 
                                        const std::string& mappingId) const;
    void ResetStats();
    
private:
    IO* io;
    
    // Profiles
    std::map<std::string, Profile> profiles;
    std::string activeProfileId;
    mutable std::mutex profileMutex;
    
    // Hotkey IDs for cleanup
    std::map<std::string, std::vector<int>> profileHotkeyIds;
    
    // Macro recording
    bool macroRecording = false;
    std::string currentMacroName;
    std::vector<std::pair<std::string, int>> recordedMacro;
    std::chrono::steady_clock::time_point lastMacroEvent;
    
    // Statistics
    std::map<std::string, std::map<std::string, MappingStats>> stats;
    
    // Profile switching hotkey
    int profileSwitchHotkeyId = -1;
    
    // Internal helpers
    void RegisterMapping(const std::string& profileId, Mapping& mapping);
    void UnregisterMapping(const std::string& profileId, const std::string& mappingId);
    
    void ExecuteMapping(Mapping& mapping, bool down);
    void ExecuteAutofire(Mapping& mapping);
    void ExecuteMacro(const Mapping& mapping);
    void ExecuteMouseMovement(const Mapping& mapping, float axisValue);
    
    void RecordMacroEvent(const std::string& key);
    
    std::string GenerateId() const;
};

} // namespace havel
