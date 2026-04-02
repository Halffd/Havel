#include "ModuleLoader.hpp"
#include "../core/ByteCompiler.hpp"
#include "havel-lang/parser/Parser.h"
#include <fstream>
#include <iostream>

namespace havel::compiler {

ModuleLoader::ModuleLoader() {
  // Add default search paths
  searchPaths_.push_back(".");
  searchPaths_.push_back("./lib");
  searchPaths_.push_back("/usr/local/lib/havel");
  searchPaths_.push_back("/usr/lib/havel");
}

ModuleLoader::~ModuleLoader() = default;

bool ModuleLoader::isModuleCached(const std::string& path) const {
  return moduleCache_.find(path) != moduleCache_.end();
}

LoadedModule* ModuleLoader::getCachedModule(const std::string& path) {
  auto it = moduleCache_.find(path);
  if (it != moduleCache_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void ModuleLoader::clearCache() {
  moduleCache_.clear();
}

void ModuleLoader::addSearchPath(const std::string& path) {
  searchPaths_.push_back(path);
}

std::vector<std::string> ModuleLoader::getSearchPaths() const {
  return searchPaths_;
}

std::optional<std::filesystem::path> ModuleLoader::resolveModulePath(
    const std::string& modulePath,
    const std::filesystem::path& basePath) const {
  
  // If absolute path, use directly
  if (std::filesystem::path(modulePath).is_absolute()) {
    if (std::filesystem::exists(modulePath)) {
      return std::filesystem::canonical(modulePath);
    }
    return std::nullopt;
  }

  // Try relative to base path first
  std::filesystem::path relativeToBase = basePath / modulePath;
  if (std::filesystem::exists(relativeToBase)) {
    return std::filesystem::canonical(relativeToBase);
  }

  // Try search paths
  for (const auto& searchPath : searchPaths_) {
    std::filesystem::path tryPath = std::filesystem::path(searchPath) / modulePath;
    if (std::filesystem::exists(tryPath)) {
      return std::filesystem::canonical(tryPath);
    }
  }

  return std::nullopt;
}

std::optional<std::string> ModuleLoader::readFile(const std::filesystem::path& path) const {
  std::ifstream file(path);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
  return content;
}

LoadedModule* ModuleLoader::loadModule(const std::string& path, 
                                       const std::filesystem::path& basePath) {
  // Resolve the full path
  auto resolvedPath = resolveModulePath(path, basePath);
  if (!resolvedPath) {
    std::cerr << "Module not found: " << path << std::endl;
    return nullptr;
  }

  std::string canonicalPath = resolvedPath->string();

  // Check cache first
  if (isModuleCached(canonicalPath)) {
    return getCachedModule(canonicalPath);
  }

  // Read file
  auto source = readFile(*resolvedPath);
  if (!source) {
    std::cerr << "Failed to read module: " << canonicalPath << std::endl;
    return nullptr;
  }

  // Create module entry
  auto module = std::make_unique<LoadedModule>();
  module->path = canonicalPath;
  module->source = *source;

  // Store in cache before compilation (to handle circular imports)
  LoadedModule* modulePtr = module.get();
  moduleCache_[canonicalPath] = std::move(module);

  return modulePtr;
}

} // namespace havel::compiler
