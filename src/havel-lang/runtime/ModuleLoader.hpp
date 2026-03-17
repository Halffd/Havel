// ModuleLoader.hpp
// Simple, explicit module loading system
// No singletons, no static registration, no macro magic

#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel {

class Environment;
class IHostAPI;
class Interpreter;

/**
 * Module registration function
 */
using ModuleFn = std::function<void(Environment &)>;

/**
 * Interpreter module registration function (has Interpreter access)
 * For modules that need to call user functions
 */
using InterpreterModuleFn = std::function<void(Environment &, Interpreter *)>;

/**
 * Host module registration function (has IHostAPI access)
 * Takes shared_ptr to keep HostAPI alive for lambdas
 */
using HostModuleFn =
    std::function<void(Environment &, std::shared_ptr<IHostAPI>)>;

/**
 * Simple module registry - NOT a singleton
 * Each Interpreter can have its own registry
 */
class ModuleLoader {
public:
  ModuleLoader() = default; // Public constructor

  /**
   * Register a standard library module
   */
  void add(const std::string &name, ModuleFn fn) {
    modules[name] = fn;
    hostModules[name] = false;
    interpreterModules[name] = false;
  }

  /**
   * Register an interpreter module (needs interpreter access)
   */
  void addInterpreter(const std::string &name, InterpreterModuleFn fn) {
    interpreterFns[name] = fn;
    hostModules[name] = false;
    interpreterModules[name] = true;
  }

  /**
   * Register a host module
   */
  void addHost(const std::string &name, HostModuleFn fn) {
    modules[name] = [fn](Environment &env) {
      // Wrapper - will fail at load time if hostAPI not provided
    };
    hostFns[name] = fn;
    hostModules[name] = true;
    interpreterModules[name] = false;
  }

  /**
   * Load a module
   */
  bool load(Environment &env, const std::string &name,
            std::shared_ptr<IHostAPI> hostAPI = nullptr,
            Interpreter *interpreter = nullptr) {
    // Check if interpreter module
    if (interpreterModules[name]) {
      auto it = interpreterFns.find(name);
      if (it != interpreterFns.end()) {
        it->second(env, interpreter);
        loaded.insert(name);
        return true;
      }
      return false;
    }

    // Check if host module
    if (hostModules[name]) {
      if (!hostAPI) {
        throw std::runtime_error("Host module '" + name +
                                 "' requires host API");
      }
      auto hostIt = hostFns.find(name);
      if (hostIt != hostFns.end()) {
        hostIt->second(env, hostAPI);
        loaded.insert(name);
        return true;
      }
      return false;
    }

    // Standard module
    auto it = modules.find(name);
    if (it == modules.end()) {
      return false;
    }

    it->second(env);
    loaded.insert(name);
    return true;
  }

  /**
   * Load all registered modules
   */
  void loadAll(Environment &env, std::shared_ptr<IHostAPI> hostAPI = nullptr) {
    for (const auto &[name, fn] : modules) {
      load(env, name, hostAPI);
    }
  }

  /**
   * Check if module exists
   */
  bool has(const std::string &name) const { return modules.count(name) > 0; }

  /**
   * Check if module is loaded
   */
  bool isLoaded(const std::string &name) const {
    return loaded.count(name) > 0;
  }

  /**
   * Get list of all modules
   */
  std::vector<std::string> list() const {
    std::vector<std::string> names;
    for (const auto &[name, fn] : modules) {
      names.push_back(name);
    }
    return names;
  }

  /**
   * Clear loaded state (for testing)
   */
  void clearLoaded() { loaded.clear(); }

private:
  std::unordered_map<std::string, ModuleFn> modules;
  std::unordered_map<std::string, HostModuleFn> hostFns;
  std::unordered_map<std::string, InterpreterModuleFn> interpreterFns;
  std::unordered_map<std::string, bool> hostModules;
  std::unordered_map<std::string, bool> interpreterModules;
  std::unordered_set<std::string> loaded;
};

} // namespace havel
