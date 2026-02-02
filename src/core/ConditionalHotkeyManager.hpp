#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace havel {

// Forward declarations
class IO;
class EventListener;

struct ConditionalHotkey {
  int id;
  std::string key;
  std::string condition; // String condition (for legacy)
  std::function<bool()> conditionFunc; // Function condition (for new functionality)
  std::function<void()> trueAction;
  std::function<void()> falseAction;
  bool currentlyGrabbed = false;
  bool lastConditionResult = false;
  bool usesFunctionCondition = false; // Flag to indicate which condition type to use
  bool monitoringEnabled = true; // Whether this hotkey should be monitored
};

// Stores the state of conditional hotkeys before suspension
struct ConditionalHotkeyState {
  int id;
  bool wasGrabbed;
};

/**
 * ConditionalHotkeyManager manages hotkeys that have conditions attached to them.
 * It handles grabbing/ungrabbing hotkeys based on condition evaluation.
 * Works with IO and EventListener for unified input handling.
 */
class ConditionalHotkeyManager {
public:
  ConditionalHotkeyManager(IO& io);
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

  // Evaluate all conditional hotkeys
  void UpdateAllConditionalHotkeys();

  // Force update all hotkeys
  void ForceUpdateAllConditionalHotkeys();

  // Reevaluate hotkeys based on gaming mode
  void ReevaluateConditionalHotkeys();

  // Suspend all conditional hotkeys (save state and ungrab all)
  bool Suspend();

  // Resume all conditional hotkeys (restore state)
  bool Resume();

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

  // Debug options
  bool verboseConditionLogging = false;
  bool verboseLogging = false;

private:
  IO& io;
  std::vector<ConditionalHotkey> conditionalHotkeys;
  std::vector<int> conditionalHotkeyIds;
  std::vector<int> gamingHotkeyIds;
  std::vector<ConditionalHotkeyState> suspendedHotkeyStates;

  std::mutex hotkeyMutex; // Protects conditionalHotkeys and related structures
  std::atomic<bool> enabled{true};
  std::atomic<bool> wasSuspended{false};

  // Condition evaluation cache
  struct CachedCondition {
    bool result;
    std::chrono::steady_clock::time_point timestamp;
  };
  std::unordered_map<std::string, CachedCondition> conditionCache;
  std::mutex conditionCacheMutex;
  static constexpr int CACHE_DURATION_MS = 50;

  // Deferred update queue
  std::queue<int> deferredUpdateQueue;
  std::mutex deferredUpdateMutex;

  // Cleanup flag
  std::atomic<bool> inCleanupMode{false};

  // Condition evaluation function (can be overridden)
  std::function<bool(const std::string&)> conditionEvaluator;
  
  // Gaming mode checker
  std::function<bool()> isGamingModeActive;

  // Mode management
  static std::mutex modeMutex;
  static std::string currentMode;

  // Update loop
  std::thread updateLoopThread;
  std::atomic<bool> updateLoopRunning{false};
  std::condition_variable updateLoopCv;
  std::mutex updateLoopMutex;

  void UpdateLoop();

  // Internal methods
  void UpdateConditionalHotkey(ConditionalHotkey& hotkey);
  void UpdateHotkeyState(ConditionalHotkey& hotkey, bool conditionMet);
  void BatchUpdateConditionalHotkeys();
  bool EvaluateConditionInternal(const std::string& condition);
  void InvalidateConditionalHotkeys();
  void Cleanup();
};

} // namespace havel
