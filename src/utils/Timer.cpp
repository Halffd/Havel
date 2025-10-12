#include "Timer.hpp"
namespace havel {
std::unordered_map<std::shared_ptr<std::atomic<bool>>, std::shared_ptr<TimerManager::TimerInfo>> 
TimerManager::activeTimers;
std::mutex TimerManager::timersMutex;
}