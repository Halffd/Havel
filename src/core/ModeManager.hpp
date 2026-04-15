#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace havel {

class IO;
namespace compiler {
class EventQueue;
class ExecutionEngine;
}

// Forward declare - complete type needed only in .cpp
namespace ast {
struct Expression;
}

/**
 * ModeManager - Dynamic mode system for Havel
 *
 * Supports script-defined modes with:
 * - Dynamic conditions (AST expressions, evaluated directly)
 * - Enter/exit callbacks
 * - Mode transitions with previous mode tracking
 * - Priority-based mode switching
 * - Mode statistics and metrics
 * - Signal system for reactive programming
 *
 * Usage in Havel script:
 * ```havel
 * // Signals (facts about system)
 * signal steam_running = window.any(exe == "steam.exe")
 * signal gaming_focus = active.exe == "steam.exe"
 *
 * // Modes (high-level state)
 * mode gaming priority 10 {
 *     condition = gaming_focus
 *     enter { brightness(50); volume(80) }
 *     exit { brightness(100); volume(50) }
 *
 *     // Specific transitions
 *     on enter from "coding" { notify("leaving code for games") }
 *     on exit to "default" { run("killall steam") }
 * }
 *
 * // Reactions
 * when steam_running {
 *     notify("Steam launched")
 * }
 *
 * when mode == "gaming" {
 *     F1 => audio.mute()
 * }
 * ```
 */
class ModeManager {
public:
  ModeManager() = default;
  ~ModeManager();

  // Signal definition
  struct Signal {
    std::string name;
    std::shared_ptr<ast::Expression> conditionExpr;
    bool value = false;
  };

  // Mode definition - stores AST via shared_ptr to prevent use-after-free
  struct ModeDefinition {
    std::string name;
    std::shared_ptr<ast::Expression> conditionExpr; // shared_ptr! (AST-based)
    std::function<bool()>
        conditionCallback; // Callback-based condition (alternative to AST)
    std::function<void()> onEnter;
    std::function<void()> onExit;
    std::function<void(const std::string &fromMode)>
        onEnterFrom; // Called when entering from specific mode
    std::function<void(const std::string &toMode)>
        onExitTo;                     // Called when exiting to specific mode
    std::function<void()> onClose;    // Called when active window closes
    std::function<void()> onMinimize; // Called when active window minimizes
    std::function<void()> onMaximize; // Called when active window maximizes
    std::function<void()> onOpen;     // Called when new window opens
    int priority = 0;
    bool isActive = false;
    std::chrono::steady_clock::time_point enterTime;
    std::chrono::milliseconds totalTime{0}; // Total time spent in this mode
    int transitionCount = 0;                // Number of times entered this mode
    
    // Phase 2F: Reactive watcher integration
    uint32_t watcher_id = 0;  // 0 = not using reactive system, else WatcherRegistry::WatcherId
  };

  // Mode group for batch operations
  struct ModeGroup {
    std::string name;
    std::vector<std::string> modes;
    std::function<void()> onEnter;
  };

  // Register a signal
  void defineSignal(Signal signal);

  // Register a mode definition
  void defineMode(ModeDefinition mode);

  // Register a mode group
  void defineGroup(ModeGroup group);

  // Get current mode name
  std::string getCurrentMode() const;

  // Get previous mode name
  std::string getPreviousMode() const;

  // Set mode explicitly (user-triggered)
  void setMode(const std::string &modeName);

  // Update all mode conditions (called periodically)
  // Pass evaluator function that evaluates AST expressions (optional if using
  // callbacks)
  using ExprEvaluator = std::function<bool(const ast::Expression &)>;
  void update(ExprEvaluator evaluator = nullptr);

  // Get all defined modes
  const std::vector<ModeDefinition> &getModes() const { return modes; }

  // Get mode statistics
  std::chrono::milliseconds getModeTime(const std::string &modeName) const;
  int getModeTransitions(const std::string &modeName) const;

  // Check if signal is active
  bool isSignalActive(const std::string &signalName) const;
  
  // Phase 2F: Set the execution engine for reactive watcher integration
  // Allows mode conditions to be registered as watchers
  void setExecutionEngine(compiler::ExecutionEngine* ee) { executionEngine_ = ee; }
  
  // Phase 2G: Set the event queue for reactive mode updates
  void setEventQueue(compiler::EventQueue* eq) { eventQueue_ = eq; }
  
  // Phase 2G: Register for VAR_CHANGED events to trigger reactive mode reevaluation
  void registerVarChangedHandler();

private:
  std::vector<Signal> signalList;
  std::vector<ModeDefinition> modes;
  std::vector<ModeGroup> groups;
  std::string currentMode;
  std::string previousMode;
  mutable std::mutex modeMutex;
  
  // Phase 2F: Execution engine for reactive watcher integration
  compiler::ExecutionEngine* executionEngine_ = nullptr;
  
  // Phase 2G: Event queue for reactive updates
  compiler::EventQueue* eventQueue_ = nullptr;

  void triggerEnter(ModeDefinition &mode);
  void triggerExit(ModeDefinition &mode);
  void triggerTransition(const std::string &fromMode,
                         const std::string &toMode);
};

} // namespace havel
