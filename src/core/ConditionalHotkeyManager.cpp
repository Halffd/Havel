#include "ConditionalHotkeyManager.hpp"
#include "IO.hpp"
#include "ModeManager.hpp"
#include "io/EventListener.hpp"
#include "utils/Logger.hpp"
#include "window/WindowMonitor.hpp"
#include "../havel-lang/compiler/runtime/EventQueue.hpp"
#include "HotkeyConditionCompiler.hpp"
#include "HotkeyActionWrapper.hpp"
#include "HotkeyActionContext.hpp"
#include <algorithm>
#include <chrono>

namespace havel {

using compiler::EventQueue;

// Static member initialization
std::mutex ConditionalHotkeyManager::modeMutex;
std::string ConditionalHotkeyManager::currentMode = "default";

ConditionalHotkeyManager::ConditionalHotkeyManager(std::shared_ptr<IO> io)
    : io(io) {
  info("Initializing ConditionalHotkeyManager (event-driven, no background thread)");
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
    // Fallback: update directly
    BatchUpdateConditionalHotkeys();
  }
}

void ConditionalHotkeyManager::registerVarChangedHandler() {
  // Phase 2G: Register to react to variable changes
  // When a global variable changes, reevaluate all hotkey conditions
  // This enables event-driven hotkey condition checking
  if (eventQueue_) {
    // Register handler for VAR_CHANGED events
    eventQueue_->onEvent(compiler::EventType::VAR_CHANGED, 
        [this](const compiler::Event& event) {
          // Reevaluate hotkey conditions when variables change
          // This allows "mode == 'gaming'" or similar conditions to react immediately
          debug("VAR_CHANGED event - reevaluating hotkey conditions");
          ScheduleReevaluation();
        });
    info("ConditionalHotkeyManager: Registered VAR_CHANGED handler for reactive hotkey updates");
  }
}

  int ConditionalHotkeyManager::AddConditionalHotkey(
    const std::string& key, const std::string& condition,
    std::function<void()> trueAction,
    std::function<void()> falseAction, int id) {
  debug("Registering conditional hotkey - Key: '{}', Condition: '{}', ID: {}",
        key, condition, id);

  if (id == 0) {
    static int nextId = 1000;
    id = nextId++;
  }

  // Phase 2H: Try to compile condition if compiler is available
  bool hasCompiledCondition = false;
  if (conditionCompiler_ && bytecodeVM_) {
    try {
      // Try to compile the condition to bytecode using the compiler
      // This caches the bytecode for fast repeated evaluation
      conditionCompiler_->compileCondition(bytecodeVM_, condition);
      hasCompiledCondition = true;
      debug("Compiled hotkey condition to bytecode: '{}'", condition);
    } catch (const std::exception& e) {
      // Fall back to string-based evaluation if compilation fails
      debug("Failed to compile condition '{}': {}, falling back to string evaluation",
            condition, e.what());
      hasCompiledCondition = false;
    }
  }

  auto action = [this, condition, trueAction, falseAction]() {
    if (EvaluateCondition(condition)) {
      if (trueAction) {
        // Phase 2I/2J: Set context and execute hotkey action
        // CRITICAL FIX: Handle exceptions to prevent crashes from blocking hotkey detection
        HotkeyActionContext::setContext({
          true,  // condition_result
          "hotkey_fired",  // changed_variable
          static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),  // timestamp_ns
          "hotkey_action",  // hotkey_name
          condition  // condition_source
        });
        
        try {
          // Execute the action with exception safety
          // TODO: Phase 2I Extended - wrap in Fiber and submit to Scheduler for true async
          // Currently: execute synchronously with context available via thread-local
          trueAction();
        } catch (const std::exception& e) {
          error("ConditionalHotkeyManager: Exception in hotkey true action: {}", e.what());
        } catch (...) {
          error("ConditionalHotkeyManager: Unknown exception in hotkey true action");
        }
        
        // Clear context after execution
        HotkeyActionContext::clearContext();
      }
    } else {
      if (falseAction) {
        // Phase 2J: Set context for false action
        HotkeyActionContext::setContext({
          false,  // condition_result
          "hotkey_fired_false",
          static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
          "hotkey_action_false",
          condition
        });
        
        try {
          falseAction();
        } catch (const std::exception& e) {
          error("ConditionalHotkeyManager: Exception in hotkey false action: {}", e.what());
        } catch (...) {
          error("ConditionalHotkeyManager: Unknown exception in hotkey false action");
        }
        
        HotkeyActionContext::clearContext();
      }
    }
  };

  ConditionalHotkey ch;
  ch.id = id;
  ch.key = key;
  ch.condition = condition;  // std::variant holds function
  ch.trueAction = trueAction;
  ch.falseAction = falseAction;
  ch.currentlyGrabbed = true;
  ch.monitoringEnabled = true;

  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    conditionalHotkeys.push_back(ch);
    conditionalHotkeyIds.push_back(id);
  }

  // Register with IO - no string condition for function-based
  io->Hotkey(key, action, "", id);

  // Initial evaluation
  UpdateConditionalHotkey(conditionalHotkeys.back());

  return id;
}

// Overload for function-based condition (std::function<bool()>)
int ConditionalHotkeyManager::AddConditionalHotkey(
    const std::string& key, std::function<bool()> condition,
    std::function<void()> trueAction,
    std::function<void()> falseAction, int id) {
  debug("Registering function-based conditional hotkey - Key: '{}', ID: {}", key, id);

  if (id == 0) {
    static int nextId = 2000;  // Use different range for function-based hotkeys
    id = nextId++;
  }

  // Create an action that evaluates the condition function
  auto action = [this, condition, trueAction, falseAction]() {
    try {
      if (condition()) {  // Call the condition function directly
        if (trueAction) {
          // Phase 2I/2J: Set context and execute hotkey action
          HotkeyActionContext::setContext({
            true,  // condition_result
            "hotkey_fired",  // changed_variable
            static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),  // timestamp_ns
            "hotkey_action",  // hotkey_name
            "function_condition"  // condition_source
          });
          
          try {
            trueAction();
          } catch (const std::exception& e) {
            error("ConditionalHotkeyManager: Exception in hotkey true action: {}", e.what());
          } catch (...) {
            error("ConditionalHotkeyManager: Unknown exception in hotkey true action");
          }
          
          HotkeyActionContext::clearContext();
        }
      } else {
        if (falseAction) {
          HotkeyActionContext::setContext({
            false,  // condition_result
            "hotkey_fired_false",
            static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
            "hotkey_action_false",
            "function_condition"
          });
          
          try {
            falseAction();
          } catch (const std::exception& e) {
            error("ConditionalHotkeyManager: Exception in hotkey false action: {}", e.what());
          } catch (...) {
            error("ConditionalHotkeyManager: Unknown exception in hotkey false action");
          }
          
          HotkeyActionContext::clearContext();
        }
      }
    } catch (const std::exception& e) {
      error("ConditionalHotkeyManager: Exception evaluating condition function: {}", e.what());
    } catch (...) {
      error("ConditionalHotkeyManager: Unknown exception evaluating condition function");
    }
  };

  ConditionalHotkey ch;
  ch.id = id;
  ch.key = key;
  ch.condition = condition;  // Store the function in the variant directly
  ch.trueAction = trueAction;
  ch.falseAction = falseAction;
  ch.currentlyGrabbed = true;
  ch.monitoringEnabled = true;

  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    conditionalHotkeys.push_back(ch);
    conditionalHotkeyIds.push_back(id);
  }

  // Register with IO
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

  // Ungrab if currently grabbed
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

void ConditionalHotkeyManager::ForceUpdateAllConditionalHotkeys() {
  BatchUpdateConditionalHotkeys();
}

void ConditionalHotkeyManager::ReevaluateConditionalHotkeys() {
  std::lock_guard<std::mutex> lock(hotkeyMutex);

  for (auto& ch : conditionalHotkeys) {
    if (!ch.monitoringEnabled) continue;

    bool shouldGrab = false;

    // Evaluate condition based on variant type
    if (std::holds_alternative<std::function<bool()>>(ch.condition)) {
      // Function condition
      const auto& func = std::get<std::function<bool()>>(ch.condition);
      if (func) {
        shouldGrab = func();
      }
    } else if (std::holds_alternative<std::string>(ch.condition)) {
      // String condition
      const auto& condStr = std::get<std::string>(ch.condition);
      // For mode-based conditions (legacy special handling)
      if (condStr.find("mode == 'gaming'") != std::string::npos) {
        shouldGrab = isGamingModeActive ? isGamingModeActive() : false;
      } else if (condStr.find("mode != 'gaming'") != std::string::npos) {
        shouldGrab = isGamingModeActive ? !isGamingModeActive() : true;
      } else {
        shouldGrab = EvaluateCondition(condStr);
      }
    }

    // Update grab state
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
      // Resume
      enabled = true;
      
      // Restore hotkeys to their original state
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
        // No saved state, reevaluate
        ReevaluateConditionalHotkeys();
      }

      wasSuspended = false;
      return true;
    } else {
      // Suspend
      enabled = false;
      
      // Save current state
      {
        std::lock_guard<std::mutex> lock(hotkeyMutex);
        suspendedHotkeyStates.clear();
        for (auto& ch : conditionalHotkeys) {
          if (!ch.monitoringEnabled) continue;
          
          ConditionalHotkeyState state;
          state.id = ch.id;
          state.wasGrabbed = ch.currentlyGrabbed;
          suspendedHotkeyStates.push_back(state);

          // Ungrab during suspension
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
    info("Conditional hotkeys are disabled");
    return;
  }

  if (verboseLogging) {
    if (std::holds_alternative<std::function<bool()>>(hotkey.condition)) {
      debug("Updating conditional hotkey - Key: '{}', Function Condition, "
            "CurrentlyGrabbed: {}",
            hotkey.key, hotkey.currentlyGrabbed);
    } else {
      debug("Updating conditional hotkey - Key: '{}', Condition: '{}', "
            "CurrentlyGrabbed: {}",
            hotkey.key, std::get<std::string>(hotkey.condition), hotkey.currentlyGrabbed);
    }
  }

  bool conditionMet;
  auto now = std::chrono::steady_clock::now();

  // Check condition type
  if (std::holds_alternative<std::function<bool()>>(hotkey.condition)) {
    // Function condition - no caching
    const auto& func = std::get<std::function<bool()>>(hotkey.condition);
    if (func) {
      conditionMet = func();
    } else {
      conditionMet = false;
    }
  } else {
    // String condition - use caching
    const auto& condStr = std::get<std::string>(hotkey.condition);
    
    // Check cache first
    {
      std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
      auto cacheIt = conditionCache.find(condStr);
      if (cacheIt != conditionCache.end()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - cacheIt->second.timestamp)
                       .count();
        if (age < CACHE_DURATION_MS) {
          conditionMet = cacheIt->second.result;

          if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
            info("Condition from cache: {} for {} ({}) - was:{} now:{}",
                 conditionMet ? 1 : 0, condStr, hotkey.key,
                 hotkey.lastConditionResult, conditionMet);
          }

          UpdateHotkeyState(hotkey, conditionMet);
          return;
        }
      }
    }

    // Evaluate condition
    conditionMet = EvaluateCondition(condStr);

    // Update cache
    {
      std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
      conditionCache[condStr] = {conditionMet, now};
    }
  }

  // Log changes
  if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
    if (std::holds_alternative<std::function<bool()>>(hotkey.condition)) {
      info("Function condition changed: {} for {} - was:{} now:{}",
           conditionMet ? 1 : 0, hotkey.key, hotkey.lastConditionResult,
           conditionMet);
    } else {
      info("Condition changed: {} for {} ({}) - was:{} now:{}",
           conditionMet ? 1 : 0, std::get<std::string>(hotkey.condition), hotkey.key,
           hotkey.lastConditionResult, conditionMet);
    }
  }

  UpdateHotkeyState(hotkey, conditionMet);
}

void ConditionalHotkeyManager::UpdateHotkeyState(ConditionalHotkey& hotkey,
                                                  bool conditionMet) {
  if (conditionMet && !hotkey.currentlyGrabbed) {
    io->GrabHotkey(hotkey.id);
    hotkey.currentlyGrabbed = true;
    if (verboseLogging) {
      if (std::holds_alternative<std::string>(hotkey.condition)) {
        debug("Grabbed conditional hotkey: {} ({})", hotkey.key, 
              std::get<std::string>(hotkey.condition));
      } else {
        debug("Grabbed conditional hotkey: {} (function condition)", hotkey.key);
      }
    }
  } else if (!conditionMet && hotkey.currentlyGrabbed) {
    io->UngrabHotkey(hotkey.id);
    hotkey.currentlyGrabbed = false;
    if (verboseLogging) {
      if (std::holds_alternative<std::string>(hotkey.condition)) {
        debug("Ungrabbed conditional hotkey: {} ({})", hotkey.key,
              std::get<std::string>(hotkey.condition));
      } else {
        debug("Ungrabbed conditional hotkey: {} (function condition)", hotkey.key);
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

  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);

    // Process all hotkeys - evaluate conditions and batch grab/ungrab
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
  }

  // Apply grab/ungrab operations outside of lock
  for (int id : toGrab) {
    io->GrabHotkey(id);
  }
  for (int id : toUngrab) {
    io->UngrabHotkey(id);
  }
}

void ConditionalHotkeyManager::InvalidateConditionalHotkeys() {
  std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
  conditionCache.clear();
}

void ConditionalHotkeyManager::Cleanup() {
  inCleanupMode = true;

  // Ungrab all hotkeys
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
  {
    std::lock_guard<std::mutex> lock(modeMutex);
    if (currentMode == newMode) {
      return; // No change
    }
    currentMode = newMode;
  }

  debug("Mode changed to: {}", newMode);
  // Schedule reevaluation through EventQueue for thread-safe execution
  ScheduleReevaluation();
}

std::string ConditionalHotkeyManager::GetMode() const {
  std::lock_guard<std::mutex> lock(modeMutex);
  return currentMode;
}

bool ConditionalHotkeyManager::EvaluateCondition(const std::string& condition) {
  // If we have an interpreter, we could parse and evaluate the condition properly
  // For now, the mini-parser handles legacy string conditions
  // Future enhancement: parse condition string to AST and use interpreter
  return EvaluateConditionInternal(condition);
}

bool ConditionalHotkeyManager::EvaluateConditionInternal(const std::string& condition) {
  // Check for mode conditions
  std::string currentModeVal = GetMode();

  if (condition.find("mode == 'gaming'") != std::string::npos) {
    return (currentModeVal == "gaming");
  } else if (condition.find("mode != 'gaming'") != std::string::npos) {
    return (currentModeVal != "gaming");
  } else if (condition.find("mode == '") != std::string::npos) {
    // Extract mode value
    size_t start = condition.find("mode == '") + 9;
    size_t end = condition.find("'", start);
    if (end != std::string::npos) {
      std::string modeVal = condition.substr(start, end - start);
      return (currentModeVal == modeVal);
    }
  }

  // Get window info - prefer WindowMonitor for cached data, fallback to IO
  std::string currentTitle;
  std::string currentClass;
  std::string currentExe;

  if (windowMonitor) {
    // Use cached window info from WindowMonitor (much faster)
    auto windowInfo = windowMonitor->GetActiveWindowInfo();
    if (windowInfo.has_value()) {
      currentTitle = windowInfo->title;
      currentClass = windowInfo->windowClass;
      currentExe = windowInfo->processName;
    }
  }
  
  // Fallback to IO if WindowMonitor not available or no cached data
  if (currentTitle.empty()) {
    currentTitle = io->GetActiveWindowTitle();
    currentClass = io->GetActiveWindowClass();
  }

  // Syntax: window.title == '' or window.title==''
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

  // Syntax: title == 'Chatterino' or title=='Chatterino'
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

  // Check for window class conditions
  // Syntax: window.class == 'Firefox' or window.class=='Firefox'
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

  // Syntax: class == 'Firefox' or class=='Firefox'
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

  // Syntax: exe == 'firefox' or exe=='steam'
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

  // Default: return false if no evaluator and condition is not recognized
  return false;
}

} // namespace havel
