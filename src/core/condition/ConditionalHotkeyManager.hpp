#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace havel {

class IO;
class EventListener;
class WindowMonitor;
class HotkeyActionWrapper;

namespace compiler {
class EventQueue;
class ExecutionEngine;
class Scheduler;
}

struct ConditionalHotkey {
  int id;
  std::string key;
  std::variant<std::string, std::function<bool()>> condition;
  std::function<void()> trueAction;
  std::function<void()> falseAction;
  bool currentlyGrabbed = false;
  bool lastConditionResult = false;
  bool monitoringEnabled = true;
  bool async = false;
  std::unordered_set<std::string> dependencies;
};

struct ConditionalHotkeyState {
  int id;
  bool wasGrabbed;
};

class ConditionalHotkeyManager {
public:
  ConditionalHotkeyManager(std::shared_ptr<IO> io);
  ~ConditionalHotkeyManager();

  int AddConditionalHotkey(const std::string& key, const std::string& condition,
    std::function<void()> trueAction,
    std::function<void()> falseAction = nullptr,
    int id = 0, bool async = false);

  int AddConditionalHotkey(const std::string& key, std::function<bool()> condition,
    std::function<void()> trueAction,
    std::function<void()> falseAction = nullptr,
    int id = 0, bool async = false);

  bool RemoveConditionalHotkey(int id);
  bool SetHotkeyMonitoring(int id, bool enabled);
  ConditionalHotkey* FindHotkey(int id);

  void UpdateAllConditionalHotkeys();
  void UpdateConditionalHotkeysForVariable(const std::string& var_name);
  void ForceUpdateAllConditionalHotkeys();
  void ReevaluateConditionalHotkeys();
  void ScheduleReevaluation();

  void setEventQueue(compiler::EventQueue* eq) { eventQueue_ = eq; }
  void registerVarChangedHandler();

  bool Suspend();
  bool Resume();
  void Cleanup();

  bool IsEnabled() const { return enabled; }
  void SetEnabled(bool e) { enabled = e; }

  void SetMode(const std::string& newMode);
  std::string GetMode() const;

  bool EvaluateCondition(const std::string& condition);

  std::vector<ConditionalHotkey>& GetHotkeys() { return conditionalHotkeys; }
  const std::vector<ConditionalHotkey>& GetHotkeys() const { return conditionalHotkeys; }
  std::mutex& GetMutex() { return hotkeyMutex; }

  void setModeManager(std::weak_ptr<class ModeManager> mgr) { modeManager = mgr; }
  void setWindowMonitor(std::shared_ptr<WindowMonitor> monitor) { windowMonitor = monitor; }
  void setScheduler(class compiler::Scheduler* sched) { scheduler_ = sched; }

  bool verboseConditionLogging = false;
  bool verboseLogging = false;

private:
  std::shared_ptr<IO> io;
  std::vector<ConditionalHotkey> conditionalHotkeys;
  std::vector<int> conditionalHotkeyIds;
  std::vector<ConditionalHotkeyState> suspendedHotkeyStates;

  std::mutex hotkeyMutex;
  std::atomic<bool> enabled{true};
  std::atomic<bool> wasSuspended{false};

  compiler::EventQueue* eventQueue_ = nullptr;
  class compiler::Scheduler* scheduler_ = nullptr;

  std::weak_ptr<class ModeManager> modeManager;
  std::shared_ptr<WindowMonitor> windowMonitor;

  compiler::ExecutionEngine* executionEngine_ = nullptr;

  void UpdateConditionalHotkey(ConditionalHotkey& hotkey);
  void UpdateHotkeyState(ConditionalHotkey& hotkey, bool conditionMet);
  void BatchUpdateConditionalHotkeys();
  bool EvaluateConditionInternal(const std::string& condition);
};

} // namespace havel
