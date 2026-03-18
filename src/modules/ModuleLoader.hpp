// ModuleLoader.hpp
// Module loading system with dynamic discovery and loading
// Simple, explicit, no magic

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

class Environment;
class IHostAPI;

namespace modules {

// Module descriptor for loaded modules
struct ModuleDescriptor {
  std::string name;
  std::string path;
  std::shared_ptr<IHostAPI> hostAPI;
  bool isLoaded = false;

  ModuleDescriptor(const std::string &name, const std::string &path,
                   std::shared_ptr<IHostAPI> hostAPI)
      : name(name), path(path), hostAPI(hostAPI) {}
};

// Module loader class
class ModuleLoader {
private:
  std::unordered_map<std::string, ModuleDescriptor> modules;
  std::vector<std::string> modulePaths;

public:
  ModuleLoader();
  ~ModuleLoader() = default;

  // Add module search path
  void addModulePath(const std::string &path);

  // Discover modules in all search paths
  void discoverModules();

  // Load a specific module by name
  bool loadModule(const std::string &moduleName, Environment &env);

  // Load all discovered modules
  void loadAllModules(Environment &env);

  // Get list of loaded modules
  std::vector<std::string> getLoadedModules() const;

  // Unload a module
  bool unloadModule(const std::string &moduleName);
};

// Define alias to module member (supports dotted paths like "mouse.scroll")
bool defineHostAlias(Environment &env, const std::string &alias,
                     const std::string &moduleName,
                     const std::string &memberPath);

// Define alias to existing global name
bool defineGlobalAlias(Environment &env, const std::string &alias,
                       const std::string &sourceName);

// Load all modules (stdlib + host + discovered)
void loadAllModules(Environment &env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
