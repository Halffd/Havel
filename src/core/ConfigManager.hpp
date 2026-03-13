/*
 * ConfigManager.hpp
 *
 * Configuration management with:
 * - INI-style parsing
 * - File watching for hot reload
 * - Callback system for config changes
 * - Thread-safe access
 */
#pragma once

#include "ConfigObject.hpp"
#include "types.hpp"
#include <QStandardPaths>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace havel {

// Path handling helper functions
namespace ConfigPaths {
// Config base directory
std::string GetDefaultConfigDir();

extern std::string CONFIG_DIR;
extern std::string MAIN_CONFIG;
extern std::string HOTKEYS_DIR;

void SetConfigPath(const std::string &path, const std::string &basename = "havel.cfg");
std::string GetConfigPath();
std::string GetConfigPath(const std::string &filename);
void EnsureConfigDir();
} // namespace ConfigPaths

// Main configuration class
class Configs {
public:
  // Default config values
  static inline const std::vector<std::string> DEFAULT_GAMING_APPS = {
      "steam_app_default"};
  static inline const std::string GAMING_APPS_KEY = "General.GamingApps";
  static inline constexpr double DEFAULT_BRIGHTNESS = 1.0;
  static inline constexpr double STARTUP_BRIGHTNESS = 1.0;
  static inline constexpr int STARTUP_GAMMA = 1000;
  static inline constexpr double DEFAULT_BRIGHTNESS_AMOUNT = 0.05;
  static inline constexpr double DEFAULT_GAMMA_AMOUNT = 50;

  // Singleton access
  static Configs &Get();

  // Destructor
  ~Configs();

  // File operations
  void Load(const std::string &filename = "havel.cfg");
  void Save(const std::string &filename = "");
  void Reload();

  // Path access
  std::string getPath();
  static void SetPath(const std::string &newPath);

  // File watching
  void StartFileWatching(const std::string &filename = "havel.cfg");
  void StopFileWatching();
  void EnsureConfigFile(const std::string &filename = "havel.cfg");

  // Config access
  template <typename T> T Get(const std::string &key, const T &defaultVal) const {
    return config.get<T>(key, defaultVal);
  }
  
  void Set(const std::string &key, const std::string &value, bool save = false);

  // Convenience setters for numeric types
  template <typename T> void Set(const std::string &key, const T &value, bool save = false) {
    config.set<T>(key, value);
    if (save) RequestSave();
  }
  
  // Batch mode - use RequestSave() for multiple changes
  void BeginBatch() { savePending = false; }  // Disable auto-save
  void EndBatch() { RequestSave(); }  // Save all changes at once

  // Get all config keys
  std::vector<std::string> GetAllKeys() const {
    return config.keys();
  }

  // Convenience getters
  bool GetVerboseKeyLogging() const { return Get<bool>("Debug.VerboseKeyLogging", false); }
  bool GetVerboseWindowLogging() const { return Get<bool>("Debug.VerboseWindowLogging", false); }
  bool GetVerboseConditionLogging() const { return Get<bool>("Debug.VerboseConditionLogging", false); }
  
  // Gaming apps helpers
  std::vector<std::string> GetGamingApps() const {
    std::string apps = Get<std::string>("General.GamingApps", "");
    std::vector<std::string> result;
    std::stringstream ss(apps);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (!item.empty()) result.push_back(item);
    }
    return result;
  }
  std::vector<std::string> GetGamingAppsExclude() const {
    std::string apps = Get<std::string>("General.GamingAppsExclude", "");
    std::vector<std::string> result;
    std::stringstream ss(apps);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (!item.empty()) result.push_back(item);
    }
    return result;
  }
  std::vector<std::string> GetGamingAppsExcludeTitle() const {
    std::string apps = Get<std::string>("General.GamingAppsExcludeTitle", "");
    std::vector<std::string> result;
    std::stringstream ss(apps);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (!item.empty()) result.push_back(item);
    }
    return result;
  }
  std::vector<std::string> GetGamingAppsTitle() const {
    std::string apps = Get<std::string>("General.GamingAppsTitle", "");
    std::vector<std::string> result;
    std::stringstream ss(apps);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (!item.empty()) result.push_back(item);
    }
    return result;
  }
  
  // Get all config key-value pairs
  std::vector<std::string> GetConfigs() const {
    std::vector<std::string> configs;
    for (const auto &[key, val] : config.values()) {
      configs.push_back(key + "=" + val);
    }
    return configs;
  }

  // Watch callbacks
  using WatchCallback = std::function<void(const std::string &)>;
  void Watch(const std::string &key, WatchCallback callback);

  // Template version for typed callbacks (caller converts string to type)
  template <typename T>
  void Watch(const std::string &key, std::function<void(T)> callback) {
    Watch(key, [callback](const std::string &value) {
      try {
        callback(Convert<T>(value));
      } catch (...) {
        // Ignore conversion errors
      }
    });
  }

  // Debounced save - call this instead of Save(true) for batch operations
  void RequestSave();
  void ForceSave();  // Immediate save, cancels pending save

  // Debug
  void Print() const;

  // Type conversions (explicit specializations only)
  template <typename T> static T Convert(const std::string &val);

private:
  // File system helpers
  std::filesystem::file_time_type GetLastModified(const std::string &filepath) const;

  // Members
  std::string path;
  ConfigObject config;  // Use ConfigObject instead of raw map
  std::map<std::string, WatchCallback> watchers;

  // File watching
  std::atomic<bool> watching{false};
  std::thread watchingThread;
  std::mutex watchingMutex;

  // Debounced save
  std::atomic<bool> savePending{false};
  std::thread saveThread;
  std::mutex saveMutex;
  static constexpr int SAVE_DELAY_MS = 500;  // Debounce delay
};

// Inline implementation for Set (needs to call Save which is in cpp)
inline void Configs::Set(const std::string &key, const std::string &value, bool save) {
  config.set(key, value);
  if (save) {
    RequestSave();  // Use debounced save
  }
  // Notify watchers
  for (auto &[watchKey, callback] : watchers) {
    if (watchKey == key || watchKey.empty()) {
      try {
        callback(value);
      } catch (...) {
      }
    }
  }
}

// Convenience functions
inline void BackupConfig(const std::string &path = "havel.cfg") {
  std::string configPath = ConfigPaths::GetConfigPath(path);
  namespace fs = std::filesystem;
  try {
    fs::path backupPath = configPath + ".bak";
    if (fs::exists(configPath)) {
      fs::copy_file(configPath, backupPath, fs::copy_options::overwrite_existing);
    }
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Config backup failed: " << e.what() << "\n";
  }
}

inline void RestoreConfig(const std::string &path = "havel.cfg") {
  std::string configPath = ConfigPaths::GetConfigPath(path);
  namespace fs = std::filesystem;
  try {
    fs::path backupPath = configPath + ".bak";
    if (fs::exists(backupPath)) {
      fs::copy_file(backupPath, configPath, fs::copy_options::overwrite_existing);
    }
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Config restore failed: " << e.what() << "\n";
  }
}

} // namespace havel

// Global config instance
namespace havel {
extern Configs& g_Configs;
} // namespace havel

// Helper function to access config
inline havel::Configs& Conf() { return havel::g_Configs; }

// Template specializations for Configs::Convert
namespace havel {
template <> inline bool Configs::Convert<bool>(const std::string &val) {
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower == "true" || lower == "yes" || lower == "1" || lower == "on");
}
template <> inline int Configs::Convert<int>(const std::string &val) {
    return std::stoi(val);
}
template <> inline double Configs::Convert<double>(const std::string &val) {
    return std::stod(val);
}
template <> inline float Configs::Convert<float>(const std::string &val) {
    return std::stof(val);
}
template <> inline std::string Configs::Convert<std::string>(const std::string &val) {
    return val;
}
} // namespace havel
