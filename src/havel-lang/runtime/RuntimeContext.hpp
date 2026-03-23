/*
 * RuntimeContext.hpp - STUBBED (interpreter removed)
 * Runtime context was part of interpreter runtime
 */
#pragma once

#include <memory>
#include <functional>
#include <string>

namespace havel {

// Forward declarations
class IO;

/**
 * Runtime context - STUBBED
 */
struct RuntimeContext {
    void* env = nullptr;  // Was Environment*
    std::shared_ptr<IO> io;
    std::function<void(const std::string&)> executeShell;
    std::function<void(const std::string&)> reportError;
    std::function<void(const std::string&)> reportWarning;
    std::function<void(const std::string&)> reportInfo;

    bool isValid() const { return env != nullptr; }
};

RuntimeContext createPureContext(void* env);
RuntimeContext createFullContext(void* env, IO* io);

} // namespace havel
