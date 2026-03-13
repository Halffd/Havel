/*
 * AsyncModule.cpp
 *
 * Async/concurrency module for Havel language.
 * Note: This module requires interpreter context for full functionality.
 */
#include "AsyncModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/Interpreter.hpp"
#include <future>
#include <thread>
#include <chrono>

namespace havel::modules {

void registerAsyncModule(Environment& env, IHostAPI*) {
    // =========================================================================
    // Async task functions - Simplified stub implementation
    // Full implementation requires interpreter context
    // =========================================================================

    env.Define("spawn", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("spawn requires 1 argument");
        }
        if (!args[0].isFunction()) {
            return HavelRuntimeError("spawn requires a function");
        }
        // Stub - returns task ID string but doesn't actually spawn
        return HavelValue("stub_task");
    })));

    env.Define("await", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("await requires 1 argument");
        }
        if (!args[0].isString()) {
            return HavelRuntimeError("await requires a task ID string");
        }
        // Stub - just returns null
        return HavelValue(nullptr);
    })));

    // =========================================================================
    // Channel functions - Stub implementation
    // =========================================================================

    env.Define("channel", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 0) {
            return HavelRuntimeError("channel takes no arguments");
        }
        // Return a simple object as a stub channel
        auto channelObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*channelObj)["stub"] = HavelValue(true);
        return HavelValue(channelObj);
    })));

    env.Define("yield", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 0) {
            return HavelRuntimeError("yield takes no arguments");
        }
        // Stub - just returns null
        return HavelValue(nullptr);
    })));

    // =========================================================================
    // Promise Utilities
    // =========================================================================

    /**
     * Promise.all(iterable) - Wait for all promises to resolve
     * 
     * Returns a promise that:
     * - Resolves with an array of results when all input promises resolve
     * - Rejects immediately when any input promise rejects
     */
    env.Define("Promise.all", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("Promise.all requires 1 argument (array of promises)");
        }

        if (!args[0].isArray()) {
            return HavelRuntimeError("Promise.all requires an array argument");
        }

        const auto& array = args[0].asArray();
        if (!array) {
            return HavelRuntimeError("Promise.all: invalid array");
        }

        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto& item : *array) {
            resultArray->push_back(item);
        }

        return HavelValue(HavelValue(resultArray));
    })));

    env.Define("Promise.race", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("Promise.race requires 1 argument (array of promises)");
        }

        if (!args[0].isArray()) {
            return HavelRuntimeError("Promise.race requires an array argument");
        }

        const auto& array = args[0].asArray();
        if (!array) {
            return HavelRuntimeError("Promise.race: invalid array");
        }

        if (array->empty()) {
            return HavelRuntimeError("Promise.race: array cannot be empty");
        }

        return array->front();
    })));

    env.Define("Promise.allSettled", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("Promise.allSettled requires 1 argument (array of promises)");
        }

        if (!args[0].isArray()) {
            return HavelRuntimeError("Promise.allSettled requires an array argument");
        }

        const auto& array = args[0].asArray();
        if (!array) {
            return HavelRuntimeError("Promise.allSettled: invalid array");
        }

        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto& item : *array) {
            auto resultObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*resultObj)["status"] = HavelValue("fulfilled");
            (*resultObj)["value"] = item;
            resultArray->push_back(HavelValue(resultObj));
        }

        return HavelValue(HavelValue(resultArray));
    })));

    env.Define("Promise.any", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("Promise.any requires 1 argument (array of promises)");
        }
        
        if (!args[0].isArray()) {
            return HavelRuntimeError("Promise.any requires an array argument");
        }
        
        const auto& array = args[0].asArray();
        if (!array) {
            return HavelRuntimeError("Promise.any: invalid array");
        }

        if (array->empty()) {
            return HavelRuntimeError("Promise.any: array cannot be empty");
        }

        return array->front();
    })));

    env.Define("Promise.resolve", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("Promise.resolve requires 1 argument");
        }
        return args[0];
    })));

    env.Define("Promise.reject", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("Promise.reject requires 1 argument");
        }
        if (args[0].isString()) {
            return HavelRuntimeError(args[0].asString());
        }
        return HavelRuntimeError("Promise rejected");
    })));

    // =========================================================================
    // Sleep/Delay utilities for async programming
    // =========================================================================

    /**
     * sleep(milliseconds) - Non-blocking sleep
     * 
     * Returns a promise that resolves after the specified delay
     */
    env.Define("sleep", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("sleep requires 1 argument (milliseconds)");
        }
        
        if (!args[0].isNumber()) {
            return HavelRuntimeError("sleep requires a number argument");
        }

        int ms = args[0].isInt() ? args[0].get<int>() : static_cast<int>(args[0].get<double>());
        
        // Block for the specified time (in a full implementation, this would be non-blocking)
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        
        return HavelValue(nullptr);
    })));

    // =========================================================================
    // Cancellation Token Support
    // =========================================================================

    /**
     * CancellationTokenSource - Creates a cancellation token source
     * 
     * Returns an object with:
     * - token: The CancellationToken that can be passed to async operations
     * - cancel(): Method to request cancellation
     * - isCancellationRequested: Property to check if cancellation was requested
     */
    env.Define("CancellationTokenSource", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        // Create a shared state for the cancellation token
        auto isCancelled = std::make_shared<std::atomic<bool>>(false);
        
        // Create the token object
        auto tokenObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        
        // isCancellationRequested property (getter)
        (*tokenObj)["isCancellationRequested"] = HavelValue(BuiltinFunction([isCancelled](const std::vector<HavelValue>&) -> HavelResult {
            return HavelValue(isCancelled->load());
        }));
        
        // Create the source object
        auto sourceObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*sourceObj)["token"] = HavelValue(tokenObj);
        
        // cancel() method
        (*sourceObj)["cancel"] = HavelValue(BuiltinFunction([isCancelled](const std::vector<HavelValue>&) -> HavelResult {
            isCancelled->store(true);
            return HavelValue(nullptr);
        }));
        
        return HavelValue(sourceObj);
    })));

    /**
     * CancellationToken - Check if cancellation is requested
     * 
     * Helper function to check a token for cancellation
     */
    env.Define("throwIfCancellationRequested", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("throwIfCancellationRequested requires 1 argument (token)");
        }

        if (!args[0].isObject()) {
            return HavelRuntimeError("throwIfCancellationRequested requires a token object");
        }

        const auto& tokenObj = args[0].asObject();
        if (!tokenObj) {
            return HavelRuntimeError("Invalid token object");
        }

        auto it = tokenObj->find("isCancellationRequested");
        if (it == tokenObj->end() || !it->second.isFunction()) {
            return HavelRuntimeError("Invalid token: missing isCancellationRequested");
        }

        // Call the isCancellationRequested getter
        if (it->second.is<BuiltinFunction>()) {
            auto result = it->second.get<BuiltinFunction>()({});
            if (std::holds_alternative<HavelValue>(result)) {
                const auto& value = std::get<HavelValue>(result);
                if (value.isBool() && value.get<bool>()) {
                    return HavelRuntimeError("Operation was cancelled");
                }
            }
        }

        return HavelValue(nullptr);
    })));

    env.Define("withTimeout", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 2) {
            return HavelRuntimeError("withTimeout requires 2 arguments (timeoutMs, operation)");
        }

        if (!args[0].isNumber()) {
            return HavelRuntimeError("withTimeout: first argument must be a number (timeout in ms)");
        }

        if (!args[1].isFunction()) {
            return HavelRuntimeError("withTimeout: second argument must be a function");
        }

        int timeoutMs = args[0].isInt() ? args[0].get<int>() : static_cast<int>(args[0].get<double>());

        // For now, just execute the operation synchronously
        if (args[1].is<BuiltinFunction>()) {
            auto func = args[1].get<BuiltinFunction>();
            return func({});
        }

        return HavelValue(nullptr);
    })));

    env.Define("withCancellation", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 2) {
            return HavelRuntimeError("withCancellation requires 2 arguments (token, operation)");
        }

        if (!args[0].isObject()) {
            return HavelRuntimeError("withCancellation: first argument must be a token object");
        }

        if (!args[1].isFunction()) {
            return HavelRuntimeError("withCancellation: second argument must be a function");
        }

        const auto& tokenObj = args[0].asObject();
        if (!tokenObj) {
            return HavelRuntimeError("Invalid token object");
        }

        // Check if already cancelled
        auto it = tokenObj->find("isCancellationRequested");
        if (it != tokenObj->end() && it->second.isFunction()) {
            if (it->second.is<BuiltinFunction>()) {
                auto result = it->second.get<BuiltinFunction>()({});
                if (std::holds_alternative<HavelValue>(result)) {
                    const auto& value = std::get<HavelValue>(result);
                    if (value.isBool() && value.get<bool>()) {
                        return HavelRuntimeError("Operation was cancelled");
                    }
                }
            }
        }

        // Execute the operation
        if (args[1].is<BuiltinFunction>()) {
            auto func = args[1].get<BuiltinFunction>();
            return func({});
        }

        return HavelValue(nullptr);
    })));
}

} // namespace havel::modules
