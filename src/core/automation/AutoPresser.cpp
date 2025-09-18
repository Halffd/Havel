#include "AutoPresser.hpp"
#include <thread>
#include <chrono>
#include <stdexcept>

namespace havel::automation {

AutoPresser::AutoPresser(std::string name, Duration interval)
    : name_(std::move(name))
    , interval_(interval) {}

AutoPresser::~AutoPresser() {
    stop();
}

void AutoPresser::start() {
    if (isRunning()) return;
    
    running_ = true;
    workerThread_ = std::make_unique<std::thread>(&AutoPresser::workerThread, this);
    onStart();
}

void AutoPresser::stop() {
    if (!isRunning()) return;
    
    running_ = false;
    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }
    workerThread_.reset();
    
    // Ensure we release any pressed keys/buttons
    onRelease();
    onStop();
}

void AutoPresser::toggle() {
    if (isRunning()) {
        stop();
    } else {
        start();
    }
}

bool AutoPresser::isRunning() const {
    return running_ && workerThread_ && workerThread_->joinable();
}

std::string AutoPresser::getName() const {
    return name_;
}

void AutoPresser::setPressAction(Action action) {
    std::lock_guard<std::mutex> lock(actionMutex_);
    pressAction_ = std::move(action);
}

void AutoPresser::setReleaseAction(Action action) {
    std::lock_guard<std::mutex> lock(actionMutex_);
    releaseAction_ = std::move(action);
}

void AutoPresser::setInterval(Duration interval) {
    interval_ = interval;
}

void AutoPresser::onStart() {}
void AutoPresser::onStop() {}
void AutoPresser::onPress() {
    std::lock_guard<std::mutex> lock(actionMutex_);
    if (pressAction_) pressAction_();
}
void AutoPresser::onRelease() {
    std::lock_guard<std::mutex> lock(actionMutex_);
    if (releaseAction_) releaseAction_();
}

void AutoPresser::workerThread() {
    while (running_) {
        onPress();
        
        // Small delay to ensure the press is registered
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        onRelease();
        
        // Wait for the remaining interval
        if (interval_.count() > 10) {
            std::this_thread::sleep_for(interval_ - std::chrono::milliseconds(10));
        }
    }
    
    // Ensure we release any pressed keys/buttons when stopping
    onRelease();
}

} // namespace havel::automation
