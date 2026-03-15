#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>

namespace havel {

class IO;

// Forward declare - complete type needed only in .cpp
namespace ast { struct Expression; }

/**
 * ModeManager - Dynamic mode system for Havel
 * 
 * Supports script-defined modes with:
 * - Dynamic conditions (AST expressions, evaluated directly)
 * - Enter/exit callbacks
 * - Mode transitions with previous mode tracking
 * 
 * Usage in Havel script:
 * ```havel
 * mode gaming {
 *     condition = exe == "steam.exe" || class in ["steam", "lutris"]
 *     enter { brightness(50); volume(80) }
 *     exit { brightness(100); volume(50) }
 * }
 * 
 * when mode gaming {
 *     F1 => audio.mute()
 * }
 * ```
 */
class ModeManager {
public:
    ModeManager() = default;
    ~ModeManager();

    // Mode definition - stores AST directly, no reparsing
    // Note: conditionExpr is owned by the ModesBlock AST node
    // ModeManager just holds a non-owning pointer
    struct ModeDefinition {
        std::string name;
        ast::Expression* conditionExpr = nullptr;  // Non-owning pointer to AST
        std::function<void()> onEnter;
        std::function<void()> onExit;
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
    // Pass evaluator function that evaluates AST expressions
    using ExprEvaluator = std::function<bool(const ast::Expression&)>;
    void update(ExprEvaluator evaluator);

    // Get all defined modes
    const std::vector<ModeDefinition>& getModes() const { return modes; }

private:
    std::vector<ModeDefinition> modes;
    std::string currentMode;
    std::string previousMode;
    mutable std::mutex modeMutex;

    void triggerEnter(ModeDefinition& mode);
    void triggerExit(ModeDefinition& mode);
};

} // namespace havel
