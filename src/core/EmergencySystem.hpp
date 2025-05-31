#pragma once
#include <functional>
#include <string>

namespace havel {

class EmergencySystem {
public:
    EmergencySystem();
    ~EmergencySystem();
    
    void RegisterEmergencyHotkey(const std::string& hotkey);
    void SetEmergencyAction(std::function<void()> action);
    void Trigger();
    
private:
    std::function<void()> emergencyAction;
    std::string emergencyHotkey;
};

} // namespace havel 