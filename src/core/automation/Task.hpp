#pragma once

#include <memory>
#include <string>
#include <functional>

namespace havel::automation {

class Task {
public:
    virtual ~Task() = default;
    
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void toggle() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual std::string getName() const = 0;
};

using TaskPtr = std::shared_ptr<Task>;

} // namespace havel::automation
