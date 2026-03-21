/*
 * AsyncService.cpp
 *
 * Async/concurrency service implementation.
 */
#include "AsyncService.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <chrono>

namespace havel::host {

struct AsyncService::Impl {
    std::unordered_map<std::string, std::thread> tasks;
    std::unordered_map<std::string, std::atomic<bool>> taskRunning;
    mutable std::mutex tasksMutex;
    
    // Channels
    struct Channel {
        std::queue<std::string> queue;
        mutable std::mutex mtx;
        std::condition_variable cv;
        bool closed = false;
    };
    std::unordered_map<std::string, std::shared_ptr<Channel>> channels;
    std::mutex channelsMutex;
};

AsyncService::AsyncService() : m_impl(std::make_unique<Impl>()) {
}

AsyncService::~AsyncService() {
    // Wait for all tasks to complete
    std::lock_guard<std::mutex> lock(m_impl->tasksMutex);
    for (auto& [id, task] : m_impl->tasks) {
        if (task.joinable()) {
            task.join();
        }
    }
}

std::string AsyncService::spawn(std::function<void()> fn) {
    static std::atomic<uint64_t> counter{1};
    std::string taskId = "task_" + std::to_string(counter++);
    
    std::lock_guard<std::mutex> lock(m_impl->tasksMutex);
    m_impl->taskRunning[taskId] = true;
    
    m_impl->tasks.emplace(taskId, [this, fn = std::move(fn), taskId]() {
        try {
            fn();
        } catch (...) {
            // Task threw exception
        }
        m_impl->taskRunning[taskId] = false;
    });
    
    return taskId;
}

bool AsyncService::await(const std::string& taskId) {
    std::unique_lock<std::mutex> lock(m_impl->tasksMutex);
    auto it = m_impl->tasks.find(taskId);
    if (it == m_impl->tasks.end()) {
        return false;
    }
    
    // Release lock while joining
    lock.unlock();
    
    if (it->second.joinable()) {
        it->second.join();
    }
    
    lock.lock();
    m_impl->tasks.erase(it);
    return true;
}

bool AsyncService::isRunning(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(m_impl->tasksMutex);
    auto it = m_impl->taskRunning.find(taskId);
    return it != m_impl->taskRunning.end() && it->second;
}

std::vector<std::string> AsyncService::getTaskIds() const {
    std::lock_guard<std::mutex> lock(m_impl->tasksMutex);
    std::vector<std::string> ids;
    for (const auto& [id, running] : m_impl->taskRunning) {
        if (running) {
            ids.push_back(id);
        }
    }
    return ids;
}

bool AsyncService::cancel(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(m_impl->tasksMutex);
    auto it = m_impl->tasks.find(taskId);
    if (it != m_impl->tasks.end()) {
        // Note: Can't actually cancel a running thread in C++
        // Just remove from tracking
        m_impl->tasks.erase(it);
        return true;
    }
    return false;
}

bool AsyncService::createChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_impl->channelsMutex);
    if (m_impl->channels.find(name) != m_impl->channels.end()) {
        return false;  // Already exists
    }
    m_impl->channels[name] = std::make_shared<Impl::Channel>();
    return true;
}

bool AsyncService::send(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_impl->channelsMutex);
    auto it = m_impl->channels.find(name);
    if (it == m_impl->channels.end() || it->second->closed) {
        return false;
    }
    
    {
        std::lock_guard<std::mutex> chLock(it->second->mtx);
        it->second->queue.push(value);
    }
    it->second->cv.notify_one();
    return true;
}

std::string AsyncService::receive(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_impl->channelsMutex);
    auto it = m_impl->channels.find(name);
    if (it == m_impl->channels.end()) {
        return "";
    }
    
    std::unique_lock<std::mutex> chLock(it->second->mtx);
    it->second->cv.wait(chLock, [this, &it]() {
        return !it->second->queue.empty() || it->second->closed;
    });
    
    if (it->second->closed && it->second->queue.empty()) {
        return "";
    }
    
    std::string value = it->second->queue.front();
    it->second->queue.pop();
    return value;
}

std::string AsyncService::tryReceive(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_impl->channelsMutex);
    auto it = m_impl->channels.find(name);
    if (it == m_impl->channels.end() || it->second->queue.empty()) {
        return "";
    }
    
    std::string value = it->second->queue.front();
    it->second->queue.pop();
    return value;
}

bool AsyncService::closeChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_impl->channelsMutex);
    auto it = m_impl->channels.find(name);
    if (it == m_impl->channels.end()) {
        return false;
    }
    
    {
        std::lock_guard<std::mutex> chLock(it->second->mtx);
        it->second->closed = true;
    }
    it->second->cv.notify_all();
    return true;
}

bool AsyncService::isChannelClosed(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_impl->channelsMutex);
    auto it = m_impl->channels.find(name);
    return it != m_impl->channels.end() && it->second->closed;
}

void AsyncService::sleep(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int64_t AsyncService::now() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

} // namespace havel::host
