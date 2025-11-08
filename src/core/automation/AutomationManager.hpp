#pragma once

#include "Task.hpp"
#include "AutoKeyPresser.hpp"
#include "ChainedTask.hpp"
#include "../IO.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>

namespace havel::automation {

class AutomationManager {
public:
    explicit AutomationManager(std::shared_ptr<IO> io);
    ~AutomationManager() = default;

    // Disable copying
    AutomationManager(const AutomationManager&) = delete;
    AutomationManager& operator=(const AutomationManager&) = delete;

    // Task management
    TaskPtr createAutoClicker();
    TaskPtr createAutoClicker(const std::string& button, int intervalMs = 100);
    
    TaskPtr createAutoRunner();
    TaskPtr createAutoRunner(const std::string& direction, int intervalMs = 50);
    
    TaskPtr createAutoKeyPresser();
    TaskPtr createAutoKeyPresser(const std::string& key, int intervalMs = 100);
    
    // Chained actions
    using Action = std::function<void()>;
    using TimedAction = std::pair<Action, std::chrono::milliseconds>;
    
    TaskPtr createChainedTask(
        const std::string& baseName,
        const std::vector<TimedAction>& actions,
        bool loop = false
    );
    
    // Task control
    TaskPtr getTask(const std::string& name) const;
    bool hasTask(const std::string& name) const;
    void removeTask(const std::string& name);
    void stopAll();
    
    // Helper to create timed actions
    template<typename F>
    static TimedAction makeTimedAction(F&& action, int delayMs) {
        return {std::forward<F>(action), std::chrono::milliseconds(delayMs)};
    }

private:
    std::shared_ptr<IO> io_;
    std::unordered_map<std::string, TaskPtr> tasks_;
    mutable std::mutex tasksMutex_;
    
    std::string generateUniqueName(const std::string& base) const;
};

} // namespace havel::automation
