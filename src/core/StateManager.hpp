#pragma once
#include <map>
#include <any>
#include <mutex>

namespace havel {
class StateManager {
public:
    template<typename T>
    void Set(const std::string& key, T value) {
        std::lock_guard<std::mutex> lock(mutex);
        states[key] = value;
    }

    template<typename T>
    T Get(const std::string& key, T defaultValue = T()) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = states.find(key);
        if(it != states.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch(...) {}
        }
        return defaultValue;
    }

    bool Exists(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        return states.count(key);
    }

private:
    std::map<std::string, std::any> states;
    std::mutex mutex;
};
}; 