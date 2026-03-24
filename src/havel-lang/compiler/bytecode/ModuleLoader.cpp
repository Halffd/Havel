/*
 * ModuleLoader.cpp - Dynamic module loading with capability gating
 */
#include "ModuleLoader.hpp"
#include "HostBridge.hpp"

#include <algorithm>
#include <sstream>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

namespace havel::compiler {

ModuleLoader::ModuleLoader(const HostContext &ctx)
    : ctx_(ctx), caps_(HostBridgeCapabilities::Full()) {
  // Register built-in modules (provided by HostBridge)
  // These are lazy-loaded on first use
  registerBuiltin("io", {"io", "1.0", true, false, "", {"ioControl"}});
  registerBuiltin("file", {"file", "1.0", true, false, "", {"fileIO"}});
  registerBuiltin("process", {"process", "1.0", true, false, "", {"processExec"}});
  registerBuiltin("window", {"window", "1.0", true, false, "", {"windowControl"}});
  registerBuiltin("hotkey", {"hotkey", "1.0", true, false, "", {"hotkeyControl"}});
  registerBuiltin("mode", {"mode", "1.0", true, false, "", {"modeControl"}});
  registerBuiltin("clipboard", {"clipboard", "1.0", true, false, "", {"clipboardAccess"}});
  registerBuiltin("screenshot", {"screenshot", "1.0", true, false, "", {"screenshotAccess"}});
  registerBuiltin("audio", {"audio", "1.0", true, false, "", {"audioControl"}});
  registerBuiltin("brightness", {"brightness", "1.0", true, false, "", {"brightnessControl"}});
  registerBuiltin("automation", {"automation", "1.0", true, false, "", {"automationControl"}});
  registerBuiltin("browser", {"browser", "1.0", true, false, "", {"browserControl"}});
  registerBuiltin("textchunker", {"textchunker", "1.0", true, false, "", {"textChunkerAccess"}});
  registerBuiltin("mapmanager", {"mapmanager", "1.0", true, false, "", {"inputRemapping"}});
  registerBuiltin("alttab", {"alttab", "1.0", true, false, "", {"altTabControl"}});
  registerBuiltin("async", {"async", "1.0", true, false, "", {"asyncOps"}});
}

ModuleLoader::~ModuleLoader() {
  // Unload all extensions
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
  // Check if already loaded
  if (isLoaded(name)) {
    return true;
  }

  // Check if module exists
  auto it = registry_.find(name);
  if (it == registry_.end()) {
    return false;
  }

  // Check capabilities
  if (!canLoadModule(name)) {
    return false;
  }

  const auto &info = it->second;

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
  // Built-in modules are provided by HostBridge
  // HostBridge already registered all functions during install()
  // Mark as loaded
  loadedModules_.insert(name);
  return true;
}

bool ModuleLoader::loadStdlib(const std::string &name, VM &vm) {
  auto initIt = stdlibInitFns_.find(name);
  if (initIt == stdlibInitFns_.end()) {
    return false;
  }

  // Initialize the module
  initIt->second(vm);
  loadedModules_.insert(name);
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
  std::vector<std::string> modules(loadedModules_.begin(), loadedModules_.end());
  std::sort(modules.begin(), modules.end());
  return modules;
}

bool ModuleLoader::canLoadModule(const std::string &name) const {
  auto it = registry_.find(name);
  if (it == registry_.end()) {
    return false;
  }

  const auto &caps = it->second.requiredCapabilities;
  for (const auto &cap : caps) {
    if (cap == "ioControl" && !caps_.ioControl) return false;
    if (cap == "fileIO" && !caps_.fileIO) return false;
    if (cap == "processExec" && !caps_.processExec) return false;
    if (cap == "windowControl" && !caps_.windowControl) return false;
    if (cap == "hotkeyControl" && !caps_.hotkeyControl) return false;
    if (cap == "modeControl" && !caps_.modeControl) return false;
    if (cap == "clipboardAccess" && !caps_.clipboardAccess) return false;
    if (cap == "screenshotAccess" && !caps_.screenshotAccess) return false;
    if (cap == "asyncOps" && !caps_.asyncOps) return false;
    if (cap == "audioControl" && !caps_.audioControl) return false;
    if (cap == "brightnessControl" && !caps_.brightnessControl) return false;
    if (cap == "automationControl" && !caps_.automationControl) return false;
    if (cap == "browserControl" && !caps_.browserControl) return false;
    if (cap == "textChunkerAccess" && !caps_.textChunkerAccess) return false;
    if (cap == "inputRemapping" && !caps_.inputRemapping) return false;
    if (cap == "altTabControl" && !caps_.altTabControl) return false;
  }
  return true;
}

void ModuleLoader::setCapabilities(const HostBridgeCapabilities &caps) {
  caps_ = caps;
}

std::string ModuleLoader::loadExtension(const std::string &path) {
#ifdef HAVE_DLFCN_H
  void *handle = dlopen(path.c_str(), RTLD_LAZY);
  if (!handle) {
    return "";
  }

  // Look for module init function: havel_module_init
  using InitFn = const char *(*)(void);
  auto initFn = reinterpret_cast<InitFn>(dlsym(handle, "havel_module_init"));
  if (!initFn) {
    dlclose(handle);
    return "";
  }

  const char *moduleName = initFn();
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
  if (it == extensionHandles_.end()) {
    return false;
  }

#ifdef HAVE_DLFCN_H
  if (it->second) {
    dlclose(it->second);
  }
#endif

  extensionHandles_.erase(it);
  loadedModules_.erase(name);
  return true;
}

bool ModuleLoader::import(const std::string &importSpec, VM &vm) {
  ImportSpec spec = parseImportSpec(importSpec);

  // Load the module
  if (!loadModule(spec.moduleName, vm)) {
    return false;
  }

  // If importing specific member, create alias
  if (!spec.memberName.empty() && !spec.alias.empty()) {
    // TODO: Create alias in VM scope
    // This requires VM support for creating variable aliases
  }

  return true;
}

ImportSpec parseImportSpec(const std::string &spec) {
  ImportSpec result;

  // Remove "use " prefix if present
  std::string specStr = spec;
  if (specStr.rfind("use ", 0) == 0) {
    specStr = specStr.substr(4);
  }

  // Trim whitespace
  size_t start = specStr.find_first_not_of(" \t");
  size_t end = specStr.find_last_not_of(" \t");
  if (start == std::string::npos) {
    return result;
  }
  specStr = specStr.substr(start, end - start + 1);

  // Check for "as" alias
  size_t asPos = specStr.rfind(" as ");
  if (asPos != std::string::npos) {
    result.alias = specStr.substr(asPos + 4);
    specStr = specStr.substr(0, asPos);
    // Trim again
    end = specStr.find_last_not_of(" \t");
    specStr = specStr.substr(0, end + 1);
  }

  // Check for wildcard import
  if (specStr == "*") {
    result.importAll = true;
    return result;
  }

  // Check for module.member syntax
  size_t dotPos = specStr.find('.');
  if (dotPos != std::string::npos) {
    result.moduleName = specStr.substr(0, dotPos);
    std::string rest = specStr.substr(dotPos + 1);
    if (rest == "*") {
      result.importAll = true;
    } else {
      result.memberName = rest;
    }
  } else {
    result.moduleName = specStr;
  }

  return result;
}

} // namespace havel::compiler
