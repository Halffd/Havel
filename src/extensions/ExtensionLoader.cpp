/*
 * ExtensionLoader.cpp - Pure dlopen-based extension loading
 *
 * SINGLE RESPONSIBILITY: Load .so files.
 * No registry, no capability checks, no type conversion.
 */
#include "ExtensionLoader.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

namespace havel {

ExtensionLoader::ExtensionLoader() {
  searchPaths_ = getStandardSearchPaths();
}

ExtensionLoader::~ExtensionLoader() {
  /* 
   * NOTE: We do NOT unload extensions.
   * See ExtensionLoader.hpp for why.
   * Extensions stay loaded until process exit.
   */
  for (auto& ext : loadedExtensions_) {
    if (ext.handle) {
#ifdef HAVE_DLFCN_H
      /* Intentionally NOT calling dlclose() */
      /* ext.handle stays valid until process exit */
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
  
  /* Current directory */
  paths.push_back(".");
  
  /* Standard system paths */
  paths.push_back("/usr/lib/havel/extensions");
  paths.push_back("/usr/local/lib/havel/extensions");
  
  /* User home directory */
  const char* home = std::getenv("HOME");
  if (home) {
    paths.push_back(std::string(home) + "/.havel/extensions");
  }
  
  /* Extension directory relative to executable */
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
    /* Try: name.so */
    std::string fullPath = path + "/" + name + suffix;
    if (access(fullPath.c_str(), F_OK) == 0) {
      return fullPath;
    }
    
    /* Try: libname.so */
    std::string libPath = path + "/lib" + name + suffix;
    if (access(libPath.c_str(), F_OK) == 0) {
      return libPath;
    }
  }
  
  return "";
}

bool ExtensionLoader::loadExtension(const std::string& path) {
#ifdef HAVE_DLFCN_H
  /* Check if already loaded */
  for (const auto& ext : loadedExtensions_) {
    if (ext.path == path) {
      return true;  /* Already loaded */
    }
  }
  
  /* Load the shared library */
  void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
  if (!handle) {
    const char* err = dlerror();
    fprintf(stderr, "[ExtensionLoader] dlopen failed for %s: %s\n", 
            path.c_str(), err ? err : "unknown error");
    return false;
  }
  
  /* Get the initialization function (C ABI) */
  auto initFn = reinterpret_cast<HavelExtensionInit>(
      dlsym(handle, "havel_extension_init"));
  if (!initFn) {
    const char* err = dlerror();
    fprintf(stderr, "[ExtensionLoader] dlsym failed for %s: %s\n", 
            path.c_str(), err ? err : "havel_extension_init not found");
    /* NOTE: Not calling dlclose() - handle stays loaded */
    return false;
  }
  
  /* Create extension record */
  LoadedExtension ext;
  ext.path = path;
  ext.handle = handle;
  ext.isLoaded = true;
  
  /* Extract name from path */
  std::string name = path;
  size_t lastSlash = name.rfind('/');
  if (lastSlash != std::string::npos) {
    name = name.substr(lastSlash + 1);
  }
  std::string suffix = getExtensionSuffix();
  if (name.size() > suffix.size() && 
      name.substr(name.size() - suffix.size()) == suffix) {
    name = name.substr(0, name.size() - suffix.size());
  }
  if (name.substr(0, 3) == "lib") {
    name = name.substr(3);
  }
  ext.name = name;
  
  /* 
   * IMPORTANT: We do NOT call initFn here.
   * The extension registers its functions via HostBridge.
   * We just load the .so and track it.
   * 
   * This separates:
   * - Loading (ExtensionLoader)
   * - Registration (HostBridge/ModuleRegistry)
   */
  
  loadedExtensions_.push_back(std::move(ext));
  return true;
#else
  (void)path;
  fprintf(stderr, "[ExtensionLoader] dlopen not available on this platform\n");
  return false;
#endif
}

bool ExtensionLoader::loadExtensionByName(const std::string& name) {
  std::string path = findExtension(name);
  if (path.empty()) {
    return false;
  }
  return loadExtension(path);
}

bool ExtensionLoader::isLoaded(const std::string& name) const {
  for (const auto& ext : loadedExtensions_) {
    if (ext.name == name && ext.isLoaded) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ExtensionLoader::getLoadedExtensions() const {
  std::vector<std::string> names;
  for (const auto& ext : loadedExtensions_) {
    if (ext.isLoaded) {
      names.push_back(ext.name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

} // namespace havel
