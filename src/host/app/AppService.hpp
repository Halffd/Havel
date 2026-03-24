/*
 * AppService.hpp - Application lifecycle and system info
 *
 * Provides:
 * - Application info
 * - System information
 * - Environment access
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace havel::host {

/**
 * SystemInfo - System information
 */
struct SystemInfo {
  std::string os;
  std::string osVersion;
  std::string hostname;
  std::string username;
  std::string homeDir;
  int cpuCores = 0;
  uint64_t totalMemory = 0;  // bytes
};

/**
 * AppService - Application and system information
 */
class AppService {
public:
  AppService();
  ~AppService();

  // ==========================================================================
  // Application info
  // ==========================================================================

  /// Get application name
  std::string getAppName() const;

  /// Get application version
  std::string getAppVersion() const;

  /// Get application directory
  std::string getAppDir() const;

  // ==========================================================================
  // System info
  // ==========================================================================

  /// Get system information
  SystemInfo getSystemInfo() const;

  /// Get OS name
  std::string getOS() const;

  /// Get hostname
  std::string getHostname() const;

  /// Get username
  std::string getUsername() const;

  /// Get home directory
  std::string getHomeDir() const;

  /// Get CPU core count
  int getCpuCores() const;

  /// Get total memory in bytes
  uint64_t getTotalMemory() const;

  // ==========================================================================
  // Environment
  // ==========================================================================

  /// Get environment variable
  /// @param name Variable name
  /// @return Variable value or empty string if not found
  std::string getEnv(const std::string &name) const;

  /// Set environment variable
  /// @param name Variable name
  /// @param value Variable value
  /// @return true on success
  bool setEnv(const std::string &name, const std::string &value);

  /// Get all environment variables
  std::vector<std::string> getEnvVars() const;

  // ==========================================================================
  // System operations
  // ==========================================================================

  /// Open URL in default browser
  bool openUrl(const std::string &url);

  /// Open file with default application
  bool openFile(const std::string &path);

  /// Show file in file manager
  bool showInFolder(const std::string &path);

  /// Copy text to clipboard
  bool copyToClipboard(const std::string &text);

  /// Get clipboard text
  std::string getClipboardText() const;

private:
  struct Impl;
  Impl *impl_;
};

} // namespace havel::host
