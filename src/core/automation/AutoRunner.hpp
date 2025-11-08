#pragma once

#include "AutoPresser.hpp"
#include "../IO.hpp"
#include <memory>
#include <string>
#include <utility> // for std::move

namespace havel::automation {

class AutoRunner : public AutoPresser {
public:
    explicit AutoRunner(std::shared_ptr<IO> io);
    ~AutoRunner() override = default;

    void setDirection(const std::string& direction);
    void setIntervalMs(int intervalMs);

private:
    void onStart() override;
    void onStop() override;
    void setupDirectionAction();

    std::shared_ptr<IO> io_;
    std::string direction_;
};

} // namespace havel::automation
