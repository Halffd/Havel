#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>

#include "../module/ModuleLoader.hpp"

namespace havel::compiler {

// Forward declarations
class BytecodeChunk;

// ============================================================================
// ModuleResolver - Handles module resolution and loading
// ============================================================================
class ModuleResolver {
public:
  struct ModuleInfo {
    std::string name;
    std::filesystem::path path;
    bool loaded = false;
    bool loading = false; // For circular dependency detection
  };

  struct ResolutionResult {
    bool found = false;
    std::filesystem::path path;
    std::string error;
  };

  explicit ModuleResolver(ModuleLoader& loader);

  // Module resolution
  ResolutionResult resolve(const std::string& moduleName,
                            const std::filesystem::path& basePath);

  // Module loading
  bool loadModule(const std::string& moduleName,
                  const std::filesystem::path& basePath,
                  BytecodeChunk& targetChunk);

  // Module path management
  void addModulePath(const std::filesystem::path& path);
  void removeModulePath(const std::filesystem::path& path);
  std::vector<std::filesystem::path> getModulePaths() const;

  // Module cache
  void clearCache();
  bool isModuleCached(const std::string& moduleName) const;
  void invalidateCache(const std::string& moduleName);

  // Circular dependency detection
  bool hasCircularDependency(const std::string& moduleName) const;
  std::vector<std::string> getCurrentLoadingChain() const;

  // Error handling
  std::vector<std::string> getErrors() const { return errors_; }
  void clearErrors() { errors_.clear(); }

private:
  ModuleLoader& loader_;
  std::vector<std::filesystem::path> modulePaths_;
  std::unordered_map<std::string, ModuleInfo> moduleCache_;
  std::vector<std::string> loadingStack_;
  std::vector<std::string> errors_;

  ResolutionResult searchInPaths(const std::string& moduleName,
                                    const std::filesystem::path& basePath);
  bool isValidModuleFile(const std::filesystem::path& path) const;
  std::string moduleNameToFileName(const std::string& moduleName) const;
  void recordError(const std::string& message);
};

// ============================================================================
// ErrorReporter - Centralized error reporting for compiler
// ============================================================================
class ErrorReporter {
public:
  struct Error {
    std::string message;
    std::string file;
    uint32_t line = 0;
    uint32_t column = 0;
    enum class Severity { Warning, Error, Fatal } severity;
  };

  // Singleton access
  static ErrorReporter& instance();

  // Error reporting
  void report(const std::string& message,
              const std::string& file = "",
              uint32_t line = 0,
              uint32_t column = 0,
              Error::Severity severity = Error::Severity::Error);

  void reportWarning(const std::string& message,
                      const std::string& file = "",
                      uint32_t line = 0,
                      uint32_t column = 0);

  void reportError(const std::string& message,
                    const std::string& file = "",
                    uint32_t line = 0,
                    uint32_t column = 0);

  void reportFatal(const std::string& message,
                    const std::string& file = "",
                    uint32_t line = 0,
                    uint32_t column = 0);

  // Error querying
  const std::vector<Error>& getErrors() const { return errors_; }
  bool hasErrors() const;
  bool hasFatalErrors() const;
  size_t getErrorCount() const { return errors_.size(); }
  size_t getWarningCount() const;

  // Error formatting
  std::string formatError(const Error& error) const;
  std::string formatAllErrors() const;

  // Error clearing
  void clear();
  void clearErrors() { clear(); }

  // Handler registration
  using ErrorHandler = std::function<void(const Error&)>;
  void setHandler(ErrorHandler handler) { handler_ = handler; }

private:
  ErrorReporter() = default;
  std::vector<Error> errors_;
  ErrorHandler handler_;

  void notifyHandler(const Error& error);
};

} // namespace havel::compiler
