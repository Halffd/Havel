#pragma once
#include <string>
#include <vector>
#include <functional>

namespace havel {

class WindowRules {
public:
    WindowRules();
    ~WindowRules();
    
    void AddRule(const std::string& windowPattern, std::function<void(wID)> action);
    void ProcessWindow(wID window);
    
private:
    struct Rule {
        std::string pattern;
        std::function<void(wID)> action;
    };
    
    std::vector<Rule> rules;
};

} // namespace H 