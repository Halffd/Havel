#pragma once

/**
 * ExtensionLoader.hpp - Load and manage native extensions
 *
 * Loads .so/.dll extensions via dlopen and initializes them.
 * Extensions register directly with VM - no HostBridge involvement.
 */

#include "ExtensionAPI.hpp"
#include "../havel-lang/compiler/bytecode/VM.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

namespace havel {

/**
 * LoadedExtension - Handle for a loaded extension
 */
struct LoadedExtension {
  std::string name;
  std::string path;
  void* handle = nullptr;
  ExtensionInfo info;
  bool isLoaded = false;
};

/**
 * ExtensionLoader - Manage native extension lifecycle
 */
class ExtensionLoader : public ExtensionAPI {
public:
  explicit ExtensionLoader(compiler::VM& vm);
  ~ExtensionLoader();
  
  // ==========================================================================
  // Extension loading
  // ==========================================================================
  
  /// Load extension from path
  /// @param path Path to .so/.dll file
  /// @return true if loaded successfully
  bool loadExtension(const std::string& path);
  
  /// Load extension by name (searches standard paths)
  /// @param name Extension name (e.g., "image", "ocr")
  /// @return true if loaded successfully
  bool loadExtensionByName(const std::string& name);
  
  /// Unload extension
  /// @param name Extension name
  /// @return true if unloaded successfully
  bool unloadExtension(const std::string& name);
  
  /// Check if extension is loaded
  bool isLoaded(const std::string& name) const;
  
  /// Get list of loaded extensions
  std::vector<std::string> getLoadedExtensions() const;
  
  // ==========================================================================
  // ExtensionAPI implementation (for extensions to register)
  // ==========================================================================
  
  void registerModule(const ExtensionModule& module) override;
  compiler::VM* getVM() override { return &vm_; }
  
  ExtensionValue createArray(const std::vector<ExtensionValue>& values) override;
  ExtensionValue createObject(const std::vector<std::pair<std::string, ExtensionValue>>& fields) override;
  
  // ==========================================================================
  // Search paths
  // ==========================================================================
  
  /// Add search path for extensions
  void addSearchPath(const std::string& path);
  
  /// Get standard search paths
  static std::vector<std::string> getStandardSearchPaths();

private:
  compiler::VM& vm_;
  std::unordered_map<std::string, LoadedExtension> loadedExtensions_;
  std::vector<std::string> searchPaths_;
  
  /// Find extension by name in search paths
  std::string findExtension(const std::string& name) const;
  
  /// Get extension suffix for current platform
  static std::string getExtensionSuffix();
};

} // namespace havel
