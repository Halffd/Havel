#include "ModeManager.hpp"
#include "havel-lang/ast/AST.h"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>

namespace havel {

ModeManager::~ModeManager() = default;

void ModeManager::defineSignal(Signal signal) {
  std::lock_guard<std::mutex> lock(modeMutex);
  signalList.push_back(std::move(signal));
}

void ModeManager::defineMode(ModeDefinition mode) {
  std::lock_guard<std::mutex> lock(modeMutex);
  modes.push_back(std::move(mode));
}

void ModeManager::defineGroup(ModeGroup group) {
  std::lock_guard<std::mutex> lock(modeMutex);
  groups.push_back(std::move(group));
}

std::string ModeManager::getCurrentMode() const {
  std::lock_guard<std::mutex> lock(modeMutex);
  return currentMode;
}

std::string ModeManager::getPreviousMode() const {
  std::lock_guard<std::mutex> lock(modeMutex);
  return previousMode;
}

void ModeManager::setMode(const std::string &modeName) {
  std::lock_guard<std::mutex> lock(modeMutex);

  if (currentMode == modeName) {
    return; // No change
  }

  std::string oldMode = currentMode;

  // Find and exit current mode
  for (auto &mode : modes) {
    if (mode.name == currentMode && mode.isActive) {
      mode.isActive = false;
      if (mode.onExit) {
        mode.onExit();
      }
      if (mode.onExitTo) {
        mode.onExitTo(modeName);
      }
    }
  }

  // Set previous mode
  previousMode = currentMode;
  currentMode = modeName;

  // Find and enter new mode
  for (auto &mode : modes) {
    if (mode.name == modeName) {
      mode.isActive = true;
      mode.enterTime = std::chrono::steady_clock::now();
      mode.transitionCount++;
      if (mode.onEnter) {
        mode.onEnter();
      }
      if (mode.onEnterFrom) {
        mode.onEnterFrom(oldMode);
      }
      break;
    }
  }

  // Trigger transition callbacks
  triggerTransition(oldMode, modeName);
}

std::chrono::milliseconds
ModeManager::getModeTime(const std::string &modeName) const {
  std::lock_guard<std::mutex> lock(modeMutex);
  for (const auto &mode : modes) {
    if (mode.name == modeName) {
      auto total = mode.totalTime;
      if (mode.isActive) {
        total += std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - mode.enterTime);
      }
      return total;
    }
  }
  return std::chrono::milliseconds(0);
}

int ModeManager::getModeTransitions(const std::string &modeName) const {
  std::lock_guard<std::mutex> lock(modeMutex);
  for (const auto &mode : modes) {
    if (mode.name == modeName) {
      return mode.transitionCount;
    }
  }
  return 0;
}

bool ModeManager::isSignalActive(const std::string &signalName) const {
  std::lock_guard<std::mutex> lock(modeMutex);
  for (const auto &signal : signalList) {
    if (signal.name == signalName) {
      return signal.value;
    }
  }
  return false;
}

void ModeManager::triggerEnter(ModeDefinition &mode) {
  mode.isActive = true;
  mode.enterTime = std::chrono::steady_clock::now();
  mode.transitionCount++;
  previousMode = currentMode;
  currentMode = mode.name;

  if (mode.onEnter) {
    mode.onEnter();
  }
  if (mode.onEnterFrom && !previousMode.empty()) {
    mode.onEnterFrom(previousMode);
  }
}

void ModeManager::triggerExit(ModeDefinition &mode) {
  // Update total time
  if (mode.isActive) {
    mode.totalTime += std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mode.enterTime);
  }

  mode.isActive = false;

  if (mode.onExit) {
    mode.onExit();
  }
  if (mode.onExitTo && !currentMode.empty()) {
    mode.onExitTo(currentMode);
  }
}

void ModeManager::triggerTransition(const std::string &fromMode,
                                    const std::string &toMode) {
  // Trigger group callbacks if applicable
  for (const auto &group : groups) {
    if (group.onEnter) {
      for (const auto &modeName : group.modes) {
        if (modeName == toMode) {
          group.onEnter();
          break;
        }
      }
    }
  }
}

void ModeManager::update(ExprEvaluator evaluator) {
  std::lock_guard<std::mutex> lock(modeMutex);

  // First, update all signals
  for (auto &signal : signalList) {
    if (signal.conditionExpr && evaluator) {
      signal.value = evaluator(*signal.conditionExpr);
    }
  }

  std::string newActiveMode = currentMode;
  bool modeChanged = false;

  debug("ModeManager::update() - checking {} modes, current={}", modes.size(),
        currentMode);

  // Sort modes by priority (higher priority first)
  std::vector<ModeDefinition *> sortedModes;
  for (auto &mode : modes) {
    sortedModes.push_back(&mode);
  }
  std::sort(sortedModes.begin(), sortedModes.end(),
            [](const ModeDefinition *a, const ModeDefinition *b) {
              return a->priority > b->priority;
            });

  // Check all mode conditions in priority order
  for (auto *modePtr : sortedModes) {
    auto &mode = *modePtr;
    bool shouldActivate = false;
    bool hasCondition = false;

    // Try callback-based condition first
    if (mode.conditionCallback) {
      shouldActivate = mode.conditionCallback();
      hasCondition = true;
    }
    // Fall back to AST-based condition if callback not available
    else if (mode.conditionExpr && evaluator) {
      shouldActivate = evaluator(*mode.conditionExpr);
      hasCondition = true;
    }

    if (!hasCondition)
      continue;

    debug("  Mode '{}' (priority {}) condition = {}", mode.name, mode.priority,
          shouldActivate ? "true" : "false");

    if (shouldActivate && !mode.isActive) {
      // Mode condition met, activate it
      // First exit current mode if different
      if (currentMode != mode.name) {
        for (auto &activeMode : modes) {
          if (activeMode.isActive && activeMode.name != mode.name) {
            debug("  Exiting mode '{}'", activeMode.name);
            triggerExit(activeMode);
          }
        }
      }
      debug("  Entering mode '{}'", mode.name);
      triggerEnter(mode);
      modeChanged = true;
      newActiveMode = mode.name;
    } else if (!shouldActivate && mode.isActive && mode.name == currentMode) {
      // Current mode condition no longer met
      debug("  Exiting mode '{}' (condition no longer met)", mode.name);
      triggerExit(mode);
      newActiveMode = "default";
      modeChanged = true;
    }
  }

  // If no mode is active, switch to default
  if (newActiveMode == "default" && currentMode != "default") {
    for (auto &mode : modes) {
      if (mode.isActive) {
        debug("  Exiting mode '{}' (no mode active)", mode.name);
        triggerExit(mode);
      }
    }
    previousMode = currentMode;
    currentMode = "default";
    modeChanged = true;
  }

  if (modeChanged) {
    info("Mode changed to '{}'", currentMode);
  }
}

} // namespace havel
