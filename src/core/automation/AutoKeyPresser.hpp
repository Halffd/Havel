#pragma once

#include "AutoPresser.hpp"
#include "../IO.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace havel::automation {

class AutoKeyPresser : public AutoPresser {
public:
    using KeySequence = std::vector<std::pair<std::string, std::chrono::milliseconds>>;
    
    explicit AutoKeyPresser(std::shared_ptr<IO> io);
    ~AutoKeyPresser() override = default;

    void setKeySequence(const KeySequence& sequence);
    void setKey(const std::string& key);
    void setIntervalMs(int intervalMs) override;

private:
    void onStart() override;
    void onStop() override;
    void executeKeyPress();
    void setupKeyActions();

    std::shared_ptr<IO> io_;
    std::string currentKey_;
    KeySequence keySequence_;
    size_t currentKeyIndex_ = 0;
    bool useSequence_ = false;
};

} // namespace havel::automation
