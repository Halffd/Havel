#pragma once

/**
 * ExtensionLoader.hpp - Pure dlopen-based extension loading
 *
 * SINGLE RESPONSIBILITY: Load/unload .so files via dlopen.
 * Does NOT handle:
 * - Registry (that's ModuleRegistry)
 * - Capability checks (that's CapabilityManager)
 * - Type conversion (that's ExtensionAPI wrapper)
 *
 * This separation prevents ModuleLoader from becoming god object v2.
 */

#include "HavelCAPI.h"

#include <string>
#include <vector>
#include <memory>

namespace havel {

/**
 * LoadedExtension - Handle for a loaded extension
 */
struct LoadedExtension {
  std::string name;
  std::string path;
  void* handle = nullptr;
  bool isLoaded = false;
  
  /* IMPORTANT: We do NOT support hot unload.
   * Once loaded, extension stays loaded until process exit.
   * 
   * Why?
   * - dlclose() + dangling function pointers = crash
   * - Python doesn't truly unload extensions either
   * - Marked as "experimental" if we ever add it
   */
};

/**
 * ExtensionLoader - Minimal dlopen wrapper
 */
class ExtensionLoader {
public:
  ExtensionLoader();
  ~ExtensionLoader();
  
  /* ==========================================================================
   * Loading (single responsibility)
   * ========================================================================== */
  
  /// Load extension from path
  bool loadExtension(const std::string& path);
  
  /// Load extension by name (searches standard paths)
  bool loadExtensionByName(const std::string& name);
  
  /// Check if extension is loaded
  bool isLoaded(const std::string& name) const;
  
  /// Get list of loaded extensions
  std::vector<std::string> getLoadedExtensions() const;
  
  /* ==========================================================================
   * Search paths
   * ========================================================================== */
  
  void addSearchPath(const std::string& path);
  static std::vector<std::string> getStandardSearchPaths();
  
  /* ==========================================================================
   * NO UNLOAD
   * 
   * Hot unload is intentionally NOT supported.
   * See LoadedExtension comment for why.
   * ========================================================================== */
  
private:
  std::vector<LoadedExtension> loadedExtensions_;
  std::vector<std::string> searchPaths_;
  
  std::string findExtension(const std::string& name) const;
  static std::string getExtensionSuffix();
};

} // namespace havel
