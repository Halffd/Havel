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
#include "host/ServiceRegistry.hpp"
#include "host/automation/AutomationService.hpp"
#include "utils/Logger.hpp"

#include <memory>
#include <atomic>

namespace havel::modules {

// Counter for generating unique task names
static std::atomic<int> g_taskCounter{0};

static std::string generateTaskName(const std::string& prefix) {
  return prefix + "_" + std::to_string(++g_taskCounter);
}

void registerAutomationModule(compiler::VMApi &api) {
  // autoClicker(button, interval) - Create and start auto clicker
  api.registerFunction("autoClicker", [](const std::vector<compiler::BytecodeValue> &args) {
    if (args.size() < 1) {
      throw std::runtime_error("autoClicker() requires at least a button argument");
    }
    
    std::string button = "left";
    int intervalMs = 100;
    
    if (std::holds_alternative<std::string>(args[0])) {
      button = std::get<std::string>(args[0]);
    }
    if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
      intervalMs = static_cast<int>(std::get<int64_t>(args[1]));
    }
    
    auto& registry = host::ServiceRegistry::instance();
    auto automationService = registry.get<host::AutomationService>();
    if (!automationService) {
      throw std::runtime_error("Automation service not available");
    }
    
    std::string name = generateTaskName("clicker");
    bool result = automationService->createAutoClicker(name, button, intervalMs);
    info("Started autoClicker: name={}, button={}, interval={}ms", name, button, intervalMs);
    return compiler::BytecodeValue(result);
  });
  
  // autoRunner(direction, interval) - Create and start auto runner
  api.registerFunction("autoRunner", [](const std::vector<compiler::BytecodeValue> &args) {
    if (args.size() < 1) {
      throw std::runtime_error("autoRunner() requires at least a direction argument");
    }
    
    std::string direction = "up";
    int intervalMs = 50;
    
    if (std::holds_alternative<std::string>(args[0])) {
      direction = std::get<std::string>(args[0]);
    }
    if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
      intervalMs = static_cast<int>(std::get<int64_t>(args[1]));
    }
    
    auto& registry = host::ServiceRegistry::instance();
    auto automationService = registry.get<host::AutomationService>();
    if (!automationService) {
      throw std::runtime_error("Automation service not available");
    }
    
    std::string name = generateTaskName("runner");
    bool result = automationService->createAutoRunner(name, direction, intervalMs);
    info("Started autoRunner: name={}, direction={}, interval={}ms", name, direction, intervalMs);
    return compiler::BytecodeValue(result);
  });
  
  // autoKeyPress(key, interval) - Create and start auto key presser
  api.registerFunction("autoKeyPress", [](const std::vector<compiler::BytecodeValue> &args) {
    if (args.size() < 1) {
      throw std::runtime_error("autoKeyPress() requires at least a key argument");
    }
    
    std::string key;
    int intervalMs = 100;
    
    if (std::holds_alternative<std::string>(args[0])) {
      key = std::get<std::string>(args[0]);
    }
    if (args.size() > 1 && std::holds_alternative<int64_t>(args[1])) {
      intervalMs = static_cast<int>(std::get<int64_t>(args[1]));
    }
    
    auto& registry = host::ServiceRegistry::instance();
    auto automationService = registry.get<host::AutomationService>();
    if (!automationService) {
      throw std::runtime_error("Automation service not available");
    }
    
    std::string name = generateTaskName("keypress");
    bool result = automationService->createAutoKeyPresser(name, key, intervalMs);
    info("Started autoKeyPress: name={}, key={}, interval={}ms", name, key, intervalMs);
    return compiler::BytecodeValue(result);
  });
  
  // stopAutomation(name) - Stop a named automation task
  api.registerFunction("stopAutomation", [](const std::vector<compiler::BytecodeValue> &args) {
    if (args.empty()) {
      throw std::runtime_error("stopAutomation() requires a task name");
    }
    
    if (!std::holds_alternative<std::string>(args[0])) {
      throw std::runtime_error("stopAutomation() requires a string name");
    }
    
    std::string name = std::get<std::string>(args[0]);
    
    auto& registry = host::ServiceRegistry::instance();
    auto automationService = registry.get<host::AutomationService>();
    if (!automationService) {
      throw std::runtime_error("Automation service not available");
    }
    
    automationService->removeTask(name);
    info("Stopped automation task: {}", name);
    return compiler::BytecodeValue(true);
  });
  
  // stopAllAutomation() - Stop all automation tasks
  api.registerFunction("stopAllAutomation", [](const std::vector<compiler::BytecodeValue> &args) {
    (void)args;
    
    auto& registry = host::ServiceRegistry::instance();
    auto automationService = registry.get<host::AutomationService>();
    if (!automationService) {
      throw std::runtime_error("Automation service not available");
    }
    
    automationService->stopAll();
    info("Stopped all automation tasks");
    return compiler::BytecodeValue(true);
  });
  
  // Register automation object with methods
  auto autoObj = api.makeObject();
  api.setField(autoObj, "clicker", api.makeFunctionRef("autoClicker"));
  api.setField(autoObj, "runner", api.makeFunctionRef("autoRunner"));
  api.setField(autoObj, "keyPress", api.makeFunctionRef("autoKeyPress"));
  api.setField(autoObj, "stop", api.makeFunctionRef("stopAutomation"));
  api.setField(autoObj, "stopAll", api.makeFunctionRef("stopAllAutomation"));
  api.setGlobal("automation", autoObj);
  
  info("Automation module registered");
}

} // namespace havel::modules
