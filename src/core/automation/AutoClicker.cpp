#include "AutoClicker.hpp"
#include <stdexcept>
#include <utility> // for std::move

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
            setPressAction([this]() { io_->Click(MouseButton::Left, MouseAction::Press); });
            setReleaseAction([this]() { io_->Click(MouseButton::Left, MouseAction::Release); });
            break;
        case ClickType::Right:
            setPressAction([this]() { io_->Click(MouseButton::Right, MouseAction::Press); });
            setReleaseAction([this]() { io_->Click(MouseButton::Right, MouseAction::Release); });
            break;
        case ClickType::Middle:
            setPressAction([this]() { io_->Click(MouseButton::Middle, MouseAction::Press); });
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

} // namespace havel::automation
