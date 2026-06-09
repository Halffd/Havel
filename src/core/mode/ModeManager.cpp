#include "core/mode/ModeManager.hpp"
#include "utils/Logger.hpp"
#include <chrono>

namespace havel {

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

void ModeManager::setMode(const std::string &modeName) {
    ModeChangedCallback cb;
    std::string oldMode;
    std::string newMode;
    {
        std::lock_guard<std::mutex> lock(modeMutex);

        if (currentMode == modeName) {
            return;
        }

        oldMode = currentMode;

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

        previousMode = currentMode;
        currentMode = modeName;
        newMode = modeName;

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

        info("Mode changed to '{}'", currentMode);
        cb = onModeChanged_;
    }

    if (cb) {
        cb(newMode, oldMode);
    }
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

} // namespace havel
