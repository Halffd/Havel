/*
 * ExtensionLoader.cpp - Load and manage native extensions
 */
#include "ExtensionLoader.hpp"

#include <algorithm>
#include <cstring>

namespace havel {

ExtensionLoader::ExtensionLoader(compiler::VM& vm) : vm_(vm) {
  // Add standard search paths
  searchPaths_ = getStandardSearchPaths();
}

ExtensionLoader::~ExtensionLoader() {
  // Unload all extensions
  for (auto& [name, ext] : loadedExtensions_) {
    if (ext.handle) {
#ifdef HAVE_DLFCN_H
      dlclose(ext.handle);
#endif
    }
  }
  loadedExtensions_.clear();
}

std::string ExtensionLoader::getExtensionSuffix() {
#ifdef _WIN32
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::vector<std::string> ExtensionLoader::getStandardSearchPaths() {
  std::vector<std::string> paths;
  
  // Current directory
  paths.push_back(".");
  
  // Standard system paths
  paths.push_back("/usr/lib/havel/extensions");
  paths.push_back("/usr/local/lib/havel/extensions");
  
  // User home directory
  const char* home = std::getenv("HOME");
  if (home) {
    paths.push_back(std::string(home) + "/.havel/extensions");
  }
  
  // Extension directory relative to executable
  const char* exeDir = std::getenv("HAVEL_EXTENSION_DIR");
  if (exeDir) {
    paths.push_back(exeDir);
  }
  
  return paths;
}

void ExtensionLoader::addSearchPath(const std::string& path) {
  searchPaths_.push_back(path);
}

std::string ExtensionLoader::findExtension(const std::string& name) const {
  std::string suffix = getExtensionSuffix();
  
  for (const auto& path : searchPaths_) {
    std::string fullPath = path + "/" + name + suffix;
    
    // Check if file exists
    if (access(fullPath.c_str(), F_OK) == 0) {
      return fullPath;
    }
    
    // Also try with "lib" prefix
    std::string libPath = path + "/lib" + name + suffix;
    if (access(libPath.c_str(), F_OK) == 0) {
      return libPath;
    }
  }
  
  return "";
}

bool ExtensionLoader::loadExtension(const std::string& path) {
#ifdef HAVE_DLFCN_H
  // Check if already loaded
  for (const auto& [name, ext] : loadedExtensions_) {
    if (ext.path == path) {
      return true;  // Already loaded
    }
  }
  
  // Load the shared library
  void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
  if (!handle) {
    // Debug: print error
    const char* err = dlerror();
    if (err) {
      fprintf(stderr, "[ExtensionLoader] dlopen failed for %s: %s\n", path.c_str(), err);
    }
    return false;
  }
  
  // Get the initialization function
  auto initFn = reinterpret_cast<ExtensionInitFn>(dlsym(handle, "havel_extension_init"));
  if (!initFn) {
    const char* err = dlerror();
    if (err) {
      fprintf(stderr, "[ExtensionLoader] dlsym failed for %s: %s\n", path.c_str(), err);
    }
    dlclose(handle);
    return false;
  }
  
  // Create extension record
  LoadedExtension ext;
  ext.path = path;
  ext.handle = handle;
  ext.isLoaded = true;
  
  // Extract name from path
  std::string name = path;
  size_t lastSlash = name.rfind('/');
  if (lastSlash != std::string::npos) {
    name = name.substr(lastSlash + 1);
  }
  std::string suffix = getExtensionSuffix();
  if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
    name = name.substr(0, name.size() - suffix.size());
  }
  if (name.substr(0, 3) == "lib") {
    name = name.substr(3);
  }
  ext.name = name;
  
  // Initialize the extension
  try {
    initFn(*this);
  } catch (...) {
    dlclose(handle);
    return false;
  }
  
  loadedExtensions_[ext.name] = ext;
  return true;
#else
  (void)path;
  return false;
#endif
}

bool ExtensionLoader::loadExtensionByName(const std::string& name) {
  fprintf(stderr, "[ExtensionLoader] Looking for extension: %s\n", name.c_str());
  std::string path = findExtension(name);
  fprintf(stderr, "[ExtensionLoader] Found path: %s\n", path.empty() ? "(not found)" : path.c_str());
  if (path.empty()) {
    return false;
  }
  return loadExtension(path);
}

bool ExtensionLoader::unloadExtension(const std::string& name) {
  auto it = loadedExtensions_.find(name);
  if (it == loadedExtensions_.end()) {
    return false;
  }
  
#ifdef HAVE_DLFCN_H
  if (it->second.handle) {
    dlclose(it->second.handle);
  }
#endif
  
  loadedExtensions_.erase(it);
  return true;
}

bool ExtensionLoader::isLoaded(const std::string& name) const {
  return loadedExtensions_.find(name) != loadedExtensions_.end();
}

std::vector<std::string> ExtensionLoader::getLoadedExtensions() const {
  std::vector<std::string> names;
  for (const auto& [name, ext] : loadedExtensions_) {
    if (ext.isLoaded) {
      names.push_back(name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

void ExtensionLoader::registerModule(const ExtensionModule& module) {
  // Register each function with the VM
  for (const auto& [funcName, func] : module.functions) {
    std::string fullName = module.name + "." + funcName;
    
    // Wrap ExtensionFunction to work with VM's BytecodeValue
    vm_.registerHostFunction(fullName, [func](const std::vector<compiler::BytecodeValue>& args) {
      // Convert BytecodeValue → ExtensionValue
      std::vector<ExtensionValue> extArgs;
      extArgs.reserve(args.size());
      for (const auto& arg : args) {
        if (std::holds_alternative<std::nullptr_t>(arg)) {
          extArgs.push_back(nullptr);
        } else if (std::holds_alternative<bool>(arg)) {
          extArgs.push_back(std::get<bool>(arg));
        } else if (std::holds_alternative<int64_t>(arg)) {
          extArgs.push_back(std::get<int64_t>(arg));
        } else if (std::holds_alternative<double>(arg)) {
          extArgs.push_back(std::get<double>(arg));
        } else if (std::holds_alternative<std::string>(arg)) {
          extArgs.push_back(std::get<std::string>(arg));
        } else {
          extArgs.push_back(nullptr);  // Unsupported type
        }
      }
      
      // Call extension function
      ExtensionValue result = func(extArgs);
      
      // Convert ExtensionValue → BytecodeValue
      if (std::holds_alternative<std::nullptr_t>(result)) {
        return compiler::BytecodeValue(nullptr);
      } else if (std::holds_alternative<bool>(result)) {
        return compiler::BytecodeValue(std::get<bool>(result));
      } else if (std::holds_alternative<int64_t>(result)) {
        return compiler::BytecodeValue(std::get<int64_t>(result));
      } else if (std::holds_alternative<double>(result)) {
        return compiler::BytecodeValue(std::get<double>(result));
      } else if (std::holds_alternative<std::string>(result)) {
        return compiler::BytecodeValue(std::get<std::string>(result));
      }
      
      return compiler::BytecodeValue(nullptr);
    });
  }
}

ExtensionValue ExtensionLoader::createArray(const std::vector<ExtensionValue>& values) {
  // For now, return first value as a simple implementation
  // Full implementation would create a VM array
  if (values.empty()) {
    return nullptr;
  }
  return values[0];
}

ExtensionValue ExtensionLoader::createObject(const std::vector<std::pair<std::string, ExtensionValue>>& fields) {
  // For now, return first field's value as a simple implementation
  // Full implementation would create a VM object
  if (fields.empty()) {
    return nullptr;
  }
  return fields[0].second;
}

} // namespace havel
