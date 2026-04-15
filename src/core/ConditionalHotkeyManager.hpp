#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace havel {

// Forward declarations
class IO;
class EventListener;
class WindowMonitor;

namespace compiler {
class EventQueue;
class ExecutionEngine;
}

/**
 * ConditionalHotkey - A hotkey with a condition
 *
 * Condition can be:
 * - String: evaluated via condition engine (e.g., "mode == 'gaming'")
 * - Function: direct C++ callback
 * 
 * Phase 2F: Integrated with reactive watcher system
 * - watcher_id: ID in WatcherRegistry (0 if not using reactive system)
 * - Condition changes trigger edge-detect watchers (false→true)
 */
struct ConditionalHotkey {
  int id;
  std::string key;
  std::variant<std::string, std::function<bool()>> condition;
  std::function<void()> trueAction;
  std::function<void()> falseAction;
  bool currentlyGrabbed = false;
  bool lastConditionResult = false;
  bool monitoringEnabled = true;
  
  // Phase 2F: Reactive watcher integration
  uint32_t watcher_id = 0;  // 0 = not using reactive system, else WatcherRegistry::WatcherId
};

// Stores the state of conditional hotkeys before suspension
struct ConditionalHotkeyState {
  int id;
  bool wasGrabbed;
};

/**
 * ConditionalHotkeyManager manages hotkeys that have conditions attached to them.
 * It handles grabbing/ungrabbing hotkeys based on condition evaluation.
 * 
 * ARCHITECTURE: Event-driven (no background thread)
 * - Mode changes, window focus changes, etc. push reevaluation to EventQueue
 * - EventQueue processes all pending callbacks in the main event loop
 * - This ensures conditions have access to shared globals/heap/stack
 * - The main loop calls UpdateAllConditionalHotkeys() each frame
 */
class ConditionalHotkeyManager {
public:
  ConditionalHotkeyManager(std::shared_ptr<IO> io);
  ~ConditionalHotkeyManager();

  // Register a conditional hotkey with string condition
  int AddConditionalHotkey(const std::string& key, const std::string& condition,
                           std::function<void()> trueAction,
                           std::function<void()> falseAction = nullptr,
                           int id = 0);

  // Register a conditional hotkey with function condition
  int AddConditionalHotkey(const std::string& key, std::function<bool()> condition,
                           std::function<void()> trueAction,
                           std::function<void()> falseAction = nullptr,
                           int id = 0);

  // Remove a conditional hotkey
  bool RemoveConditionalHotkey(int id);

  // Enable/disable monitoring for a specific hotkey
  bool SetHotkeyMonitoring(int id, bool enabled);

  // Get hotkey by ID
  ConditionalHotkey* FindHotkey(int id);

  // Evaluate all conditional hotkeys (called from main event loop)
  void UpdateAllConditionalHotkeys();

  // Force update all hotkeys (immediate, bypasses cache)
  void ForceUpdateAllConditionalHotkeys();

  // Reevaluate hotkeys based on current mode/state (called from EventQueue callback)
  void ReevaluateConditionalHotkeys();

  // Schedule reevaluation via EventQueue (thread-safe, called from any thread)
  void ScheduleReevaluation();

  // Set the event queue for scheduling reevaluation
  void setEventQueue(compiler::EventQueue* eq) { eventQueue_ = eq; }
  
  // Phase 2G: Register for VAR_CHANGED events to trigger reactive hotkey reevaluation
  void registerVarChangedHandler();
  
  // Phase 2F: Set the execution engine for reactive watcher integration
  // Allows hotkey conditions to be registered as watchers
  void setExecutionEngine(compiler::ExecutionEngine* ee) { executionEngine_ = ee; }

  // Suspend all conditional hotkeys (save state and ungrab all)
  bool Suspend();

  // Resume all conditional hotkeys (restore state)
  bool Resume();

  // Cleanup
  void Cleanup();

  // Get/set enabled state
  bool IsEnabled() const { return enabled; }
  void SetEnabled(bool e) { enabled = e; }

  // Mode management
  void SetMode(const std::string& newMode);
  std::string GetMode() const;

  // Condition evaluation
  bool EvaluateCondition(const std::string& condition);

  // Get access to hotkeys for direct manipulation
  std::vector<ConditionalHotkey>& GetHotkeys() { return conditionalHotkeys; }
  const std::vector<ConditionalHotkey>& GetHotkeys() const { return conditionalHotkeys; }

  // Get mutex for thread-safe operations
  std::mutex& GetMutex() { return hotkeyMutex; }

  // Set condition evaluation function
  void SetConditionEvaluator(std::function<bool(const std::string&)> evaluator) {
    conditionEvaluator = evaluator;
  }

  // Set gaming mode checker
  void SetGamingModeChecker(std::function<bool()> checker) {
    isGamingModeActive = checker;
  }

  // Set mode manager for automatic mode updates
  void setModeManager(std::weak_ptr<class ModeManager> mgr) { modeManager = mgr; }

  // Set window monitor for efficient window info caching
  void setWindowMonitor(std::shared_ptr<WindowMonitor> monitor) { windowMonitor = monitor; }

  // Debug options
  bool verboseConditionLogging = false;
  bool verboseLogging = false;

private:
  std::shared_ptr<IO> io;  // Shared ownership
  std::vector<ConditionalHotkey> conditionalHotkeys;
  std::vector<int> conditionalHotkeyIds;
  std::vector<int> gamingHotkeyIds;
  std::vector<ConditionalHotkeyState> suspendedHotkeyStates;

  std::mutex hotkeyMutex; // Protects conditionalHotkeys and related structures
  std::atomic<bool> enabled{true};
  std::atomic<bool> wasSuspended{false};

  // Event queue for scheduling reevaluation from any thread
  compiler::EventQueue* eventQueue_ = nullptr;
  
  // Phase 2F: Execution engine for reactive watcher integration
  compiler::ExecutionEngine* executionEngine_ = nullptr;

  // Condition evaluation cache
  struct CachedCondition {
    bool result;
    std::chrono::steady_clock::time_point timestamp;
  };
  std::unordered_map<std::string, CachedCondition> conditionCache;
  std::mutex conditionCacheMutex;
  static constexpr int CACHE_DURATION_MS = 50;

  // Cleanup flag
  std::atomic<bool> inCleanupMode{false};

  // Condition evaluation function (can be overridden)
  std::function<bool(const std::string&)> conditionEvaluator;

  // Gaming mode checker
  std::function<bool()> isGamingModeActive;

  // Mode manager (weak reference to avoid circular dependency)
  std::weak_ptr<class ModeManager> modeManager;

  // Window monitor for efficient window info caching
  std::shared_ptr<WindowMonitor> windowMonitor;

  // Mode management
  static std::mutex modeMutex;
  static std::string currentMode;

  // Internal methods
  void UpdateConditionalHotkey(ConditionalHotkey& hotkey);
  void UpdateHotkeyState(ConditionalHotkey& hotkey, bool conditionMet);
  void BatchUpdateConditionalHotkeys();
  bool EvaluateConditionInternal(const std::string& condition);
  void InvalidateConditionalHotkeys();
};

} // namespace havel
