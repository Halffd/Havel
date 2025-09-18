#pragma once

#include "Task.hpp"
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>

namespace havel::automation {

class AutoPresser : public Task {
public:
    using Action = std::function<void()>;
    using Duration = std::chrono::milliseconds;

    AutoPresser(std::string name, Duration interval);
    virtual ~AutoPresser();

    // Disable copying
    AutoPresser(const AutoPresser&) = delete;
    AutoPresser& operator=(const AutoPresser&) = delete;

    // Task interface
    void start() override;
    void stop() override;
    void toggle() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] std::string getName() const override;

    // Configuration
    void setPressAction(Action action);
    void setReleaseAction(Action action);
    void setIntervalMs(int intervalMs) { interval_ = std::chrono::milliseconds(intervalMs); }
    int getIntervalMs() const { return static_cast<int>(interval_.count()); }
    std::chrono::milliseconds getInterval() const { return interval_; }
    void setInterval(std::chrono::milliseconds interval) { interval_ = interval; }

protected:
    virtual void onStart() {}
    virtual void onStop() {}
    virtual void onPress();
    virtual void onRelease();

private:
    void workerThread();

    const std::string name_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> workerThread_;
    
    Action pressAction_;
    Action releaseAction_;
    std::mutex actionMutex_;
};

} // namespace havel::automation
