// ModuleLoader.cpp
// Module loading system with dynamic discovery and loading
// Simple, explicit, no magic

#include "ModuleLoader.hpp"
#include "../havel-lang/runtime/ModuleLoader.hpp"
#include "HostModules.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "havel-lang/runtime/StdLibModules.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>

namespace havel {
namespace modules {

// Module loading function type
using ModuleLoadFunction =
    std::function<bool(Environment &, std::shared_ptr<IHostAPI>)>;

// Registry of available modules
static std::unordered_map<std::string, ModuleLoadFunction> registeredModules;

// ModuleLoader implementation
ModuleLoader::ModuleLoader() {
  // Add default module search paths
  addModulePath("./modules");
  addModulePath("/usr/local/lib/havel/modules");
  addModulePath("/usr/lib/havel/modules");

  // Discover available modules
  discoverModules();
}

// Add module search path
void ModuleLoader::addModulePath(const std::string &path) {
  if (std::filesystem::exists(path)) {
    modulePaths.push_back(path);
  }
}

// Discover modules in all search paths
void ModuleLoader::discoverModules() {
  for (const auto &path : modulePaths) {
    if (!std::filesystem::exists(path)) {
      continue;
    }

    try {
      for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".hv") {
          std::string moduleName = entry.path().stem().string();
          std::string fullPath = entry.path().string();

          // Create a module descriptor (hostAPI will be set during loading)
          ModuleDescriptor descriptor(moduleName, fullPath, nullptr);
          modules[moduleName] = descriptor;
        }
      }
    } catch (const std::exception &e) {
      // Log error but continue discovering other modules
      // In a real implementation, this should use proper logging
      continue;
    }
  }
}

// Load a specific module by name
bool ModuleLoader::loadModule(const std::string &moduleName, Environment &env) {
  auto it = modules.find(moduleName);
  if (it == modules.end()) {
    return false;
  }

  auto &descriptor = it->second;
  if (descriptor.isLoaded) {
    return true; // Already loaded
  }

  try {
    // For now, we'll use a simple loading mechanism
    // In a real implementation, this would:
    // 1. Load the module file (.hv)
    // 2. Parse and execute it in a new environment
    // 3. Register the module's functions

    // Simplified implementation - just mark as loaded
    descriptor.isLoaded = true;

    // Create a simple module object to indicate successful loading
    auto moduleObj =
        std::make_shared<std::unordered_map<std::string, HavelValue>>();
    (*moduleObj)["loaded"] = HavelValue(true);
    (*moduleObj)["name"] = HavelValue(moduleName);
    (*moduleObj)["path"] = HavelValue(descriptor.path);

    // Register the module object with the module name
    env.Define(moduleName, HavelValue(moduleObj));

    return true;
  } catch (const std::exception &e) {
    return false;
  }
}

// Load all discovered modules
void loadAllModules(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  // Create module loader using havel-lang version
  havel::ModuleLoader loader;

  // Register and load stdlib modules
  registerStdLibModules(loader);
  loadStdLibModules(env, loader);

  // Initialize service registry with all services
  initializeServiceRegistry(hostAPI);

  // Register and load host modules
  registerHostModules(loader);
  loadHostModules(env, loader, hostAPI);

  // Load discovered modules using the modules ModuleLoader
  modules::ModuleLoader modulesLoader;
  for (const auto &pair : modulesLoader.getDiscoveredModules()) {
    modulesLoader.loadModule(pair.first, env);
  }

  // =========================================================================
  // Backwards-compatible global aliases for common host APIs
  // =========================================================================

  defineHostAlias(env, "send", "io", "send");
  defineHostAlias(env, "mouse", "io", "mouse");
  defineHostAlias(env, "scroll", "io", "mouse.scroll");
  defineHostAlias(env, "mousemove", "io", "mouse.move");
  defineHostAlias(env, "run", "launcher", "run");
  defineHostAlias(env, "runAsync", "launcher", "runAsync");
  defineHostAlias(env, "runDetached", "launcher", "runDetached");
  defineHostAlias(env, "runShell", "launcher", "runShell");
  defineHostAlias(env, "terminal", "launcher", "terminal");
  defineHostAlias(env, "play", "media", "play");
  defineHostAlias(env, "window.active", "window", "getActiveWindow");
  defineGlobalAlias(env, "sleep", "sleep");
}

// Get list of loaded modules
std::vector<std::string> ModuleLoader::getLoadedModules() const {
  std::vector<std::string> loaded;
  for (const auto &pair : modules) {
    if (pair.second.isLoaded) {
      loaded.push_back(pair.first);
    }
  }
  return loaded;
}

// Unload a module
bool ModuleLoader::unloadModule(const std::string &moduleName) {
  auto it = modules.find(moduleName);
  if (it == modules.end()) {
    return false;
  }

  it->second.isLoaded = false;
  return true;
}

// Register a module type
void ModuleLoader::registerModuleType(const std::string &name,
                                      ModuleLoadFunction loadFunc) {
  registeredModules[name] = loadFunc;
}

// Helper to split dotted paths
static std::vector<std::string> splitPath(const std::string &path) {
  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string item;
  while (std::getline(ss, item, '.')) {
    if (!item.empty()) {
      parts.push_back(item);
    }
  }
  return parts;
}

// Define alias like: send -> io.send
bool defineHostAlias(Environment &env, const std::string &alias,
                     const std::string &moduleName,
                     const std::string &memberPath) {
  auto moduleVal = env.Get(moduleName);
  if (!moduleVal || !moduleVal->isObject()) {
    return false;
  }

  HavelValue current = *moduleVal;
  for (const auto &part : splitPath(memberPath)) {
    if (!current.isObject()) {
      return false;
    }
    auto obj = current.asObject();
    if (!obj) {
      return false;
    }
    auto it = obj->find(part);
    if (it == obj->end()) {
      return false;
    }
    current = it->second;
  }

  // MIGRATED TO BYTECODE VM: env.Define(alias, current);
  return true;
}

// Define global alias
bool defineGlobalAlias(Environment &env, const std::string &alias,
                       const std::string &sourceName) {
  auto val = env.Get(sourceName);
  if (!val) {
    return false;
  }
  // MIGRATED TO BYTECODE VM: env.Define(alias, *val);
  return true;
}

} // namespace modules
} // namespace havel
