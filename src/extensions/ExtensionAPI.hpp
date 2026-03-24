#pragma once

/**
 * ExtensionAPI.hpp - Clean ABI for native extensions
 *
 * This is the contract between Havel VM and native extension modules.
 * Extensions register directly with VM - NO HostBridge dependency.
 *
 * Design principles:
 * - Minimal surface area (only what extensions need)
 * - Stable ABI (can change VM internals without breaking extensions)
 * - Type-safe registration
 * - No Python C API complexity
 */

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <variant>
#include <optional>

namespace havel {

// Forward declarations - extensions don't need full VM internals
namespace compiler { class VM; }

/**
 * ExtensionValue - Value type for extension ↔ VM communication
 *
 * Simplified from BytecodeValue - only what extensions need.
 */
using ExtensionValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    std::string
>;

/**
 * ExtensionFunction - Function signature for extension exports
 */
using ExtensionFunction = std::function<ExtensionValue(const std::vector<ExtensionValue>&)>;

/**
 * ExtensionModule - Module definition for registration
 */
struct ExtensionModule {
  std::string name;
  std::vector<std::pair<std::string, ExtensionFunction>> functions;
  
  void addFunction(const std::string& name, ExtensionFunction fn) {
    functions.emplace_back(name, fn);
  }
};

/**
 * ExtensionAPI - Interface exposed to native extensions
 *
 * This is the ONLY thing extensions see.
 * VM internals are hidden.
 */
class ExtensionAPI {
public:
  virtual ~ExtensionAPI() = default;
  
  // Register a module with the VM
  virtual void registerModule(const ExtensionModule& module) = 0;
  
  // Convenience: add a function to a module
  void addFunction(const std::string& moduleName, const std::string& funcName, ExtensionFunction fn) {
    ExtensionModule mod;
    mod.name = moduleName;
    mod.addFunction(funcName, fn);
    registerModule(mod);
  }
  
  // Get VM pointer (for advanced extensions that need direct access)
  virtual compiler::VM* getVM() = 0;
  
  // Utility: create array from values
  virtual ExtensionValue createArray(const std::vector<ExtensionValue>& values) = 0;
  
  // Utility: create object from key-value pairs
  virtual ExtensionValue createObject(const std::vector<std::pair<std::string, ExtensionValue>>& fields) = 0;
};

/**
 * Extension Entry Point
 *
 * Every native extension must export this function:
 * 
 * extern "C" void havel_extension_init(ExtensionAPI& api);
 */
using ExtensionInitFn = void (*)(ExtensionAPI&);

/**
 * Extension Metadata (optional but recommended)
 */
struct ExtensionInfo {
  std::string name;
  std::string version;
  std::string description;
  std::vector<std::string> dependencies;
};

/**
 * Helper macro for extension entry point
 * 
 * Usage in extension .cpp:
 *   HAVEL_EXTENSION(my_extension) {
 *     // registration code
 *   }
 */
#define HAVEL_EXTENSION(name) \
  static void name##_init(havel::ExtensionAPI& api); \
  extern "C" void havel_extension_init(havel::ExtensionAPI& api) { \
    name##_init(api); \
  } \
  static void name##_init(havel::ExtensionAPI& api)

} // namespace havel
