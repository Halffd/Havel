#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>

namespace havel {

class IO;
class DynamicConditionEvaluator;

/**
 * ModeManager - Dynamic mode system for Havel
 * 
 * Supports script-defined modes with:
 * - Dynamic conditions (exe, class, title, time, battery, cpu, etc.)
 * - Enter/exit callbacks
 * - Mode transitions with previous mode tracking
 * 
 * Usage in Havel script:
 * ```havel
 * modes {
 *     gaming {
 *         condition = exe == "steam.exe" || class in ["steam", "lutris"]
 *         enter { brightness(50); volume(80) }
 *         exit { brightness(100); volume(50) }
 *     }
 * }
 * 
 * when mode gaming {
 *     F1 => audio.mute()
 * }
 * ```
 */
class ModeManager {
public:
    ModeManager(std::shared_ptr<IO> io);
    ~ModeManager();

    // Mode definition
    struct ModeDefinition {
        std::string name;
        std::string conditionStr;     // Original condition string
        std::function<bool()> condition;  // Dynamic condition evaluator
        std::function<void()> onEnter;    // Called when mode activates
        std::function<void()> onExit;     // Called when mode deactivates
        bool isActive = false;
    };

    // Register a mode definition
    void defineMode(ModeDefinition mode);

    // Get current mode name
    std::string getCurrentMode() const;

    // Get previous mode name
    std::string getPreviousMode() const;

    // Set mode explicitly (user-triggered)
    void setMode(const std::string& modeName);

    // Update all mode conditions (called periodically)
    void update();

    // Get all defined modes
    const std::vector<ModeDefinition>& getModes() const { return modes; }

    // Evaluate a condition string
    bool evaluateCondition(const std::string& condition);

private:
    std::shared_ptr<IO> io;
    std::unique_ptr<DynamicConditionEvaluator> evaluator;
    std::vector<ModeDefinition> modes;
    std::string currentMode;
    std::string previousMode;
    mutable std::mutex modeMutex;

    void triggerEnter(ModeDefinition& mode);
    void triggerExit(ModeDefinition& mode);
};

} // namespace havel
