/*
 * AutomationModule.cpp
 * 
 * Automation module for Havel language.
 * Host binding - connects language to AutomationManager.
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/automation/AutomationManager.hpp"
#include "gui/HavelApp.hpp"

namespace havel::modules {

void registerAutomationModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI) {
    // Automation module depends on HavelApp singleton
    auto app = HavelApp::instance;
    if (!app || !app->automationManager) {
        return;  // Skip if automation manager not available
    }
    
    auto& am = *app->automationManager;
    
    // Create automation module object
    auto automationObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                std::string s = oss.str();
                if (s.find('.') != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && s[last] == '.') {
                        s = s.substr(0, last);
                    } else if (last != std::string::npos) {
                        s = s.substr(0, last + 1);
                    }
                }
                return s;
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };
    
    // =========================================================================
    // AutoClicker functions
    // =========================================================================
    
    (*automationObj)["startAutoClicker"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string button = args.empty() ? "left" : valueToString(args[0]);
        int intervalMs = args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 100;
        auto task = am.createAutoClicker(button, intervalMs);
        task->start();
        return HavelValue(task->getName());
    }));
    
    (*automationObj)["stopAutoClicker"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "AutoClicker" : valueToString(args[0]);
        auto task = am.getTask(taskName);
        if (task) {
            task->stop();
            return HavelValue(true);
        }
        return HavelValue(false);
    }));
    
    // =========================================================================
    // AutoKeyPresser functions
    // =========================================================================
    
    (*automationObj)["startAutoKeyPresser"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string key = args.empty() ? "space" : valueToString(args[0]);
        int intervalMs = args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 100;
        auto task = am.createAutoKeyPresser(key, intervalMs);
        task->start();
        return HavelValue(task->getName());
    }));
    
    (*automationObj)["stopAutoKeyPresser"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "AutoKeyPresser" : valueToString(args[0]);
        auto task = am.getTask(taskName);
        if (task) {
            task->stop();
            return HavelValue(true);
        }
        return HavelValue(false);
    }));
    
    // =========================================================================
    // AutoRunner functions
    // =========================================================================
    
    (*automationObj)["startAutoRunner"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string direction = args.empty() ? "w" : valueToString(args[0]);
        int intervalMs = args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 50;
        auto task = am.createAutoRunner(direction, intervalMs);
        task->start();
        return HavelValue(task->getName());
    }));
    
    (*automationObj)["stopAutoRunner"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "AutoRunner" : valueToString(args[0]);
        auto task = am.getTask(taskName);
        if (task) {
            task->stop();
            return HavelValue(true);
        }
        return HavelValue(false);
    }));
    
    // =========================================================================
    // Task management functions
    // =========================================================================
    
    (*automationObj)["getTask"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "" : valueToString(args[0]);
        auto task = am.getTask(taskName);
        if (task) {
            auto taskObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*taskObj)["name"] = HavelValue(task->getName());
            (*taskObj)["running"] = HavelValue(task->isRunning());
            return HavelValue(taskObj);
        }
        return HavelValue(nullptr);
    }));
    
    (*automationObj)["hasTask"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "" : valueToString(args[0]);
        return HavelValue(am.hasTask(taskName));
    }));
    
    (*automationObj)["removeTask"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "" : valueToString(args[0]);
        am.removeTask(taskName);
        return HavelValue(true);
    }));
    
    (*automationObj)["stopAllTasks"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>&) -> HavelResult {
        am.stopAll();
        return HavelValue(true);
    }));
    
    (*automationObj)["toggleTask"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string taskName = args.empty() ? "" : valueToString(args[0]);
        auto task = am.getTask(taskName);
        if (task) {
            task->toggle();
            return HavelValue(task->isRunning());
        }
        return HavelValue(false);
    }));
    
    // =========================================================================
    // Convenience functions
    // =========================================================================
    
    (*automationObj)["autoClick"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string button = args.empty() ? "left" : valueToString(args[0]);
        int intervalMs = args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 100;
        auto task = am.createAutoClicker(button, intervalMs);
        task->toggle();
        return HavelValue(task->getName());
    }));
    
    (*automationObj)["autoPress"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string key = args.empty() ? "space" : valueToString(args[0]);
        int intervalMs = args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 100;
        auto task = am.createAutoKeyPresser(key, intervalMs);
        task->toggle();
        return HavelValue(task->getName());
    }));
    
    (*automationObj)["autoRun"] = HavelValue(BuiltinFunction([&am, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        std::string direction = args.empty() ? "w" : valueToString(args[0]);
        int intervalMs = args.size() > 1 ? static_cast<int>(args[1].asNumber()) : 50;
        auto task = am.createAutoRunner(direction, intervalMs);
        task->toggle();
        return HavelValue(task->getName());
    }));
    
    // Register automation module
    env.Define("automation", HavelValue(automationObj));
}

} // namespace havel::modules
