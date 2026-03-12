/*
 * AppModule.cpp
 *
 * Application module for Havel language.
 * Provides application control (quit, restart, info, args).
 */
#include "AppModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/process/ProcessManager.hpp"
#include "core/io/EventListener.hpp"
#include <QApplication>
#include <QCoreApplication>

namespace havel::modules {

void registerAppModule(Environment& env, HostContext& ctx) {
    auto& io = *ctx.io;
    
    // Create app module object
    auto appObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // =========================================================================
    // app.quit() - Exit application
    // =========================================================================
    
    (*appObj)["quit"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        info("Quit requested - performing hard exit");
        
        // Stop EventListener FIRST to prevent use-after-free in KeyMap access
        if (io.GetEventListener()) {
            info("Stopping EventListener before exit...");
            io.GetEventListener()->Stop();
            info("EventListener stopped");
        }
        
        std::exit(0);
        return HavelValue(nullptr);  // Never reached
    }));
    
    // =========================================================================
    // app.restart() - Restart application
    // =========================================================================

    (*appObj)["restart"] = HavelValue(BuiltinFunction([&io](const std::vector<HavelValue>&) -> HavelResult {
        info("Restart requested");
        
        // Stop EventListener FIRST to prevent use-after-free in KeyMap access
        if (io.GetEventListener()) {
            info("Stopping EventListener before restart...");
            io.GetEventListener()->Stop();
            info("EventListener stopped");
        }
        
        if (QApplication::instance()) {
            // Use Qt's proper restart mechanism - exit code 42 signals restart
            QCoreApplication::exit(42);
            return HavelValue(true);
        }
        
        // No QApplication - can't restart properly
        return HavelRuntimeError("app.restart() requires GUI application");
    }));
    
    // =========================================================================
    // app.info() - Get application information
    // =========================================================================
    
    (*appObj)["info"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        auto infoObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        
        auto pid = ProcessManager::getCurrentPid();
        (*infoObj)["pid"] = HavelValue(static_cast<double>(pid));
        
        std::string exec = ProcessManager::getProcessExecutablePath(pid);
        (*infoObj)["path"] = HavelValue(exec);
        
        (*infoObj)["version"] = HavelValue("2.0.0");
        (*infoObj)["name"] = HavelValue("Havel");
        
        return HavelValue(infoObj);
    }));
    
    // =========================================================================
    // app.args - Get command line arguments
    // =========================================================================
    
    (*appObj)["args"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>&) -> HavelResult {
        // For now, return empty array - would need to store command line args at startup
        auto arr = std::make_shared<std::vector<HavelValue>>();
        return HavelValue(arr);
    }));
    
    // Register app module
    env.Define("app", HavelValue(appObj));
}

} // namespace havel::modules
