/*
 * AsyncModule.cpp
 *
 * Async/concurrency module for Havel language.
 * Note: This module requires interpreter context for full functionality.
 */
#include "AsyncModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/Interpreter.hpp"

namespace havel::modules {

void registerAsyncModule(Environment& env, HostContext&) {
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
}

} // namespace havel::modules
