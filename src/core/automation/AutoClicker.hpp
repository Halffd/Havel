#pragma once

#include "AutoPresser.hpp"
#include "../IO.hpp"
#include <memory>
#include <functional>

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

private:
    void setupClickActions();
    void onStart() override;
    void onStop() override;

    std::shared_ptr<havel::IO> io_;
    ClickType clickType_;
    std::function<void()> customClickFunc_;
};

} // namespace havel::automation
