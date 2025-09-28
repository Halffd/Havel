#include "AutomationManager.hpp"
#include "AutoClicker.hpp"
#include "AutoRunner.hpp"
#include "AutoKeyPresser.hpp"
#include <stdexcept>
#include <memory>
#include <utility> // for std::move
#include <chrono>
#include <thread>

namespace havel::automation {

AutomationManager::AutomationManager(std::shared_ptr<havel::IO> io)
    : io_(std::move(io)) {
    if (!io_) {
        throw std::invalid_argument("IO cannot be null");
    }
}

TaskPtr AutomationManager::createAutoClicker() {
    return createAutoClicker("left", 100); // Default to left button with 100ms interval
}

TaskPtr AutomationManager::createAutoClicker(const std::string& button, int intervalMs) {
    auto name = generateUniqueName("AutoClicker_" + button);
    auto clicker = std::shared_ptr<AutoClicker>(new AutoClicker(io_));
    clicker->setButton(button);
    clicker->setIntervalMs(intervalMs);
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_[name] = clicker;
    return clicker;
}

TaskPtr AutomationManager::createAutoRunner() {
    return createAutoRunner("w", 50); // Default to 'w' key with 50ms interval
}

TaskPtr AutomationManager::createAutoRunner(const std::string& direction, int intervalMs) {
    auto name = generateUniqueName("AutoRunner_" + direction);
    auto runner = std::shared_ptr<AutoRunner>(new AutoRunner(io_));
    runner->setDirection(direction);
    runner->setIntervalMs(intervalMs);
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_[name] = runner;
    return runner;
}

TaskPtr AutomationManager::createAutoKeyPresser() {
    return createAutoKeyPresser("space", 100); // Default to space key with 100ms interval
}

TaskPtr AutomationManager::createAutoKeyPresser(const std::string& key, int intervalMs) {
    auto name = generateUniqueName("AutoKeyPresser_" + key);
    auto keyPresser = std::shared_ptr<AutoKeyPresser>(new AutoKeyPresser(io_));
    keyPresser->setKey(key);
    keyPresser->setIntervalMs(intervalMs);
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_[name] = keyPresser;
    return keyPresser;
}

TaskPtr AutomationManager::createChainedTask(
    const std::string& baseName,
    const std::vector<TimedAction>& actions,
    bool loop
) {
    if (actions.empty()) {
        throw std::invalid_argument("Actions list cannot be empty");
    }

    auto name = baseName;
    
    // Create a task that will manage the chained actions
    auto task = std::make_shared<ChainedTask>(
        name,
        actions,
        loop,
        [this](const std::string& taskName) {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            tasks_.erase(taskName);
        }
    );

    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasks_[name] = task;
    }

    // Start the task
    task->start();
    
    return task;
}

TaskPtr AutomationManager::getTask(const std::string& name) const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(name);
    if (it == tasks_.end()) {
        return nullptr;
    }
    return it->second;
}

bool AutomationManager::hasTask(const std::string& name) const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    return tasks_.find(name) != tasks_.end();
}

void AutomationManager::removeTask(const std::string& name) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(name);
    if (it != tasks_.end()) {
        // Stop the task before removing
        if (auto task = it->second) {
            task->stop();
        }
        tasks_.erase(it);
    }
}

void AutomationManager::stopAll() {
    std::unordered_map<std::string, TaskPtr> tasksCopy;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasksCopy = tasks_;
    }
    
    for (auto& [name, task] : tasksCopy) {
        task->stop();
    }
    
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_.clear();
}

std::string AutomationManager::generateUniqueName(const std::string& base) const {
    static std::atomic<int> counter{0};
    return base + "_" + std::to_string(counter++);
}

} // namespace havel::automation
