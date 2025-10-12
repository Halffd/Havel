#include "AutoClicker.hpp"
#include <stdexcept>
#include <utility> // for std::move
#include <chrono>

namespace havel::automation {

AutoClicker::AutoClicker(std::shared_ptr<IO> io)
    : AutoPresser("AutoClicker", std::chrono::milliseconds(100))
    , io_(std::move(io))
    , clickType_(ClickType::Left) {
    if (!io_) {
        throw std::invalid_argument("IO cannot be null");
    }
    setupClickActions();
}

void AutoClicker::setClickType(ClickType type) {
    clickType_ = type;
    setupClickActions();
}

void AutoClicker::setClickFunction(std::function<void()> clickFunc) {
    customClickFunc_ = std::move(clickFunc);
    setupClickActions();
}

void AutoClicker::setIntervalMs(int intervalMs) {
    setInterval(std::chrono::milliseconds(intervalMs));
}

void AutoClicker::setButton(const std::string& button) {
    if (button == "left" || button == "Left" || button == "LEFT") {
        setClickType(ClickType::Left);
    } else if (button == "right" || button == "Right" || button == "RIGHT") {
        setClickType(ClickType::Right);
    } else if (button == "middle" || button == "Middle" || button == "MIDDLE") {
        setClickType(ClickType::Middle);
    } else {
        throw std::invalid_argument("Invalid button type. Must be 'left', 'right', or 'middle'");
    }
}

void AutoClicker::setupClickActions() {
    if (customClickFunc_) {
        setPressAction([this]() {
            customClickFunc_();
        });
        setReleaseAction(nullptr);
        return;
    }

    using havel::MouseButton;
    using havel::MouseAction;
    
    switch (clickType_) {
        case ClickType::Left:
            setPressAction([this]() { io_->Click(MouseButton::Left, MouseAction::Hold); });
            setReleaseAction([this]() { io_->Click(MouseButton::Left, MouseAction::Release); });
            break;
        case ClickType::Right:
            setPressAction([this]() { io_->Click(MouseButton::Right, MouseAction::Hold); });
            setReleaseAction([this]() { io_->Click(MouseButton::Right, MouseAction::Release); });
            break;
        case ClickType::Middle:
            setPressAction([this]() { io_->Click(MouseButton::Middle, MouseAction::Hold); });
            setReleaseAction([this]() { io_->Click(MouseButton::Middle, MouseAction::Release); });
            break;
    }
}

void AutoClicker::onStart() {
    // Additional setup when autoclicker starts
}

void AutoClicker::onStop() {
    // Cleanup when autoclicker stops
}

// Fast-mode: avoid AutoPresser's 10ms press/release cadence and just click once per interval
void AutoClicker::start() {
    if (isRunning()) return;
    fastRunning_.store(true);
    fastThread_ = std::make_unique<std::thread>(&AutoClicker::fastClickThread, this);
    onStart();
}

void AutoClicker::stop() {
    if (!isRunning()) return;
    fastRunning_.store(false);
    if (fastThread_ && fastThread_->joinable()) {
        fastThread_->join();
    }
    fastThread_.reset();
    onStop();
}

bool AutoClicker::isRunning() const {
    return fastRunning_.load() && fastThread_ && fastThread_->joinable();
}

void AutoClicker::fastClickThread() {
    using havel::MouseButton;
    using havel::MouseAction;

    auto toButton = [this]() {
        switch (clickType_) {
            case ClickType::Left: return MouseButton::Left;
            case ClickType::Right: return MouseButton::Right;
            case ClickType::Middle: return MouseButton::Middle;
        }
        return MouseButton::Left;
    };

    while (fastRunning_.load()) {
        // Single instantaneous click
        io_->Click(toButton(), MouseAction::Click);

        // Sleep for configured interval
        std::this_thread::sleep_for(getInterval());
    }
}

} // namespace havel::automation
