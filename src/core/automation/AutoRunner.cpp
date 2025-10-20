#include "AutoRunner.hpp"
#include <stdexcept>
#include <utility>
#include "utils/Logger.hpp"

namespace havel::automation {

AutoRunner::AutoRunner(std::shared_ptr<IO> io)
    : AutoPresser("AutoRunner", std::chrono::milliseconds(100))
    , io_(std::move(io))
    , direction_("w") {
    if (!io_) {
        throw std::invalid_argument("IO cannot be null");
    }
    // Don't setup actions - we'll handle everything in onStart/onStop
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
    
    if (wasRunning) {
        start();
    }
}

void AutoRunner::setIntervalMs(int intervalMs) {
    setInterval(std::chrono::milliseconds(intervalMs));
}

void AutoRunner::onStart() {
    // JUST hold the keys down - no spamming needed!
    io_->Send("{" + direction_ + ":down}");
    io_->Send("{LShift:down}");
    info("AutoRunner started - holding {} and Shift", direction_);
}

void AutoRunner::onStop() {
    // Release both keys
    io_->Send("{" + direction_ + ":up}");
    io_->Send("{LShift:up}");
    info("AutoRunner stopped - released {} and Shift", direction_);
}

void AutoRunner::setupDirectionAction() {
    // DON'T SET PRESS/RELEASE ACTIONS!
    // AutoPresser will call onPress/onRelease which we don't want for holding keys
    // We only want onStart/onStop for hold-style automation
    
    // Clear any actions to prevent AutoPresser from doing press/release cycles
    setPressAction(nullptr);
    setReleaseAction(nullptr);
}

} // namespace havel::automation