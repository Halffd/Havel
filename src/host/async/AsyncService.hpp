/*
 * AsyncService.hpp - DEPRECATED
 *
 * This service spawned OS threads and provided blocking operations.
 * It has been superseded by Havel's unified concurrency model:
 * 
 * - Scheduler: Cooperative, bytecode-aware, single-threaded
 * - Fiber: Per-goroutine execution context
 * - EventQueue: Non-blocking callback bridge for async operations
 *
 * Methods are stubbed to prevent compilation errors if referenced.
 * New code should use the unified concurrency model instead.
 *
 * Phase 3+ (main loop) will provide:
 * - thread.spawn() - Non-blocking spawning via EventQueue
 * - thread.await() - Fiber suspension + EventQueue callback
 * - channel.recv() - Fiber suspension + EventQueue callback
 * - time.sleep() - EventQueue timer callback
 * - All non-blocking, all cooperative via EventQueue
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace havel::host {

/**
 * AsyncService - DEPRECATED (stubbed for compatibility)
 *
 * Do not use. Use unified concurrency model instead.
 */
class AsyncService {
public:
    AsyncService() = default;
    ~AsyncService() = default;

    // DEPRECATED: Use unified model
    std::string spawn(std::function<void()> fn) {
        // Stubbed - spawn via EventQueue instead
        return "";
    }

    bool await(const std::string& taskId) {
        // Stubbed - use fiber suspension + EventQueue
        return false;
    }

    bool isRunning(const std::string& taskId) const {
        // Stubbed
        return false;
    }

    std::vector<std::string> getTaskIds() const {
        // Stubbed
        return {};
    }

    bool cancel(const std::string& taskId) {
        // Stubbed
        return false;
    }

    bool createChannel(const std::string& name) {
        // Stubbed
        return false;
    }

    bool send(const std::string& name, const std::string& value) {
        // Stubbed
        return false;
    }

    std::string receive(const std::string& name) {
        // Stubbed - use fiber suspension instead
        return "";
    }

    std::string tryReceive(const std::string& name) {
        // Stubbed
        return "";
    }

    bool closeChannel(const std::string& name) {
        // Stubbed
        return false;
    }

    bool isChannelClosed(const std::string& name) const {
        // Stubbed
        return false;
    }

    void sleep(int milliseconds) {
        // Stubbed - use EventQueue timer instead
    }

    int64_t now() const {
        // Stubbed
        return 0;
    }
};

}  // namespace havel::host
