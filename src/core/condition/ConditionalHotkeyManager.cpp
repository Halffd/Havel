#include "core/condition/ConditionalHotkeyManager.hpp"
#include "core/io/IO.hpp"
#include "core/mode/ModeManager.hpp"
#include "core/io/EventListener.hpp"
#include "utils/Logger.hpp"
#include "utils/DebugFlags.hpp"
#include "core/window/WindowMonitor.hpp"
#include "../havel-lang/compiler/runtime/EventQueue.hpp"
#include "../havel-lang/runtime/concurrency/Scheduler.hpp"
#include "core/hotkey/HotkeyActionWrapper.hpp"
#include "../havel-lang/runtime/execution/ExecutionEngine.hpp"

#include <algorithm>
#include <chrono>

namespace havel {

using compiler::EventQueue;

ConditionalHotkeyManager::ConditionalHotkeyManager(std::shared_ptr<IO> io)
: io(io) {
 if (debugging::debug_hotkeys) debug("Initializing ConditionalHotkeyManager (event-driven, no background thread)");
}

ConditionalHotkeyManager::~ConditionalHotkeyManager() {
 Cleanup();
}

void ConditionalHotkeyManager::ScheduleReevaluation() {
 if (eventQueue_) {
  eventQueue_->push([this]() {
   BatchUpdateConditionalHotkeys();
  });
 } else {
  BatchUpdateConditionalHotkeys();
 }
}

void ConditionalHotkeyManager::registerVarChangedHandler() {
 if (eventQueue_) {
  eventQueue_->onEvent(compiler::EventType::VAR_CHANGED,
  [this](const compiler::Event& event) {
   std::string var_name;
   if (event.ptr) {
    auto* sp = static_cast<std::string*>(event.ptr);
    var_name = *sp;
    delete sp;
   }
   if (debugging::debug_hotkeys) debug("VAR_CHANGED event for '{}' - reevaluating conditional hotkeys", var_name);
   if (!var_name.empty()) {
    eventQueue_->push([this, var_name]() {
     UpdateConditionalHotkeysForVariable(var_name);
    });
   } else {
    ScheduleReevaluation();
   }
  });
  if (debugging::debug_hotkeys) debug("ConditionalHotkeyManager: Registered VAR_CHANGED handler with variable filtering");
 }
}

int ConditionalHotkeyManager::AddConditionalHotkey(
const std::string& key, const std::string& condition,
std::function<void()> trueAction, std::function<void()> falseAction, int id, bool async) {
 if (id == 0) {
  static int nextId = 1000;
  id = nextId++;
 }

auto action = [this, condition, trueAction, falseAction, async]() {
 if (trueAction) {
 try {
 if (async && scheduler_) {
 auto fiber = HotkeyActionWrapper::createActionFiber(
 "hotkey_action_" + condition, trueAction);
 if (fiber) {
 scheduler_->addActionFiber(fiber, havel::compiler::FiberPriority::HOTKEY);
 if (debugging::debug_hotkeys) debug("Scheduled async hotkey action for condition: {}", condition);
 } else {
 error("Failed to create Fiber for async hotkey action, falling back to sync");
 trueAction();
 }
 } else {
 trueAction();
 }
 } catch (const std::exception& e) {
 error("ConditionalHotkeyManager: Exception in hotkey true action: {}", e.what());
 } catch (...) {
 error("ConditionalHotkeyManager: Unknown exception in hotkey true action");
 }
}
};

ConditionalHotkey ch;
 ch.id = id;
 ch.key = key;
 ch.condition = condition;
 ch.trueAction = trueAction;
 ch.falseAction = falseAction;
 ch.currentlyGrabbed = true;
 ch.monitoringEnabled = true;
 ch.async = async;

 {
  std::lock_guard<std::mutex> lock(hotkeyMutex);
  conditionalHotkeys.push_back(ch);
  conditionalHotkeyIds.push_back(id);
 }

 io->Hotkey(key, action, "", id);
 UpdateConditionalHotkey(conditionalHotkeys.back());

 return id;
}

int ConditionalHotkeyManager::AddConditionalHotkey(
const std::string& key, std::function<bool()> condition,
std::function<void()> trueAction, std::function<void()> falseAction, int id, bool async) {
 if (id == 0) {
  static int nextId = 2000;
  id = nextId++;
 }

auto action = [this, condition, trueAction, falseAction, async]() {
 if (trueAction) {
 try {
 if (async && scheduler_) {
 auto fiber = HotkeyActionWrapper::createActionFiber(
 "hotkey_action_function", trueAction);
 if (fiber) {
 scheduler_->addActionFiber(fiber, havel::compiler::FiberPriority::HOTKEY);
 if (debugging::debug_hotkeys) debug("Scheduled async hotkey action for function condition");
 } else {
 error("Failed to create Fiber for async hotkey action, falling back to sync");
 trueAction();
 }
 } else {
 trueAction();
 }
 } catch (const std::exception& e) {
 error("ConditionalHotkeyManager: Exception in hotkey true action: {}", e.what());
 } catch (...) {
 error("ConditionalHotkeyManager: Unknown exception in hotkey true action");
 }
 }
 };

 ConditionalHotkey ch;
 ch.id = id;
 ch.key = key;
 ch.condition = condition;
 ch.trueAction = trueAction;
 ch.falseAction = falseAction;
 ch.currentlyGrabbed = true;
 ch.monitoringEnabled = true;
 ch.async = async;

 {
  std::lock_guard<std::mutex> lock(hotkeyMutex);
  conditionalHotkeys.push_back(ch);
  conditionalHotkeyIds.push_back(id);
 }

 io->Hotkey(key, action, "", id);

 return id;
}

bool ConditionalHotkeyManager::RemoveConditionalHotkey(int id) {
 std::lock_guard<std::mutex> lock(hotkeyMutex);

 auto it = std::find_if(conditionalHotkeys.begin(), conditionalHotkeys.end(),
 [id](const ConditionalHotkey& ch) { return ch.id == id; });

 if (it == conditionalHotkeys.end()) {
  return false;
 }

 if (it->currentlyGrabbed) {
  io->UngrabHotkey(id);
 }

 conditionalHotkeys.erase(it);

 auto idIt = std::find(conditionalHotkeyIds.begin(), conditionalHotkeyIds.end(), id);
 if (idIt != conditionalHotkeyIds.end()) {
  conditionalHotkeyIds.erase(idIt);
 }

 return true;
}

bool ConditionalHotkeyManager::SetHotkeyMonitoring(int id, bool enabled) {
 std::lock_guard<std::mutex> lock(hotkeyMutex);

 auto it = std::find_if(conditionalHotkeys.begin(), conditionalHotkeys.end(),
 [id](const ConditionalHotkey& ch) { return ch.id == id; });

 if (it == conditionalHotkeys.end()) {
  return false;
 }

 it->monitoringEnabled = enabled;

 if (!enabled && it->currentlyGrabbed) {
  io->UngrabHotkey(id);
  it->currentlyGrabbed = false;
 }

 return true;
}

ConditionalHotkey* ConditionalHotkeyManager::FindHotkey(int id) {
 std::lock_guard<std::mutex> lock(hotkeyMutex);

 auto it = std::find_if(conditionalHotkeys.begin(), conditionalHotkeys.end(),
 [id](const ConditionalHotkey& ch) { return ch.id == id; });

 return (it != conditionalHotkeys.end()) ? &(*it) : nullptr;
}

void ConditionalHotkeyManager::UpdateAllConditionalHotkeys() {
 BatchUpdateConditionalHotkeys();
}

void ConditionalHotkeyManager::UpdateConditionalHotkeysForVariable(const std::string& var_name) {
 if (!enabled) {
  return;
 }

 std::vector<int> toGrab;
 std::vector<int> toUngrab;

 {
  std::lock_guard<std::mutex> lock(hotkeyMutex);

  for (auto& ch : conditionalHotkeys) {
   if (!ch.monitoringEnabled) continue;

   bool depends = ch.dependencies.empty() ||
   ch.dependencies.count(var_name) > 0;
   if (!depends) continue;

   bool shouldGrab = false;
   if (std::holds_alternative<std::string>(ch.condition)) {
    shouldGrab = EvaluateCondition(std::get<std::string>(ch.condition));
   } else if (std::holds_alternative<std::function<bool()>>(ch.condition)) {
    const auto& func = std::get<std::function<bool()>>(ch.condition);
    if (func) shouldGrab = func();
   }

   if (shouldGrab && !ch.currentlyGrabbed) {
    toGrab.push_back(ch.id);
    ch.currentlyGrabbed = true;
    ch.lastConditionResult = true;
   } else if (!shouldGrab && ch.currentlyGrabbed) {
    toUngrab.push_back(ch.id);
    ch.currentlyGrabbed = false;
    ch.lastConditionResult = false;
   }
  }
 }

 for (int id : toGrab) {
  io->GrabHotkey(id);
 }
 for (int id : toUngrab) {
  io->UngrabHotkey(id);
 }
}

void ConditionalHotkeyManager::ForceUpdateAllConditionalHotkeys() {
 BatchUpdateConditionalHotkeys();
}

void ConditionalHotkeyManager::ReevaluateConditionalHotkeys() {
 std::lock_guard<std::mutex> lock(hotkeyMutex);

 for (auto& ch : conditionalHotkeys) {
  if (!ch.monitoringEnabled) continue;

  bool shouldGrab = false;

  if (std::holds_alternative<std::function<bool()>>(ch.condition)) {
   const auto& func = std::get<std::function<bool()>>(ch.condition);
   if (func) {
    shouldGrab = func();
   }
  } else if (std::holds_alternative<std::string>(ch.condition)) {
   shouldGrab = EvaluateCondition(std::get<std::string>(ch.condition));
  }

  if (shouldGrab && !ch.currentlyGrabbed) {
   io->GrabHotkey(ch.id);
   ch.currentlyGrabbed = true;
   ch.lastConditionResult = true;
  } else if (!shouldGrab && ch.currentlyGrabbed) {
   io->UngrabHotkey(ch.id);
   ch.currentlyGrabbed = false;
   ch.lastConditionResult = false;
  }
 }
}

bool ConditionalHotkeyManager::Suspend() {
 try {
  if (wasSuspended) {
   enabled = true;

   if (!suspendedHotkeyStates.empty()) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    for (const auto& state : suspendedHotkeyStates) {
     auto it = std::find_if(
     conditionalHotkeys.begin(), conditionalHotkeys.end(),
     [state](const ConditionalHotkey& ch) { return ch.id == state.id; });

     if (it != conditionalHotkeys.end()) {
      if (state.wasGrabbed && !it->currentlyGrabbed) {
       io->GrabHotkey(state.id);
       it->currentlyGrabbed = true;
      } else if (!state.wasGrabbed && it->currentlyGrabbed) {
       io->UngrabHotkey(state.id);
       it->currentlyGrabbed = false;
      }
     }
    }
    suspendedHotkeyStates.clear();
   } else {
    ReevaluateConditionalHotkeys();
   }

   wasSuspended = false;
   return true;
  } else {
   enabled = false;

   {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    suspendedHotkeyStates.clear();
    for (auto& ch : conditionalHotkeys) {
     if (!ch.monitoringEnabled) continue;

     ConditionalHotkeyState state;
     state.id = ch.id;
     state.wasGrabbed = ch.currentlyGrabbed;
     suspendedHotkeyStates.push_back(state);

     if (ch.currentlyGrabbed) {
      io->UngrabHotkey(ch.id);
      ch.currentlyGrabbed = false;
     }
    }
   }

   wasSuspended = true;
   return true;
  }
 } catch (const std::exception& e) {
  error("Error in ConditionalHotkeyManager::Suspend: {}", e.what());
  return false;
 }
}

bool ConditionalHotkeyManager::Resume() {
 return Suspend();
}

void ConditionalHotkeyManager::UpdateConditionalHotkey(ConditionalHotkey& hotkey) {
 if (!enabled || !hotkey.monitoringEnabled) {
  return;
 }

 if (verboseLogging) {
  if (std::holds_alternative<std::function<bool()>>(hotkey.condition)) {
   if (debugging::debug_hotkeys) debug("Updating conditional hotkey - Key: '{}', Function Condition, CurrentlyGrabbed: {}",
    hotkey.key, hotkey.currentlyGrabbed);
  } else {
   if (debugging::debug_hotkeys) debug("Updating conditional hotkey - Key: '{}', Condition: '{}', CurrentlyGrabbed: {}",
    hotkey.key, std::get<std::string>(hotkey.condition), hotkey.currentlyGrabbed);
  }
 }

 bool conditionMet;

 if (std::holds_alternative<std::function<bool()>>(hotkey.condition)) {
  const auto& func = std::get<std::function<bool()>>(hotkey.condition);
  conditionMet = func ? func() : false;
 } else {
  const auto& condStr = std::get<std::string>(hotkey.condition);
  conditionMet = EvaluateCondition(condStr);
 }

 if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
  if (std::holds_alternative<std::function<bool()>>(hotkey.condition)) {
   info("Function condition changed: {} for {} - was:{} now:{}",
    conditionMet ? 1 : 0, hotkey.key, hotkey.lastConditionResult, conditionMet);
  } else {
   info("Condition changed: {} for {} ({}) - was:{} now:{}",
    conditionMet ? 1 : 0, std::get<std::string>(hotkey.condition), hotkey.key,
    hotkey.lastConditionResult, conditionMet);
  }
 }

 UpdateHotkeyState(hotkey, conditionMet);
}

void ConditionalHotkeyManager::UpdateHotkeyState(ConditionalHotkey& hotkey, bool conditionMet) {
 if (conditionMet && !hotkey.currentlyGrabbed) {
  io->GrabHotkey(hotkey.id);
  hotkey.currentlyGrabbed = true;
  if (verboseLogging) {
   if (std::holds_alternative<std::string>(hotkey.condition)) {
    if (debugging::debug_hotkeys) debug("Grabbed conditional hotkey: {} ({})", hotkey.key,
     std::get<std::string>(hotkey.condition));
   } else {
    if (debugging::debug_hotkeys) debug("Grabbed conditional hotkey: {} (function condition)", hotkey.key);
   }
  }
 } else if (!conditionMet && hotkey.currentlyGrabbed) {
  io->UngrabHotkey(hotkey.id);
  hotkey.currentlyGrabbed = false;
  if (verboseLogging) {
   if (std::holds_alternative<std::string>(hotkey.condition)) {
    if (debugging::debug_hotkeys) debug("Ungrabbed conditional hotkey: {} ({})", hotkey.key,
     std::get<std::string>(hotkey.condition));
   } else {
    if (debugging::debug_hotkeys) debug("Ungrabbed conditional hotkey: {} (function condition)", hotkey.key);
   }
  }
 }

 hotkey.lastConditionResult = conditionMet;
}

void ConditionalHotkeyManager::BatchUpdateConditionalHotkeys() {
 if (!enabled) {
  return;
 }

 std::vector<int> toGrab;
 std::vector<int> toUngrab;

 for (auto& ch : conditionalHotkeys) {
  if (!ch.monitoringEnabled) continue;

  bool shouldGrab = false;
  if (std::holds_alternative<std::string>(ch.condition)) {
   shouldGrab = EvaluateCondition(std::get<std::string>(ch.condition));
  } else if (std::holds_alternative<std::function<bool()>>(ch.condition)) {
   const auto& func = std::get<std::function<bool()>>(ch.condition);
   if (func) shouldGrab = func();
  }

  if (shouldGrab && !ch.currentlyGrabbed) {
   toGrab.push_back(ch.id);
   ch.currentlyGrabbed = true;
   ch.lastConditionResult = true;
  } else if (!shouldGrab && ch.currentlyGrabbed) {
   toUngrab.push_back(ch.id);
   ch.currentlyGrabbed = false;
   ch.lastConditionResult = false;
  }
 }

 for (int id : toGrab) {
  io->GrabHotkey(id);
 }
 for (int id : toUngrab) {
  io->UngrabHotkey(id);
 }
}

void ConditionalHotkeyManager::Cleanup() {
 {
  std::lock_guard<std::mutex> lock(hotkeyMutex);
  for (auto& ch : conditionalHotkeys) {
   if (ch.currentlyGrabbed) {
    io->UngrabHotkey(ch.id);
    ch.currentlyGrabbed = false;
   }
  }
  conditionalHotkeys.clear();
  conditionalHotkeyIds.clear();
 }
}

void ConditionalHotkeyManager::SetMode(const std::string& newMode) {
 if (auto mgr = modeManager.lock()) {
  mgr->setMode(newMode);
 } else {
  if (debugging::debug_hotkeys) debug("SetMode: no ModeManager, mode change '{}' queued for reevaluation", newMode);
 }
 if (debugging::debug_hotkeys) debug("Mode changed to: {}", newMode);
 ScheduleReevaluation();
}

std::string ConditionalHotkeyManager::GetMode() const {
 if (auto mgr = modeManager.lock()) {
  return mgr->getCurrentMode();
 }
 return "default";
}

bool ConditionalHotkeyManager::EvaluateCondition(const std::string& condition) {
 return EvaluateConditionInternal(condition);
}

bool ConditionalHotkeyManager::EvaluateConditionInternal(const std::string& condition) {
 std::string currentModeVal = GetMode();

 if (condition.find("mode == 'gaming'") != std::string::npos) {
  return (currentModeVal == "gaming");
 } else if (condition.find("mode != 'gaming'") != std::string::npos) {
  return (currentModeVal != "gaming");
 } else if (condition.find("mode == '") != std::string::npos) {
  size_t start = condition.find("mode == '") + 9;
  size_t end = condition.find("'", start);
  if (end != std::string::npos) {
   std::string modeVal = condition.substr(start, end - start);
   return (currentModeVal == modeVal);
  }
 }

 std::string currentTitle;
 std::string currentClass;
 std::string currentExe;

 if (windowMonitor) {
  auto windowInfo = windowMonitor->GetActiveWindowInfo();
  if (windowInfo.has_value()) {
   currentTitle = windowInfo->title;
   currentClass = windowInfo->windowClass;
   currentExe = windowInfo->processName;
  }
 }

 if (currentTitle.empty()) {
  currentTitle = io->GetActiveWindowTitle();
  currentClass = io->GetActiveWindowClass();
 }

 if (condition.find("window.title ==") != std::string::npos ||
 condition.find("window.title==") != std::string::npos) {
  size_t start = condition.find("'");
  if (start != std::string::npos) {
   size_t end = condition.find("'", start + 1);
   if (end != std::string::npos) {
    std::string titleVal = condition.substr(start + 1, end - start - 1);
    return (currentTitle.find(titleVal) != std::string::npos);
   }
  }
 }

 if (condition.find("title ==") != std::string::npos ||
 condition.find("title==") != std::string::npos) {
  size_t start = condition.find("'");
  if (start != std::string::npos) {
   size_t end = condition.find("'", start + 1);
   if (end != std::string::npos) {
    std::string titleVal = condition.substr(start + 1, end - start - 1);
    return (currentTitle.find(titleVal) != std::string::npos);
   }
  }
 }

 if (condition.find("window.class ==") != std::string::npos ||
 condition.find("window.class==") != std::string::npos) {
  size_t start = condition.find("'");
  if (start != std::string::npos) {
   size_t end = condition.find("'", start + 1);
   if (end != std::string::npos) {
    std::string classVal = condition.substr(start + 1, end - start - 1);
    return (currentClass.find(classVal) != std::string::npos);
   }
  }
 }

 if (condition.find("class ==") != std::string::npos ||
 condition.find("class==") != std::string::npos) {
  size_t start = condition.find("'");
  if (start != std::string::npos) {
   size_t end = condition.find("'", start + 1);
   if (end != std::string::npos) {
    std::string classVal = condition.substr(start + 1, end - start - 1);
    return (currentClass.find(classVal) != std::string::npos);
   }
  }
 }

 if (!currentExe.empty() && (condition.find("exe ==") != std::string::npos ||
 condition.find("exe==") != std::string::npos)) {
  size_t start = condition.find("'");
  if (start != std::string::npos) {
   size_t end = condition.find("'", start + 1);
   if (end != std::string::npos) {
    std::string exeVal = condition.substr(start + 1, end - start - 1);
    return (currentExe.find(exeVal) != std::string::npos);
   }
  }
 }

 return false;
}

} // namespace havel
