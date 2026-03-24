/*
 * ModuleLoader.cpp - Dynamic module loading with capability gating
 * 
 * Key design decisions:
 * 1. LAZY LOADING: Modules only loaded on first 'use' statement
 * 2. RUNTIME CHECKS: Capability checked at load AND call time
 * 3. VM BINDING: Import injects symbols into VM scope
 * 4. TYPE-SAFE: Bitmask capabilities, not strings
 */
#include "ModuleLoader.hpp"

#include <algorithm>
#include <sstream>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

namespace havel::compiler {

ModuleLoader::ModuleLoader(const HostContext &ctx)
    : ctx_(ctx), caps_(HostBridgeCapabilities::Full()) {
  // Register built-in modules (metadata only - lazy loading)
  // Modules are NOT loaded until first 'use' statement
  registerBuiltin("io", {"io", "1.0", true, false, "", Capability::IO});
  registerBuiltin("file", {"file", "1.0", true, false, "", Capability::FileIO});
  registerBuiltin("process", {"process", "1.0", true, false, "", Capability::ProcessExec});
  registerBuiltin("window", {"window", "1.0", true, false, "", Capability::WindowControl});
  registerBuiltin("hotkey", {"hotkey", "1.0", true, false, "", Capability::HotkeyControl});
  registerBuiltin("mode", {"mode", "1.0", true, false, "", Capability::ModeControl});
  registerBuiltin("clipboard", {"clipboard", "1.0", true, false, "", Capability::ClipboardAccess});
  registerBuiltin("screenshot", {"screenshot", "1.0", true, false, "", Capability::ScreenshotAccess});
  registerBuiltin("audio", {"audio", "1.0", true, false, "", Capability::AudioControl});
  registerBuiltin("brightness", {"brightness", "1.0", true, false, "", Capability::BrightnessControl});
  registerBuiltin("automation", {"automation", "1.0", true, false, "", Capability::AutomationControl});
  registerBuiltin("browser", {"browser", "1.0", true, false, "", Capability::BrowserControl});
  registerBuiltin("textchunker", {"textchunker", "1.0", true, false, "", Capability::TextChunkerAccess});
  registerBuiltin("mapmanager", {"mapmanager", "1.0", true, false, "", Capability::InputRemapping});
  registerBuiltin("alttab", {"alttab", "1.0", true, false, "", Capability::AltTabControl});
  registerBuiltin("async", {"async", "1.0", true, false, "", Capability::AsyncOps});
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
  // NOTE: Does NOT load the module - lazy loading on first use
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

  // Check capabilities at load time
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
  // HostBridge registers functions during install()
  // Mark as loaded (lazy loading complete)
  loadedModules_[name] = true;
  return true;
}

bool ModuleLoader::loadStdlib(const std::string &name, VM &vm) {
  auto initIt = stdlibInitFns_.find(name);
  if (initIt == stdlibInitFns_.end()) {
    return false;
  }

  // Initialize the module
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
    if (loaded) {
      modules.push_back(name);
    }
  }
  std::sort(modules.begin(), modules.end());
  return modules;
}

bool ModuleLoader::canLoadModule(const std::string &name) const {
  auto it = registry_.find(name);
  if (it == registry_.end()) {
    return false;
  }

  // Check capability requirement
  Capability required = it->second.requiredCaps;
  return havel::compiler::hasCapability(caps_.caps, required);
}

bool ModuleLoader::checkCapability(const std::string &functionName) const {
  // Runtime capability check for function calls
  // This is the SECOND line of defense (after load-time check)
  
  // Map function prefixes to capabilities
  if (functionName.rfind("io.", 0) == 0 || functionName == "send") 
    return caps_.has(Capability::IO);
  if (functionName.rfind("readFile") == 0 || functionName.rfind("writeFile") == 0 ||
      functionName.rfind("file") == 0)
    return caps_.has(Capability::FileIO);
  if (functionName.rfind("execute") == 0 || functionName.rfind("getpid") == 0 ||
      functionName.rfind("process.") == 0)
    return caps_.has(Capability::ProcessExec);
  if (functionName.rfind("window.") == 0)
    return caps_.has(Capability::WindowControl);
  if (functionName.rfind("hotkey.") == 0)
    return caps_.has(Capability::HotkeyControl);
  if (functionName.rfind("clipboard.") == 0)
    return caps_.has(Capability::ClipboardAccess);
  if (functionName.rfind("screenshot.") == 0)
    return caps_.has(Capability::ScreenshotAccess);
  if (functionName.rfind("audio.") == 0)
    return caps_.has(Capability::AudioControl);
  if (functionName.rfind("brightness.") == 0)
    return caps_.has(Capability::BrightnessControl);
  if (functionName.rfind("automation.") == 0)
    return caps_.has(Capability::AutomationControl);
  if (functionName.rfind("browser.") == 0)
    return caps_.has(Capability::BrowserControl);
  if (functionName.rfind("mapmanager.") == 0)
    return caps_.has(Capability::InputRemapping);
  if (functionName.rfind("alttab.") == 0)
    return caps_.has(Capability::AltTabControl);
  
  // Default: allow (could be stdlib or VM builtin)
  return true;
}

std::string ModuleLoader::loadExtension(const std::string &path) {
#ifdef HAVE_DLFCN_H
  void *handle = dlopen(path.c_str(), RTLD_LAZY);
  if (!handle) {
    return "";
  }

  // Look for module init function with proper ABI
  // extern "C" void havel_module_init(ModuleAPI& api);
  using InitFn = void (*)(void *);
  auto initFn = reinterpret_cast<InitFn>(dlsym(handle, "havel_module_init"));
  if (!initFn) {
    dlclose(handle);
    return "";
  }

  // For now, just get module name
  // TODO: Proper ModuleAPI for function registration
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

  // Load the module (lazy loading triggered here)
  if (!loadModule(spec.moduleName, vm)) {
    return false;
  }

  // Import specific members or wildcard
  if (spec.importAll) {
    return importWithBinding(spec.moduleName, vm);
  } else if (!spec.members.empty()) {
    for (const auto &member : spec.members) {
      std::string alias = spec.alias.empty() ? member : spec.alias;
      importMember(spec.moduleName, member, alias, vm);
    }
  } else if (!spec.alias.empty()) {
    // Module alias: "use clipboard as cb"
    // TODO: Create module alias in VM
  }

  return true;
}

bool ModuleLoader::importWithBinding(const std::string &moduleName, VM &vm) {
  // Import all members from module into VM scope
  // This injects module functions as VM-level symbols
  // e.g., "use io.*" makes io.send available as just send()
  
  // TODO: VM support for symbol injection
  // For now, just mark as loaded
  (void)moduleName;
  (void)vm;
  return true;
}

bool ModuleLoader::importMember(const std::string &moduleName,
                                const std::string &memberName,
                                const std::string &alias,
                                VM &vm) {
  // Import specific member with alias
  // e.g., "use clipboard.get as clip_get"
  
  // TODO: VM support for creating function aliases
  (void)moduleName;
  (void)memberName;
  (void)alias;
  (void)vm;
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

  // Check for "as" alias (scan from right to avoid matching "class")
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

  // Parse module.member syntax (supports nested: fs.path.read)
  std::vector<std::string> parts;
  std::stringstream ss(specStr);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }

  if (parts.empty()) {
    return result;
  }

  result.moduleName = parts[0];

  if (parts.size() > 1) {
    // Check if last part is wildcard
    if (parts.back() == "*") {
      result.importAll = true;
    } else {
      // Member import(s)
      for (size_t i = 1; i < parts.size(); ++i) {
        result.members.push_back(parts[i]);
      }
    }
  }

  return result;
}

} // namespace havel::compiler
