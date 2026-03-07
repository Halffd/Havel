/*
 * AsyncModule.cpp
 * 
 * Async/concurrency module for Havel language.
 * Note: This module requires interpreter context for full functionality.
 */
#include "AsyncModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "havel-lang/runtime/AsyncScheduler.hpp"
#include "havel-lang/runtime/Channel.hpp"

namespace havel::modules {

void registerAsyncModule(Environment& env, HostContext&) {
    // =========================================================================
    // Async task functions
    // =========================================================================
    
    env.Define("spawn", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("spawn requires 1 argument");
        }
        
        if (!args[0].is<std::shared_ptr<HavelFunction>>()) {
            return HavelRuntimeError("spawn requires a function");
        }
        
        auto func = args[0].get<std::shared_ptr<HavelFunction>>();
        std::string taskId = "task_" + std::to_string(std::rand());
        
        // Note: Full implementation requires interpreter context to Evaluate() the function body
        // This is a simplified version that registers the task but can't execute it
        // without interpreter access
        AsyncScheduler::getInstance().spawn(
            [func]() -> HavelResult {
                // TODO: This needs interpreter context to evaluate func->declaration->body
                return HavelRuntimeError("spawn execution requires interpreter context");
            },
            taskId);
        
        return HavelValue(taskId);
    }));
    
    env.Define("await", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 1) {
            return HavelRuntimeError("await requires 1 argument");
        }
        
        if (!args[0].isString()) {
            return HavelRuntimeError("await requires a task ID string");
        }
        
        std::string taskId = args[0].asString();
        return AsyncScheduler::getInstance().await(taskId);
    }));
    
    // =========================================================================
    // Channel functions
    // =========================================================================
    
    env.Define("channel", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 0) {
            return HavelRuntimeError("channel takes no arguments");
        }
        
        auto channel = std::make_shared<Channel>();
        return HavelValue(channel);
    }));
    
    env.Define("yield", HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() != 0) {
            return HavelRuntimeError("yield takes no arguments");
        }
        
        AsyncScheduler::getInstance().yield();
        return HavelValue(nullptr);
    }));
}

} // namespace havel::modules
