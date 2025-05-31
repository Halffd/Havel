#pragma once
#include <string>
#include <vector>
#include <functional>

namespace havel {

class SequenceDetector {
public:
    SequenceDetector();
    ~SequenceDetector();
    
    void AddSequence(const std::string& sequence, std::function<void()> action);
    void ProcessKey(const std::string& key);
    void Reset();
    
private:
    struct Sequence {
        std::string pattern;
        std::function<void()> action;
    };
    
    std::vector<Sequence> sequences;
    std::string currentInput;
};

} // namespace H 