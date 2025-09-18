#include "ChainedTask.hpp"
#include <iostream>

namespace havel::automation {

ChainedTask::ChainedTask(
    const std::string& name,
    const std::vector<TimedAction>& actions,
    bool loop,
    CompletionCallback onCompletion
) : name_(name),
    actions_(actions),
    loop_(loop),
    onCompletion_(std::move(onCompletion)) {
    if (actions_.empty()) {
        throw std::invalid_argument("Actions list cannot be empty");
    }
}

ChainedTask::~ChainedTask() {
    stop();
}

void ChainedTask::start() {
    if (running_) {
        return;
    }

    stopRequested_ = false;
    running_ = true;
    currentActionIndex_ = 0;
    
    workerThread_ = std::make_unique<std::thread>(&ChainedTask::workerThread, this);
}

void ChainedTask::stop() {
    if (!running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_all();

    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
        workerThread_.reset();
    }

    running_ = false;
    
    if (onCompletion_) {
        onCompletion_(name_);
    }
}

void ChainedTask::toggle() {
    if (isRunning()) {
        stop();
    } else {
        start();
    }
}

bool ChainedTask::isRunning() const {
    return running_;
}

std::string ChainedTask::getName() const {
    return name_;
}

void ChainedTask::workerThread() {
    while (!stopRequested_ && running_) {
        // Execute current action
        executeCurrentAction();
        
        // Move to next action or stop/loop
        if (stopRequested_) {
            break;
        }
        
        currentActionIndex_++;
        
        if (currentActionIndex_ >= actions_.size()) {
            if (loop_) {
                currentActionIndex_ = 0;
            } else {
                break;
            }
        }
        
        // Wait for the specified delay before next action
        auto delay = actions_[currentActionIndex_].second;
        if (delay > std::chrono::milliseconds(0)) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, delay, [this] { 
                return stopRequested_.load(); 
            });
        }
    }
    
    // Clean up
    running_ = false;
    if (onCompletion_ && !stopRequested_) {
        onCompletion_(name_);
    }
}

void ChainedTask::executeCurrentAction() {
    if (currentActionIndex_ >= actions_.size()) {
        return;
    }
    
    try {
        const auto& [action, _] = actions_[currentActionIndex_];
        if (action) {
            action();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error executing action " << currentActionIndex_ 
                  << " in chained task " << name_ << ": " << e.what() << std::endl;
    }
}

} // namespace havel::automation
