#include "AutoKeyPresser.hpp"
#include <stdexcept>
#include <utility>
#include <iostream>

namespace havel::automation {

AutoKeyPresser::AutoKeyPresser(std::shared_ptr<IO> io)
    : AutoPresser("AutoKeyPresser", std::chrono::milliseconds(100))
    , io_(std::move(io))
    , currentKey_("") {
    if (!io_) {
        throw std::invalid_argument("IO cannot be null");
    }
    setupKeyActions();
}

void AutoKeyPresser::setKeySequence(const KeySequence& sequence) {
    if (sequence.empty()) {
        throw std::invalid_argument("Key sequence cannot be empty");
    }
    keySequence_ = sequence;
    useSequence_ = true;
    currentKeyIndex_ = 0;
    setupKeyActions();
}

void AutoKeyPresser::setKey(const std::string& key) {
    if (key.empty()) {
        throw std::invalid_argument("Key cannot be empty");
    }
    currentKey_ = key;
    useSequence_ = false;
    setupKeyActions();
}

void AutoKeyPresser::setIntervalMs(int intervalMs) {
    AutoPresser::setIntervalMs(intervalMs);
}

void AutoKeyPresser::onStart() {
    currentKeyIndex_ = 0;
    setupKeyActions();
}

void AutoKeyPresser::onStop() {
    // Release any held keys when stopping
    if (!currentKey_.empty()) {
        try {
            io_->Send(io_->StringToVirtualKey(currentKey_), false);
        } catch (const std::exception& e) {
            std::cerr << "Error releasing key: " << e.what() << std::endl;
        }
    }
}

void AutoKeyPresser::executeKeyPress() {
    if (useSequence_ && !keySequence_.empty()) {
        try {
            // Execute current key in sequence
            const auto& [key, _] = keySequence_[currentKeyIndex_];
            
            // Press and release the key
            io_->Send(io_->StringToVirtualKey(key), true);
            io_->Send(io_->StringToVirtualKey(key), false);
            
            // Move to next key in sequence
            currentKeyIndex_ = (currentKeyIndex_ + 1) % keySequence_.size();
            
            // Update interval if needed
            const auto& [nextKey, nextInterval] = keySequence_[currentKeyIndex_];
            setIntervalMs(static_cast<int>(nextInterval.count()));
        } catch (const std::exception& e) {
            std::cerr << "Error in key sequence: " << e.what() << std::endl;
            stop();
        }
    } else if (!currentKey_.empty()) {
        try {
            // Simple key press
            io_->Send(io_->StringToVirtualKey(currentKey_), true);
            io_->Send(io_->StringToVirtualKey(currentKey_), false);
        } catch (const std::exception& e) {
            std::cerr << "Error sending key: " << e.what() << std::endl;
            stop();
        }
    }
}

void AutoKeyPresser::setupKeyActions() {
    if (useSequence_ && !keySequence_.empty()) {
        // Set up for sequence
        const auto& [key, interval] = keySequence_[0];
        setPressAction([this]() { executeKeyPress(); });
        setReleaseAction(nullptr);
        setIntervalMs(static_cast<int>(interval.count()));
    } else if (!currentKey_.empty()) {
        // Set up for single key
        setPressAction([this]() { executeKeyPress(); });
        setReleaseAction(nullptr);
    }
}

} // namespace havel::automation
