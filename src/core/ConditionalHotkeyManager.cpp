#include "ConditionalHotkeyManager.hpp"
#include "IO.hpp"
#include "io/EventListener.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>

namespace havel {

// Static member initialization
std::mutex ConditionalHotkeyManager::modeMutex;
std::string ConditionalHotkeyManager::currentMode = "default";

ConditionalHotkeyManager::ConditionalHotkeyManager(IO& io)
    : io(io) {
  info("Initializing ConditionalHotkeyManager");
  
  // Start update loop thread
  updateLoopRunning = true;
  updateLoopThread = std::thread(&ConditionalHotkeyManager::UpdateLoop, this);
}

ConditionalHotkeyManager::~ConditionalHotkeyManager() {
  Cleanup();
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

  auto action = [this, condition, trueAction, falseAction]() {
    if (EvaluateCondition(condition)) {
      if (trueAction) trueAction();
    } else {
      if (falseAction) falseAction();
    }
  };

  ConditionalHotkey ch;
  ch.id = id;
  ch.key = key;
  ch.condition = condition;
  ch.conditionFunc = nullptr;
  ch.trueAction = trueAction;
  ch.falseAction = falseAction;
  ch.currentlyGrabbed = true;
  ch.usesFunctionCondition = false;
  ch.monitoringEnabled = true;

  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    conditionalHotkeys.push_back(ch);
    conditionalHotkeyIds.push_back(id);
  }

  // Register with IO
  io.Hotkey(key, action, condition, id);

  // Initial evaluation
  UpdateConditionalHotkey(conditionalHotkeys.back());

  return id;
}

int ConditionalHotkeyManager::AddConditionalHotkey(
    const std::string& key, std::function<bool()> condition,
    std::function<void()> trueAction,
    std::function<void()> falseAction, int id) {
  debug("Registering conditional hotkey - Key: '{}', Lambda Condition, ID: {}",
        key, id);
  
  if (id == 0) {
    static int nextId = 1000;
    id = nextId++;
  }

  auto action = [condition, trueAction, falseAction]() {
    if (condition()) {
      if (trueAction) trueAction();
    } else {
      if (falseAction) falseAction();
    }
  };

  ConditionalHotkey ch;
  ch.id = id;
  ch.key = key;
  ch.condition = "";
  ch.conditionFunc = condition;
  ch.trueAction = trueAction;
  ch.falseAction = falseAction;
  ch.currentlyGrabbed = true;
  ch.usesFunctionCondition = true;
  ch.monitoringEnabled = true;

  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    conditionalHotkeys.push_back(ch);
    conditionalHotkeyIds.push_back(id);
  }

  // Register with IO
  io.Hotkey(key, action, "", id);

  // Initial evaluation
  UpdateConditionalHotkey(conditionalHotkeys.back());

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
    io.UngrabHotkey(id);
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
    io.UngrabHotkey(id);
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
  {
    std::lock_guard<std::mutex> lock(deferredUpdateMutex);
    deferredUpdateQueue.push(-1); // Signal to update all
  }
  updateLoopCv.notify_one();
}

void ConditionalHotkeyManager::ForceUpdateAllConditionalHotkeys() {
  BatchUpdateConditionalHotkeys();
}

void ConditionalHotkeyManager::ReevaluateConditionalHotkeys() {
  std::lock_guard<std::mutex> lock(hotkeyMutex);
  
  for (auto& ch : conditionalHotkeys) {
    if (!ch.monitoringEnabled) continue;
    
    bool shouldGrab = false;
    
    if (ch.usesFunctionCondition) {
      if (ch.conditionFunc) {
        shouldGrab = ch.conditionFunc();
      }
    } else {
      // For mode-based conditions
      if (ch.condition.find("mode == 'gaming'") != std::string::npos) {
        shouldGrab = isGamingModeActive ? isGamingModeActive() : false;
      } else if (ch.condition.find("mode != 'gaming'") != std::string::npos) {
        shouldGrab = isGamingModeActive ? !isGamingModeActive() : true;
      } else {
        shouldGrab = EvaluateCondition(ch.condition);
      }
    }

    // Update grab state
    if (shouldGrab && !ch.currentlyGrabbed) {
      io.GrabHotkey(ch.id);
      ch.currentlyGrabbed = true;
      ch.lastConditionResult = true;
    } else if (!shouldGrab && ch.currentlyGrabbed) {
      io.UngrabHotkey(ch.id);
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
              io.GrabHotkey(state.id);
              it->currentlyGrabbed = true;
            } else if (!state.wasGrabbed && it->currentlyGrabbed) {
              io.UngrabHotkey(state.id);
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
            io.UngrabHotkey(ch.id);
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
    if (hotkey.usesFunctionCondition) {
      debug("Updating conditional hotkey - Key: '{}', Function Condition, "
            "CurrentlyGrabbed: {}",
            hotkey.key, hotkey.currentlyGrabbed);
    } else {
      debug("Updating conditional hotkey - Key: '{}', Condition: '{}', "
            "CurrentlyGrabbed: {}",
            hotkey.key, hotkey.condition, hotkey.currentlyGrabbed);
    }
  }

  bool conditionMet;
  auto now = std::chrono::steady_clock::now();

  if (hotkey.usesFunctionCondition) {
    if (hotkey.conditionFunc) {
      conditionMet = hotkey.conditionFunc();
    } else {
      conditionMet = false;
    }
  } else {
    // Check cache first
    {
      std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
      auto cacheIt = conditionCache.find(hotkey.condition);
      if (cacheIt != conditionCache.end()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - cacheIt->second.timestamp)
                       .count();
        if (age < CACHE_DURATION_MS) {
          conditionMet = cacheIt->second.result;

          if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
            info("Condition from cache: {} for {} ({}) - was:{} now:{}",
                 conditionMet ? 1 : 0, hotkey.condition, hotkey.key,
                 hotkey.lastConditionResult, conditionMet);
          }

          UpdateHotkeyState(hotkey, conditionMet);
          return;
        }
      }
    }

    // Evaluate condition
    conditionMet = EvaluateCondition(hotkey.condition);

    // Update cache
    {
      std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
      conditionCache[hotkey.condition] = {conditionMet, now};
    }
  }

  // Log changes
  if (conditionMet != hotkey.lastConditionResult && verboseConditionLogging) {
    if (hotkey.usesFunctionCondition) {
      info("Function condition changed: {} for {} - was:{} now:{}",
           conditionMet ? 1 : 0, hotkey.key, hotkey.lastConditionResult,
           conditionMet);
    } else {
      info("Condition changed: {} for {} ({}) - was:{} now:{}",
           conditionMet ? 1 : 0, hotkey.condition, hotkey.key,
           hotkey.lastConditionResult, conditionMet);
    }
  }

  UpdateHotkeyState(hotkey, conditionMet);
}

void ConditionalHotkeyManager::UpdateHotkeyState(ConditionalHotkey& hotkey,
                                                  bool conditionMet) {
  if (conditionMet && !hotkey.currentlyGrabbed) {
    io.GrabHotkey(hotkey.id);
    hotkey.currentlyGrabbed = true;
    if (verboseLogging) {
      debug("Grabbed conditional hotkey: {} ({})", hotkey.key, hotkey.condition);
    }
  } else if (!conditionMet && hotkey.currentlyGrabbed) {
    io.UngrabHotkey(hotkey.id);
    hotkey.currentlyGrabbed = false;
    if (verboseLogging) {
      debug("Ungrabbed conditional hotkey: {} ({})", hotkey.key, hotkey.condition);
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
  std::vector<ConditionalHotkey*> updatedHotkeys;

  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);

    // Process deferred updates
    std::queue<int> localQueue;
    {
      std::lock_guard<std::mutex> queueLock(deferredUpdateMutex);
      std::swap(localQueue, deferredUpdateQueue);
    }

    while (!localQueue.empty()) {
      int id = localQueue.front();
      localQueue.pop();

      if (id == -1) {
        // Update all
        for (auto& ch : conditionalHotkeys) {
          if (ch.monitoringEnabled) {
            updatedHotkeys.push_back(&ch);
          }
        }
        break;
      }

      for (auto& ch : conditionalHotkeys) {
        if (ch.id == id && ch.monitoringEnabled) {
          updatedHotkeys.push_back(&ch);
          break;
        }
      }
    }

    // Process all hotkeys
    for (auto& ch : conditionalHotkeys) {
      if (!ch.monitoringEnabled) continue;
      
      bool needsUpdate = false;
      for (auto* hotkey : updatedHotkeys) {
        if (hotkey->id == ch.id) {
          needsUpdate = true;
          break;
        }
      }

      // Always update hotkeys with mode in condition
      if (!needsUpdate && ch.condition.find("mode") != std::string::npos) {
        needsUpdate = true;
        updatedHotkeys.push_back(&ch);
      }

      if (needsUpdate) {
        bool shouldGrab = EvaluateCondition(ch.condition);

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
  }
  // Apply grab/ungrab operations outside of lock
  for (int id : toGrab) {
    io.GrabHotkey(id);
  }
  for (int id : toUngrab) {
    io.UngrabHotkey(id);
  }
}

void ConditionalHotkeyManager::InvalidateConditionalHotkeys() {
  std::lock_guard<std::mutex> cacheLock(conditionCacheMutex);
  conditionCache.clear();
}

void ConditionalHotkeyManager::UpdateLoop() {
  info("ConditionalHotkeyManager: Starting update loop");

  while (updateLoopRunning.load()) {
    {
      std::unique_lock<std::mutex> lock(updateLoopMutex);
      updateLoopCv.wait_for(lock, std::chrono::milliseconds(50));
    }

    if (!updateLoopRunning.load()) break;

    BatchUpdateConditionalHotkeys();
  }

  info("ConditionalHotkeyManager: Update loop stopped");
}

void ConditionalHotkeyManager::Cleanup() {
  inCleanupMode = true;
  updateLoopRunning = false;
  updateLoopCv.notify_one();

  if (updateLoopThread.joinable()) {
    updateLoopThread.join();
  }

  // Ungrab all hotkeys
  {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    for (auto& ch : conditionalHotkeys) {
      if (ch.currentlyGrabbed) {
        io.UngrabHotkey(ch.id);
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
  BatchUpdateConditionalHotkeys();
}

std::string ConditionalHotkeyManager::GetMode() const {
  std::lock_guard<std::mutex> lock(modeMutex);
  return currentMode;
}

bool ConditionalHotkeyManager::EvaluateCondition(const std::string& condition) {
  if (conditionEvaluator) {
    return conditionEvaluator(condition);
  }
  
  // Default evaluation for common patterns
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
  
  // Default: return false if no evaluator and condition is not recognized
  return false;
}

} // namespace havel
