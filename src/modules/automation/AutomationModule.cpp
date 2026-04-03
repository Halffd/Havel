/*
 * AutomationModule.cpp - Automation module for Havel scripting
 * 
 * Exposes automation functionality to scripts:
 * - autoClicker(button, interval) - Auto clicker
 * - autoRunner(direction, interval) - Auto key runner (up/down/left/right)
 * - autoKeyPress(key, interval) - Auto key presser
 * - stopAutomation(name) - Stop a named task
 * - stopAllAutomation() - Stop all tasks
 */
#include "AutomationModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "host/ServiceRegistry.hpp"
#include "host/automation/AutomationService.hpp"
#include "utils/Logger.hpp"

#include <atomic>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

// Counter for generating unique task names
static std::atomic<int> g_taskCounter{0};

static std::string generateTaskName(const std::string& prefix) {
  return prefix + "_" + std::to_string(++g_taskCounter);
}

void registerAutomationModule(VMApi &api) {
  HAVEL_BEGIN_MODULE("Automation");
  // autoClicker(button, interval) - Create and start auto clicker
  HAVEL_REGISTER_FUNCTION(api, "autoClicker", [](const auto& args) {
    HAVEL_ARG_CHECK(args, 1, "autoClicker() requires at least a button argument");
    
    // TODO: string pool integration for string args
    std::string button = "left";
    int intervalMs = HAVEL_GET_INT_ARG(args, 1, 100);
    
    HAVEL_REQUIRE_SERVICE(autoSvc, AutomationService);
    
    std::string name = generateTaskName("clicker");
    bool result = autoSvc->createAutoClicker(name, button, intervalMs);
    info("Started autoClicker: name={}, button={}, interval={}ms", name, button, intervalMs);
    return Value::makeBool(result);
  });
  
  // autoRunner(direction, interval) - Create and start auto runner
  HAVEL_REGISTER_FUNCTION(api, "autoRunner", [](const auto& args) {
    HAVEL_ARG_CHECK(args, 1, "autoRunner() requires at least a direction argument");
    
    std::string direction = "up";
    int intervalMs = HAVEL_GET_INT_ARG(args, 1, 50);
    
    HAVEL_REQUIRE_SERVICE(autoSvc, AutomationService);
    
    std::string name = generateTaskName("runner");
    bool result = autoSvc->createAutoRunner(name, direction, intervalMs);
    info("Started autoRunner: name={}, direction={}, interval={}ms", name, direction, intervalMs);
    return Value::makeBool(result);
  });
  
  // autoKeyPress(key, interval) - Create and start auto key presser
  HAVEL_REGISTER_FUNCTION(api, "autoKeyPress", [](const auto& args) {
    HAVEL_ARG_CHECK(args, 1, "autoKeyPress() requires at least a key argument");
    
    // TODO: string pool integration for string args
    std::string key;
    int intervalMs = HAVEL_GET_INT_ARG(args, 1, 100);
    
    HAVEL_REQUIRE_SERVICE(autoSvc, AutomationService);
    
    std::string name = generateTaskName("keypress");
    bool result = autoSvc->createAutoKeyPresser(name, key, intervalMs);
    info("Started autoKeyPress: name={}, key={}, interval={}ms", name, key, intervalMs);
    return Value::makeBool(result);
  });
  
  // stopAutomation(name) - Stop a named automation task
  HAVEL_REGISTER_FUNCTION(api, "stopAutomation", [](const auto& args) {
    HAVEL_ARG_CHECK(args, 1, "stopAutomation() requires a task name");
    
    // TODO: string pool integration for string args
    std::string name = "task_" + std::to_string(reinterpret_cast<uintptr_t>(&args[0]));
    
    HAVEL_REQUIRE_SERVICE(autoSvc, AutomationService);
    
    autoSvc->removeTask(name);
    info("Stopped automation task: {}", name);
    HAVEL_RETURN_SUCCESS();
  });
  
  // stopAllAutomation() - Stop all automation tasks
  HAVEL_REGISTER_FUNCTION(api, "stopAllAutomation", [](const auto& args) {
    (void)args;
    
    HAVEL_REQUIRE_SERVICE(autoSvc, AutomationService);
    
    autoSvc->stopAll();
    info("Stopped all automation tasks");
    HAVEL_RETURN_SUCCESS();
  });
  
  // Register automation object with methods
  HAVEL_CREATE_MODULE_OBJECT(api, autoObj, "automation");
  HAVEL_REGISTER_METHOD(autoObj, api, "clicker", "autoClicker");
  HAVEL_REGISTER_METHOD(autoObj, api, "runner", "autoRunner");
  HAVEL_REGISTER_METHOD(autoObj, api, "keyPress", "autoKeyPress");
  HAVEL_REGISTER_METHOD(autoObj, api, "stop", "stopAutomation");
  HAVEL_REGISTER_METHOD(autoObj, api, "stopAll", "stopAllAutomation");
  
  HAVEL_END_MODULE();
}

} // namespace havel::modules
