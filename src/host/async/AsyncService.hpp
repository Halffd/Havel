/*
 * AsyncService.hpp
 *
 * Async/concurrency service.
 * Provides task spawning, channels, and await functionality.
 * 
 * Pure C++ implementation with std::thread and std::async.
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace havel::host {

/**
 * AsyncService - Async task and channel management
 * 
 * Provides:
 * - spawn(fn): Run function in background
 * - await(taskId): Wait for task result
 * - Channel: Send/receive between tasks
 * - sleep(ms): Delay execution
 */
class AsyncService {
public:
    AsyncService();
    ~AsyncService();

    // =========================================================================
    // Task management
    // =========================================================================

    /// Spawn a function to run in background
    /// @param fn Function to execute
    /// @return Task ID
    std::string spawn(std::function<void()> fn);

    /// Wait for task to complete
    /// @param taskId Task ID
    /// @return true if completed
    bool await(const std::string& taskId);

    /// Check if task is running
    bool isRunning(const std::string& taskId) const;

    /// Get list of task IDs
    std::vector<std::string> getTaskIds() const;

    /// Cancel a task
    bool cancel(const std::string& taskId);

    // =========================================================================
    // Channels
    // =========================================================================

    /// Create a new channel
    /// @param name Channel name
    /// @return true if created
    bool createChannel(const std::string& name);

    /// Send value to channel
    /// @param name Channel name
    /// @param value Value to send
    /// @return true if sent
    bool send(const std::string& name, const std::string& value);

    /// Receive value from channel (blocking)
    /// @param name Channel name
    /// @return Received value or empty if closed
    std::string receive(const std::string& name);

    /// Try to receive value (non-blocking)
    /// @param name Channel name
    /// @return Received value or empty if empty/closed
    std::string tryReceive(const std::string& name);

    /// Close a channel
    bool closeChannel(const std::string& name);

    /// Check if channel is closed
    bool isChannelClosed(const std::string& name) const;

    // =========================================================================
    // Timing
    // =========================================================================

    /// Sleep for specified milliseconds
    void sleep(int milliseconds);

    /// Get current timestamp in milliseconds
    int64_t now() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace havel::host
