/*
 * ModuleLoader.cpp - Dynamic module loading
 * 
 * Simple lazy loading:
 * - Modules loaded on first 'use' statement
 * - No security checks here (that's ExecutionPolicy)
 * - Loader just loads
 */
#include "ModuleLoader.hpp"

#include <algorithm>
#include <sstream>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

namespace havel::compiler {

ModuleLoader::ModuleLoader(const HostContext &ctx)
    : ctx_(ctx), policy_(ExecutionPolicy::DefaultPolicy()) {
  // Register built-in modules (metadata only - lazy loading)
  registerBuiltin("io", {"io", "1.0", true, false, ""});
  registerBuiltin("file", {"file", "1.0", true, false, ""});
  registerBuiltin("process", {"process", "1.0", true, false, ""});
  registerBuiltin("window", {"window", "1.0", true, false, ""});
  registerBuiltin("hotkey", {"hotkey", "1.0", true, false, ""});
  registerBuiltin("mode", {"mode", "1.0", true, false, ""});
  registerBuiltin("clipboard", {"clipboard", "1.0", true, false, ""});
  registerBuiltin("screenshot", {"screenshot", "1.0", true, false, ""});
  registerBuiltin("audio", {"audio", "1.0", true, false, ""});
  registerBuiltin("brightness", {"brightness", "1.0", true, false, ""});
  registerBuiltin("automation", {"automation", "1.0", true, false, ""});
  registerBuiltin("browser", {"browser", "1.0", true, false, ""});
  registerBuiltin("textchunker", {"textchunker", "1.0", true, false, ""});
  registerBuiltin("mapmanager", {"mapmanager", "1.0", true, false, ""});
  registerBuiltin("alttab", {"alttab", "1.0", true, false, ""});
  registerBuiltin("async", {"async", "1.0", true, false, ""});
}

ModuleLoader::~ModuleLoader() {
  for (auto &[name, handle] : extensionHandles_) {
    if (handle) {
#ifdef HAVE_DLFCN_H
      dlclose(handle);
#endif
    }
  }
  extensionHandles_.clear();
}

void ModuleLoader::registerBuiltin(const std::string &name, const ModuleInfo &info) {
  registry_[name] = info;
}

void ModuleLoader::registerStdlib(const std::string &name,
                                   std::function<void(VM &)> initFn,
                                   const ModuleInfo &info) {
  registry_[name] = info;
  stdlibInitFns_[name] = std::move(initFn);
}

bool ModuleLoader::loadModule(const std::string &name, VM &vm) {
  // Check if already loaded (idempotent)
  if (isLoaded(name)) {
    return true;
  }

  // Check if module exists
  auto it = registry_.find(name);
  if (it == registry_.end()) {
    return false;
  }

  // Check execution policy (optional - for embedding)
  const auto &info = it->second;
  if (info.isBuiltin) {
    Capability cap = capabilityForModule(name);
    if (!policy_.canExecute("module." + name)) {
      return false;
    }
  }

  // Load based on module type
  if (info.isBuiltin) {
    return loadBuiltin(name, vm);
  } else {
    return loadStdlib(name, vm);
  }
}

bool ModuleLoader::loadBuiltin(const std::string &name, VM &vm) {
  (void)name;
  (void)vm;
  // Built-in modules provided by HostBridge
  // HostBridge registers functions during install()
  loadedModules_[name] = true;
  return true;
}

bool ModuleLoader::loadStdlib(const std::string &name, VM &vm) {
  auto initIt = stdlibInitFns_.find(name);
  if (initIt == stdlibInitFns_.end()) {
    return false;
  }
  initIt->second(vm);
  loadedModules_[name] = true;
  return true;
}

bool ModuleLoader::isLoaded(const std::string &name) const {
  return loadedModules_.find(name) != loadedModules_.end();
}

std::vector<std::string> ModuleLoader::getAvailableModules() const {
  std::vector<std::string> modules;
  for (const auto &[name, info] : registry_) {
    modules.push_back(name);
  }
  std::sort(modules.begin(), modules.end());
  return modules;
}

std::vector<std::string> ModuleLoader::getLoadedModules() const {
  std::vector<std::string> modules;
  for (const auto &[name, loaded] : loadedModules_) {
    if (loaded) modules.push_back(name);
  }
  std::sort(modules.begin(), modules.end());
  return modules;
}

std::string ModuleLoader::loadExtension(const std::string &path) {
#ifdef HAVE_DLFCN_H
  void *handle = dlopen(path.c_str(), RTLD_LAZY);
  if (!handle) return "";

  using NameFn = const char *(*)();
  auto nameFn = reinterpret_cast<NameFn>(dlsym(handle, "havel_module_name"));
  if (!nameFn) {
    dlclose(handle);
    return "";
  }

  const char *moduleName = nameFn();
  if (!moduleName) {
    dlclose(handle);
    return "";
  }

  extensionHandles_[moduleName] = handle;
  return moduleName;
#else
  (void)path;
  return "";
#endif
}

bool ModuleLoader::unloadExtension(const std::string &name) {
  auto it = extensionHandles_.find(name);
  if (it == extensionHandles_.end()) return false;

#ifdef HAVE_DLFCN_H
  if (it->second) dlclose(it->second);
#endif

  extensionHandles_.erase(it);
  loadedModules_.erase(name);
  return true;
}

bool ModuleLoader::import(const std::string &importSpec, VM &vm) {
  ImportSpec spec = parseImportSpec(importSpec);

  // Load the module (lazy loading triggered here)
  if (!loadModule(spec.moduleName, vm)) {
    return false;
  }

  // Import specific members or wildcard
  if (spec.importAll) {
    // TODO: Inject all module symbols into VM scope
  } else if (!spec.members.empty()) {
    // TODO: Import specific members
  } else if (!spec.alias.empty()) {
    // TODO: Create module alias
  }

  return true;
}

ImportSpec parseImportSpec(const std::string &spec) {
  ImportSpec result;

  std::string specStr = spec;
  if (specStr.rfind("use ", 0) == 0) {
    specStr = specStr.substr(4);
  }

  size_t start = specStr.find_first_not_of(" \t");
  size_t end = specStr.find_last_not_of(" \t");
  if (start == std::string::npos) return result;
  specStr = specStr.substr(start, end - start + 1);

  // Check for "as" alias
  size_t asPos = specStr.rfind(" as ");
  if (asPos != std::string::npos) {
    result.alias = specStr.substr(asPos + 4);
    specStr = specStr.substr(0, asPos);
    end = specStr.find_last_not_of(" \t");
    specStr = specStr.substr(0, end + 1);
  }

  if (specStr == "*") {
    result.importAll = true;
    return result;
  }

  // Parse module.member syntax
  std::vector<std::string> parts;
  std::stringstream ss(specStr);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }

  if (parts.empty()) return result;

  result.moduleName = parts[0];

  if (parts.size() > 1) {
    if (parts.back() == "*") {
      result.importAll = true;
    } else {
      for (size_t i = 1; i < parts.size(); ++i) {
        result.members.push_back(parts[i]);
      }
    }
  }

  return result;
}

} // namespace havel::compiler
