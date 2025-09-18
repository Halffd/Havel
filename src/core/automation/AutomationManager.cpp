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
    auto name = generateUniqueName("AutoClicker");
    auto clicker = std::shared_ptr<AutoClicker>(new AutoClicker(io_));
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_[name] = clicker;
    return clicker;
}

TaskPtr AutomationManager::createAutoRunner() {
    auto name = generateUniqueName("AutoRunner");
    auto runner = std::shared_ptr<AutoRunner>(new AutoRunner(io_));
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_[name] = runner;
    return runner;
}

TaskPtr AutomationManager::createAutoKeyPresser() {
    auto name = generateUniqueName("AutoKeyPresser");
    auto keyPresser = std::shared_ptr<AutoKeyPresser>(new AutoKeyPresser(io_));
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

    auto name = generateUniqueName(baseName + "_Chained");
    
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
