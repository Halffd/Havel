#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace havel {

class ModeManager {
public:
    ModeManager() = default;
    ~ModeManager();

    struct ModeDefinition {
        std::string name;
        std::function<void()> onEnter;
        std::function<void()> onExit;
        std::function<void(const std::string &fromMode)> onEnterFrom;
        std::function<void(const std::string &toMode)> onExitTo;
        int priority = 0;
        bool isActive = false;
        std::chrono::steady_clock::time_point enterTime;
        std::chrono::milliseconds totalTime{0};
        int transitionCount = 0;
    };

    void defineMode(ModeDefinition mode);

    std::string getCurrentMode() const;
    std::string getPreviousMode() const;

    void setMode(const std::string &modeName);

    const std::vector<ModeDefinition> &getModes() const { return modes; }

    std::chrono::milliseconds getModeTime(const std::string &modeName) const;
    int getModeTransitions(const std::string &modeName) const;

    using ModeChangedCallback = std::function<void(const std::string &newMode,
                                                   const std::string &oldMode)>;
    void setOnModeChanged(ModeChangedCallback cb) { onModeChanged_ = std::move(cb); }

private:
    std::vector<ModeDefinition> modes;
    std::string currentMode;
    std::string previousMode;
    mutable std::mutex modeMutex;
    ModeChangedCallback onModeChanged_;

    void triggerEnter(ModeDefinition &mode);
    void triggerExit(ModeDefinition &mode);
};

} // namespace havel
