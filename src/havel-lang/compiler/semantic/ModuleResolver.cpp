#include "ModuleResolver.hpp"
#include "../module/ModuleLoader.hpp"
#include "../core/BytecodeIR.hpp"
#include <fstream>

namespace havel::compiler {

// ============================================================================
// ModuleResolver Implementation
// ============================================================================

ModuleResolver::ModuleResolver(ModuleLoader& loader) : loader_(loader) {}

ModuleResolver::ResolutionResult ModuleResolver::resolve(
    const std::string& moduleName,
    const std::filesystem::path& basePath) {

  // Check cache first
  auto it = moduleCache_.find(moduleName);
  if (it != moduleCache_.end()) {
    if (it->second.loaded) {
      return ResolutionResult{true, it->second.path, ""};
    }
  }

  // Check for circular dependency
  if (hasCircularDependency(moduleName)) {
    recordError("Circular dependency detected: " + moduleName);
    return ResolutionResult{false, {}, "Circular dependency"};
  }

  // Search in module paths
  auto result = searchInPaths(moduleName, basePath);
  if (result.found) {
    ModuleInfo info;
    info.name = moduleName;
    info.path = result.path;
    moduleCache_[moduleName] = info;
  }

  return result;
}

bool ModuleResolver::loadModule(const std::string& moduleName,
                                 const std::filesystem::path& basePath,
                                 BytecodeChunk& targetChunk) {
  // Check if already loaded
  auto it = moduleCache_.find(moduleName);
  if (it != moduleCache_.end() && it->second.loaded) {
    return true;
  }

  // Check for circular dependency
  if (hasCircularDependency(moduleName)) {
    recordError("Circular dependency: " + moduleName);
    return false;
  }

  // Resolve module
  auto resolution = resolve(moduleName, basePath);
  if (!resolution.found) {
    recordError("Module not found: " + moduleName);
    return false;
  }

  // Mark as loading (for circular detection)
  loadingStack_.push_back(moduleName);
  if (it != moduleCache_.end()) {
    it->second.loading = true;
  }

  // TODO: Actually load and compile the module
  // This would involve:
  // 1. Reading the file
  // 2. Parsing
  // 3. Compiling to bytecode
  // 4. Merging into targetChunk

  // Mark as loaded
  if (it != moduleCache_.end()) {
    it->second.loaded = true;
    it->second.loading = false;
  }

  loadingStack_.pop_back();
  return true;
}

void ModuleResolver::addModulePath(const std::filesystem::path& path) {
  if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
    modulePaths_.push_back(path);
  }
}

void ModuleResolver::removeModulePath(const std::filesystem::path& path) {
  auto it = std::remove(modulePaths_.begin(), modulePaths_.end(), path);
  modulePaths_.erase(it, modulePaths_.end());
}

std::vector<std::filesystem::path> ModuleResolver::getModulePaths() const {
  return modulePaths_;
}

void ModuleResolver::clearCache() {
  moduleCache_.clear();
}

bool ModuleResolver::isModuleCached(const std::string& moduleName) const {
  auto it = moduleCache_.find(moduleName);
  return it != moduleCache_.end() && it->second.loaded;
}

void ModuleResolver::invalidateCache(const std::string& moduleName) {
  moduleCache_.erase(moduleName);
}

bool ModuleResolver::hasCircularDependency(const std::string& moduleName) const {
  return std::find(loadingStack_.begin(), loadingStack_.end(), moduleName)
         != loadingStack_.end();
}

std::vector<std::string> ModuleResolver::getCurrentLoadingChain() const {
  return loadingStack_;
}

ModuleResolver::ResolutionResult ModuleResolver::searchInPaths(
    const std::string& moduleName,
    const std::filesystem::path& basePath) {

  std::string fileName = moduleNameToFileName(moduleName);

  // First check relative to base path
  auto relativePath = basePath / fileName;
  if (isValidModuleFile(relativePath)) {
    return ResolutionResult{true, relativePath, ""};
  }

  // Check in module paths
  for (const auto& searchPath : modulePaths_) {
    auto fullPath = searchPath / fileName;
    if (isValidModuleFile(fullPath)) {
      return ResolutionResult{true, fullPath, ""};
    }
  }

  return ResolutionResult{false, {}, "Module not found in search paths"};
}

bool ModuleResolver::isValidModuleFile(const std::filesystem::path& path) const {
  return std::filesystem::exists(path) &&
         std::filesystem::is_regular_file(path);
}

std::string ModuleResolver::moduleNameToFileName(const std::string& moduleName) const {
  // Convert module name to file name
  // e.g., "std.math" -> "std/math.havel"
  std::string result = moduleName;
  for (char& c : result) {
    if (c == '.') {
      c = '/';
    }
  }
  return result + ".havel";
}

void ModuleResolver::recordError(const std::string& message) {
  errors_.push_back(message);
}

// ============================================================================
// ErrorReporter Implementation
// ============================================================================

ErrorReporter& ErrorReporter::instance() {
  static ErrorReporter instance;
  return instance;
}

void ErrorReporter::report(const std::string& message,
                            const std::string& file,
                            uint32_t line,
                            uint32_t column,
                            Error::Severity severity) {
  Error error;
  error.message = message;
  error.file = file;
  error.line = line;
  error.column = column;
  error.severity = severity;

  errors_.push_back(error);
  notifyHandler(error);
}

void ErrorReporter::reportWarning(const std::string& message,
                                   const std::string& file,
                                   uint32_t line,
                                   uint32_t column) {
  report(message, file, line, column, Error::Severity::Warning);
}

void ErrorReporter::reportError(const std::string& message,
                                 const std::string& file,
                                 uint32_t line,
                                 uint32_t column) {
  report(message, file, line, column, Error::Severity::Error);
}

void ErrorReporter::reportFatal(const std::string& message,
                                 const std::string& file,
                                 uint32_t line,
                                 uint32_t column) {
  report(message, file, line, column, Error::Severity::Fatal);
}

bool ErrorReporter::hasErrors() const {
  for (const auto& error : errors_) {
    if (error.severity == Error::Severity::Error ||
        error.severity == Error::Severity::Fatal) {
      return true;
    }
  }
  return false;
}

bool ErrorReporter::hasFatalErrors() const {
  for (const auto& error : errors_) {
    if (error.severity == Error::Severity::Fatal) {
      return true;
    }
  }
  return false;
}

size_t ErrorReporter::getWarningCount() const {
  size_t count = 0;
  for (const auto& error : errors_) {
    if (error.severity == Error::Severity::Warning) {
      ++count;
    }
  }
  return count;
}

std::string ErrorReporter::formatError(const Error& error) const {
  std::string result;

  switch (error.severity) {
    case Error::Severity::Warning:
      result += "[WARNING] ";
      break;
    case Error::Severity::Error:
      result += "[ERROR] ";
      break;
    case Error::Severity::Fatal:
      result += "[FATAL] ";
      break;
  }

  if (!error.file.empty()) {
    result += error.file;
    if (error.line > 0) {
      result += ":" + std::to_string(error.line);
      if (error.column > 0) {
        result += ":" + std::to_string(error.column);
      }
    }
    result += ": ";
  }

  result += error.message;
  return result;
}

std::string ErrorReporter::formatAllErrors() const {
  std::string result;
  for (const auto& error : errors_) {
    result += formatError(error) + "\n";
  }
  return result;
}

void ErrorReporter::clear() {
  errors_.clear();
}

void ErrorReporter::notifyHandler(const Error& error) {
  if (handler_) {
    handler_(error);
  }
}

} // namespace havel::compiler
