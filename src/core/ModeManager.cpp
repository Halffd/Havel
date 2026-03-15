#include "ModeManager.hpp"
#include "DynamicConditionEvaluator.hpp"
#include "core/IO.hpp"
#include <chrono>
#include <algorithm>

namespace havel {

ModeManager::ModeManager(std::shared_ptr<IO> io)
    : io(io), 
      evaluator(std::make_unique<DynamicConditionEvaluator>(io)),
      currentMode("default"), 
      previousMode("default") {
    // Set up mode getters for condition evaluator
    evaluator->setModeGetter([this]() { return getCurrentMode(); });
    evaluator->setPreviousModeGetter([this]() { return getPreviousMode(); });
}

ModeManager::~ModeManager() = default;

void ModeManager::defineMode(ModeDefinition mode) {
    std::lock_guard<std::mutex> lock(modeMutex);
    modes.push_back(std::move(mode));
}

std::string ModeManager::getCurrentMode() const {
    std::lock_guard<std::mutex> lock(modeMutex);
    return currentMode;
}

std::string ModeManager::getPreviousMode() const {
    std::lock_guard<std::mutex> lock(modeMutex);
    return previousMode;
}

void ModeManager::setMode(const std::string& modeName) {
    std::lock_guard<std::mutex> lock(modeMutex);
    
    if (currentMode == modeName) {
        return;  // No change
    }
    
    // Find and exit current mode
    for (auto& mode : modes) {
        if (mode.name == currentMode && mode.isActive) {
            mode.isActive = false;
            if (mode.onExit) {
                mode.onExit();
            }
        }
    }
    
    // Set previous mode
    previousMode = currentMode;
    currentMode = modeName;
    
    // Find and enter new mode
    for (auto& mode : modes) {
        if (mode.name == modeName) {
            mode.isActive = true;
            if (mode.onEnter) {
                mode.onEnter();
            }
            break;
        }
    }
}

void ModeManager::triggerEnter(ModeDefinition& mode) {
    mode.isActive = true;
    previousMode = currentMode;
    currentMode = mode.name;
    
    if (mode.onEnter) {
        mode.onEnter();
    }
}

void ModeManager::triggerExit(ModeDefinition& mode) {
    mode.isActive = false;
    
    if (mode.onExit) {
        mode.onExit();
    }
}

bool ModeManager::evaluateCondition(const std::string& condition) {
    if (!evaluator) return false;
    return evaluator->evaluate(condition);
}

void ModeManager::update() {
    std::lock_guard<std::mutex> lock(modeMutex);
    
    std::string newActiveMode = currentMode;
    bool modeChanged = false;
    
    // Check all mode conditions
    for (auto& mode : modes) {
        if (mode.condition) {
            bool shouldActivate = mode.condition();
            
            if (shouldActivate && !mode.isActive) {
                // Mode condition met, activate it
                // First exit current mode if different
                if (currentMode != mode.name) {
                    for (auto& activeMode : modes) {
                        if (activeMode.isActive && activeMode.name != mode.name) {
                            triggerExit(activeMode);
                        }
                    }
                }
                triggerEnter(mode);
                modeChanged = true;
                newActiveMode = mode.name;
            } else if (!shouldActivate && mode.isActive && mode.name == currentMode) {
                // Current mode condition no longer met
                triggerExit(mode);
                newActiveMode = "default";
                modeChanged = true;
            }
        }
    }
    
    // If no mode is active, switch to default
    if (newActiveMode == "default" && currentMode != "default") {
        for (auto& mode : modes) {
            if (mode.isActive) {
                triggerExit(mode);
            }
        }
        previousMode = currentMode;
        currentMode = "default";
        modeChanged = true;
    }
}

} // namespace havel
