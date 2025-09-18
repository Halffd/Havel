#pragma once

#include "Task.hpp"
#include <functional>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace havel::automation {

class ChainedTask : public Task {
public:
    using Action = std::function<void()>;
    using TimedAction = std::pair<Action, std::chrono::milliseconds>;
    using CompletionCallback = std::function<void(const std::string&)>;

    ChainedTask(
        const std::string& name,
        const std::vector<TimedAction>& actions,
        bool loop,
        CompletionCallback onCompletion
    );
    
    ~ChainedTask() override;

    // Task interface
    void start() override;
    void stop() override;
    void toggle() override;
    bool isRunning() const override;
    std::string getName() const override;

private:
    void workerThread();
    void executeCurrentAction();
    void notifyActionCompleted();

    const std::string name_;
    const std::vector<TimedAction> actions_;
    const bool loop_;
    const CompletionCallback onCompletion_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<size_t> currentActionIndex_{0};
    
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unique_ptr<std::thread> workerThread_;
};

} // namespace havel::automation
