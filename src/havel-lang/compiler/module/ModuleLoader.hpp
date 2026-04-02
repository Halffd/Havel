#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "havel-lang/ast/AST.h"
#include "../core/BytecodeIR.hpp"

namespace havel::compiler {

// Forward declarations
class ByteCompiler;
class LexicalResolver;

// Represents a loaded module with its exports
struct LoadedModule {
  std::string path;
  std::string source;
  std::unique_ptr<ast::Program> ast;
  std::unique_ptr<BytecodeChunk> bytecode;
  std::unordered_map<std::string, BytecodeValue> exports;
  std::vector<std::string> exportNames; // Preserve order
  bool compiled = false;
};

// Module loader handles file loading, caching, and compilation
class ModuleLoader {
public:
  ModuleLoader();
  ~ModuleLoader();

  // Load a module from file path (relative or absolute)
  // Returns the loaded module or nullptr if not found
  LoadedModule *loadModule(const std::string &path,
                           const std::filesystem::path &basePath);

  // Check if module is already cached
  bool isModuleCached(const std::string &path) const;

  // Get cached module
  LoadedModule *getCachedModule(const std::string &path);

  // Clear module cache
  void clearCache();

  // Get search paths for modules
  void addSearchPath(const std::string &path);
  std::vector<std::string> getSearchPaths() const;

  // Resolve full path for a module
  std::optional<std::filesystem::path>
  resolveModulePath(const std::string &modulePath,
                    const std::filesystem::path &basePath) const;

private:
  // Module cache: resolved path -> loaded module
  std::unordered_map<std::string, std::unique_ptr<LoadedModule>> moduleCache_;

  // Search paths for modules
  std::vector<std::string> searchPaths_;

  // Read file contents
  std::optional<std::string> readFile(const std::filesystem::path &path) const;

  // Parse and compile module
  bool compileModule(LoadedModule &module, ByteCompiler &compiler);
};

} // namespace havel::compiler
