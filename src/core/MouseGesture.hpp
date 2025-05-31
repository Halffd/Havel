#pragma once
#include <string>
#include <vector>
#include <functional>

namespace havel {

class MouseGesture {
public:
    MouseGesture();
    ~MouseGesture();
    
    void AddGesture(const std::string& pattern, std::function<void()> action);
    void StartTracking();
    void StopTracking();
    
private:
    struct Gesture {
        std::string pattern;
        std::function<void()> action;
    };
    
    std::vector<Gesture> gestures;
    bool tracking = false;
};

} // namespace H 