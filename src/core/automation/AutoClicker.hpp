#pragma once

#include "AutoPresser.hpp"
#include "../IO.hpp"
#include <memory>
#include <functional>
#include <atomic>
#include <thread>

namespace havel::automation {

class AutoClicker : public AutoPresser {
public:
    enum class ClickType {
        Left,
        Right,
        Middle
    };

    explicit AutoClicker(std::shared_ptr<IO> io);
    ~AutoClicker() override = default;

    void setClickType(ClickType type);
    void setButton(const std::string& button);
    void setClickFunction(std::function<void()> clickFunc);
    void setIntervalMs(int intervalMs);

    // Fast-mode: override to avoid press+release cycle and 10ms delay
    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;

private:
    void setupClickActions();
    void onStart() override;
    void onStop() override;
    void fastClickThread();

    std::shared_ptr<IO> io_;
    ClickType clickType_;
    std::function<void()> customClickFunc_;

    // Fast-mode worker
    std::atomic<bool> fastRunning_{false};
    std::unique_ptr<std::thread> fastThread_;
};

} // namespace havel::automation
