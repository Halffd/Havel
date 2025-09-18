#include "AutoRunner.hpp"
#include <stdexcept>
#include <utility> // for std::move

namespace havel::automation {

AutoRunner::AutoRunner(std::shared_ptr<IO> io)
    : AutoPresser("AutoRunner", std::chrono::milliseconds(100))
    , io_(std::move(io))
    , direction_("w") {
    if (!io_) {
        throw std::invalid_argument("IO cannot be null");
    }
    setupDirectionAction();
}

void AutoRunner::setDirection(const std::string& direction) {
    if (direction.empty()) {
        throw std::invalid_argument("Direction cannot be empty");
    }
    
    bool wasRunning = isRunning();
    if (wasRunning) {
        stop();
    }
    
    direction_ = direction;
    setupDirectionAction();
    
    if (wasRunning) {
        start();
    }
}

void AutoRunner::setIntervalMs(int intervalMs) {
    setInterval(std::chrono::milliseconds(intervalMs));
}

void AutoRunner::onStart() {
    // Press the direction key when starting
    io_->Send(direction_);
}

void AutoRunner::onStop() {
    // Release the direction key when stopping
    io_->Send(direction_ + " up");
}

void AutoRunner::setupDirectionAction() {
    setPressAction([this]() {
        // Press the direction key
        io_->Send(direction_);
    });
    
    setReleaseAction([this]() {
        // Release the direction key
        io_->Send(direction_ + " up");
    });
}

} // namespace havel::automation
