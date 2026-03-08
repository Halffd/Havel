/*
 * RuntimeModule.cpp
 *
 * Runtime utilities module for Havel language.
 * Provides app control, debug utilities, and runOnce functionality.
 */
#include "RuntimeModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/Interpreter.hpp"
#include "process/Launcher.hpp"
#include "stdlib/TypeModule.hpp"

namespace havel::modules {

void registerRuntimeModule(Environment& env, Interpreter* interpreter) {
    if (!interpreter) {
        return;  // Can't register without interpreter
    }

    // =========================================================================
    // app.args - CLI arguments (stub - cliArgs is private in Interpreter)
    // =========================================================================

    auto argsArray = std::make_shared<std::vector<HavelValue>>();
    // Note: Would need public accessor in Interpreter for cliArgs

    auto appObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    (*appObj)["args"] = HavelValue(argsArray);
    
    // =========================================================================
    // app.enableReload/disableReload/toggleReload/reload
    // =========================================================================
    
    (*appObj)["enableReload"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>&) -> HavelResult {
        interpreter->enableReload();
        return HavelValue(true);
    }));
    
    (*appObj)["disableReload"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>&) -> HavelResult {
        interpreter->disableReload();
        return HavelValue(false);
    }));
    
    (*appObj)["toggleReload"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>&) -> HavelResult {
        interpreter->toggleReload();
        return HavelValue(interpreter->isReloadEnabled());
    }));
    
    (*appObj)["reload"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() >= 1) {
            if (auto b = args[0].get_if<bool>()) {
                if (*b) {
                    interpreter->enableReload();
                } else {
                    interpreter->disableReload();
                }
            }
        }
        return HavelValue(interpreter->isReloadEnabled());
    }));
    
    // =========================================================================
    // runOnce(id, [command]) - Execute command only once per session
    // Note: hasRunOnce/markRunOnce not yet implemented in Interpreter
    // =========================================================================

    env.Define("runOnce", HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("runOnce requires an id and a command string");
        }

        std::string id;
        if (args[0].is<std::string>()) {
            id = args[0].get<std::string>();
        } else {
            return HavelRuntimeError("runOnce: first argument must be a string id");
        }

        // Note: Full implementation requires hasRunOnce/markRunOnce in Interpreter
        // For now, just execute the command if provided
        
        // If there's a command string argument, execute it
        if (args.size() >= 2 && args[1].is<std::string>()) {
            std::string cmd = args[1].get<std::string>();
            auto result = Launcher::runShell(cmd);
            if (result.success) {
                info("runOnce('{}'): Command executed successfully", id);
                return HavelValue(true);
            } else {
                error("runOnce('{}'): Command failed: {}", id, result.error);
                return HavelValue(false);
            }
        }

        return HavelValue(true);
    })));
    
    // Register app module
    env.Define("app", HavelValue(appObj));
    
    // =========================================================================
    // Debug control functions
    // =========================================================================

    auto debugObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();

    (*debugObj)["showAST"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.size() >= 1) {
            if (auto b = args[0].get_if<bool>()) {
                interpreter->setShowAST(*b);
            }
        }
        return HavelValue(interpreter->getShowAST());
    }));

    (*debugObj)["stopOnError"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>& args) -> HavelValue {
        if (args.size() >= 1) {
            if (auto b = args[0].get_if<bool>()) {
                interpreter->setStopOnError(*b);
            }
        }
        return HavelValue(interpreter->getStopOnError());
    }));

    (*debugObj)["interpreterState"] = HavelValue(BuiltinFunction([interpreter](const std::vector<HavelValue>&) -> HavelValue {
        return HavelValue(interpreter->getInterpreterState());
    }));

    env.Define("debug", HavelValue(debugObj));
    
    // =========================================================================
    // Register stdlib modules
    // =========================================================================
    
    havel::stdlib::registerTypeModule(&env);
}

} // namespace havel::modules
