/*
 * ConfigManager.hpp
 *
 * Features:
 * 1. File watching: Automatically detects config file changes and reloads
 * 2. Dynamic saving: Settings are saved automatically when changed via Set()
 * 3. Global access: Use g_Configs or havel::GlobalConfig() for easy access
 * 4. Hot reloading: Changes to config file are applied without restart
 *
 * Fixed template issues by:
 * 1. Moving the Configs class definition before the Mappings class
 * 2. Ensuring there are no duplicate template implementations
 * 3. Making the utility functions (BackupConfig, RestoreConfig) inline
 * 4. Making implementation in the header file directly to avoid linker errors
 */
#pragma once
#include "IO.hpp"
#include "process/Launcher.hpp"
#include "types.hpp"
#include "window/WindowManager.hpp"
#include <QStandardPaths>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
namespace havel {
// Path handling helper functions
namespace ConfigPaths {
// Config base directory - simple initialization
static std::string CONFIG_DIR = []() -> std::string {
  const char* home = std::getenv("HOME");
  if (home) {
    return std::string(home) + "/.config/havel/";
  }
  return "/tmp/havel/";  // Fallback
}();

// Config file paths
static std::string MAIN_CONFIG = CONFIG_DIR + "havel.cfg";
static std::string HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";

inline void SetConfigPath(const std::string &path, const std::string &basename = "havel.cfg") {
  CONFIG_DIR = path;
  MAIN_CONFIG = CONFIG_DIR + basename;
  HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";
}
inline std::string GetConfigPath() { return CONFIG_DIR; }
// Get path for a config file
inline std::string GetConfigPath(const std::string &filename) {
  if (filename.find('/') != std::string::npos) {
    // If already contains a path separator, use as-is
    return filename;
  }
  return CONFIG_DIR + filename;
}

// Ensure config directory exists
inline void EnsureConfigDir() {
  namespace fs = std::filesystem;
  try {
    // Validate CONFIG_DIR before attempting to create
    if (CONFIG_DIR.empty()) {
      error("Config directory path is empty, using fallback");
      CONFIG_DIR = "./havel/";
      MAIN_CONFIG = CONFIG_DIR + "havel.cfg";
      HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";
    }
    
    if (!fs::exists(CONFIG_DIR)) {
      fs::create_directories(CONFIG_DIR);
    }
    if (!fs::exists(HOTKEYS_DIR)) {
      fs::create_directories(HOTKEYS_DIR);
    }
  } catch (const fs::filesystem_error &e) {
    error("Failed to create config directories: " + std::string(e.what()) + "\nDirectory: " + CONFIG_DIR + " " + HOTKEYS_DIR + " Config: " + MAIN_CONFIG);
  }
}
} // namespace ConfigPaths

// Configs class definition
class Configs {
public:
  // Default config values
  static inline const std::vector<std::string> DEFAULT_GAMING_APPS = {
      "steam_app_default"};
  static inline const std::string GAMING_APPS_KEY = "General.GamingApps";
  
  // Brightness/Gamma defaults
  static inline constexpr double DEFAULT_BRIGHTNESS = 1.0;
  static inline constexpr double STARTUP_BRIGHTNESS = 1.0;
  static inline constexpr int STARTUP_GAMMA = 1000;
  static inline constexpr double DEFAULT_BRIGHTNESS_AMOUNT = 0.05;
  static inline constexpr double DEFAULT_GAMMA_AMOUNT = 50;

public:
  static Configs &Get() {
    static Configs instance;
    return instance;
  }
  std::string path = "";

  // File watching functionality
  void StartFileWatching(const std::string &filename = "havel.cfg") {
    if (watchingThread.joinable()) {
      watchingThread.join();
    }

    EnsureConfigFile(filename);
    watching = true;
    watchingThread = std::thread([this, filename]() {
      auto lastModified = GetLastModified(path);
      while (watching.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        try {
          auto currentModified = GetLastModified(path);
          if (currentModified > lastModified) {
            lastModified = currentModified;
            Reload();
          }
        } catch (...) {
          // Silently continue
        }
      }
    });
  }

  void StopFileWatching() {
    watching = false;
    if (watchingThread.joinable()) {
      watchingThread.join();
    }
  }

  void EnsureConfigFile(const std::string &filename = "havel.cfg") {
    path = ConfigPaths::GetConfigPath(filename);
    ConfigPaths::EnsureConfigDir();
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
      try {
        std::ofstream file(path);
        if (!file.is_open())
          throw std::runtime_error("Could not create config file: " + path);
        // Write sensible defaults
        file << "[Debug]" << std::endl;
        file << "VerboseKeyLogging=false" << std::endl;
        file << "VerboseWindowLogging=false" << std::endl;
        file << "VerboseConditionLogging=false" << std::endl;
        file << "[General]" << std::endl;
        file << "GamingApps=";
        for (size_t i = 0; i < Configs::DEFAULT_GAMING_APPS.size(); ++i) {
          file << Configs::DEFAULT_GAMING_APPS[i];
          if (i + 1 < Configs::DEFAULT_GAMING_APPS.size())
            file << ",";
        }
        file << std::endl;
        file << "DefaultBrightness=" << Configs::DEFAULT_BRIGHTNESS
             << std::endl;
        file << "StartupBrightness=" << Configs::STARTUP_BRIGHTNESS
             << std::endl;
        file << "StartupGamma=" << Configs::STARTUP_GAMMA << std::endl;
        file << "BrightnessAmount=" << Configs::DEFAULT_BRIGHTNESS_AMOUNT
             << std::endl;
        file << "GammaAmount=" << Configs::DEFAULT_GAMMA_AMOUNT << std::endl;
        file.close();
      } catch (const std::exception &e) {
        std::cerr << "Failed to create default config: " << e.what()
                  << std::endl;
      }
    }
  }
  std::string getPath() { return path; }
  static void SetPath(const std::string &newPath) {
    std::string dir = std::filesystem::path(newPath).parent_path();
    std::string basename = std::filesystem::path(newPath).filename();
    ConfigPaths::SetConfigPath(dir, basename);
  }
  void Load(const std::string &filename = "havel.cfg") {
    path = ConfigPaths::GetConfigPath(filename);

    std::ifstream file(path);

    std::string line, currentSection;
    int lineNumber = 0;

    while (std::getline(file, line)) {
      lineNumber++;

      // Trim leading and trailing whitespace
      line.erase(0, line.find_first_not_of(" \t\r"));
      line.erase(line.find_last_not_of(" \t\r") + 1);

      // Skip empty lines and comments
      if (line.empty() || line[0] == '#' || line[0] == ';') {
        continue;
      }

      // Handle section headers [SectionName]
      if (line[0] == '[') {
        size_t closeBracket = line.find(']');
        if (closeBracket != std::string::npos) {
          currentSection = line.substr(1, closeBracket - 1);
          // Trim section name
          currentSection.erase(0, currentSection.find_first_not_of(" \t"));
          currentSection.erase(currentSection.find_last_not_of(" \t") + 1);
        } else {
          std::cerr << "Warning: Malformed section header at line "
                    << lineNumber << ": " << line << std::endl;
        }
        continue;
      }

      // Handle key=value pairs
      size_t delim = line.find('=');
      if (delim != std::string::npos) {
        // Extract key and value
        std::string keyName = line.substr(0, delim);
        std::string value = line.substr(delim + 1);

        // Trim key name
        keyName.erase(0, keyName.find_first_not_of(" \t"));
        keyName.erase(keyName.find_last_not_of(" \t") + 1);

        // Build full key with section
        std::string fullKey =
            currentSection.empty() ? keyName : currentSection + "." + keyName;

        // Trim value and handle quoted strings
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Handle quoted values (remove surrounding quotes)
        if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
          char quote = value[0];
          value = value.substr(1); // Remove opening quote

          size_t endQuote = value.find(quote);
          if (endQuote != std::string::npos) {
            value = value.substr(0, endQuote); // Remove clos`ing quote
          } else {
            error("Warning: Unmatched quote at line " +
                  std::to_string(lineNumber));
          }
        }

        // Store the setting
        settings[fullKey] = value;

      } else {
        error("Warning: Invalid line format at line " +
              std::to_string(lineNumber) + ": " + line);
      }
    }

    file.close();
  }

  void Save(const std::string &filename = "havel.cfg") {
    path = ConfigPaths::GetConfigPath(filename);
    std::string tempPath = path + ".tmp";
    ConfigPaths::EnsureConfigDir();

    std::ofstream file(tempPath);
    if (!file.is_open()) {
      error("Error: Could not save config file to temporary path: " + tempPath);
      return;
    }

    std::string currentSection;

    // Sort settings by key to ensure consistent output
    std::map<std::string, std::string> sortedSettings(settings.begin(),
                                                      settings.end());

    for (const auto &[key, value] : sortedSettings) {
      size_t dotPos = key.find('.');
      if (dotPos == std::string::npos)
        continue; // Skip invalid keys
      std::string section = key.substr(0, dotPos);
      std::string name = key.substr(dotPos + 1);

      if (section != currentSection) {
        if (file.tellp() !=
            0) { // Don't write newline at the beginning of the file
          file << "\n";
        }
        file << "[" << section << "]\n";
        currentSection = section;
      }

      file << name << "=" << value << "\n";
    }
    file.close();

    // Atomically replace the old config with the new one
    try {
      std::filesystem::rename(tempPath, path);
    } catch (const std::filesystem::filesystem_error &e) {
      error("Error renaming temporary config file: ", e.what());
      // As a fallback, try to copy and delete
      try {
        std::filesystem::copy_file(
            tempPath, path, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(tempPath);
      } catch (const std::filesystem::filesystem_error &e2) {
        error("Error copying temporary config file: ", e2.what());
        return;
      }
    }
  }

  template <typename T> T Get(const std::string &key, T defaultValue) const {
    auto it = settings.find(key);
    if (it == settings.end())
      return defaultValue;
    return Convert<T>(it->second);
  }

  template <typename T> void Set(const std::string &key, T value) {
    std::ostringstream oss;
    oss << value;
    std::string oldValue = settings[key];
    settings[key] = oss.str();

    // Notify watchers
    if (watchers.find(key) != watchers.end()) {
      for (auto &watcher : watchers[key]) {
        watcher(oldValue, settings[key]);
      }
    }

    // Automatically save the config when a setting is changed
    if (!path.empty() && oldValue != settings[key]) {
      Save();
    }
  }

  template <typename T>
  void Watch(const std::string &key, std::function<void(T, T)> callback) {
    watchers[key].push_back(
        [=](const std::string &oldVal, const std::string &newVal) {
          T oldT = Convert<T>(oldVal);
          T newT = Convert<T>(newVal);
          callback(oldT, newT);
        });
  }

  void Reload() {
    auto oldSettings = settings;
    settings.clear();
    Load();

    for (const auto &[key, newVal] : settings) {
      if (oldSettings[key] != newVal) {
        for (auto &watcher : watchers[key]) {
          watcher(oldSettings[key], newVal);
        }
      }
    }
  }

  void Validate() const {
    const std::set<std::string> validKeys = {
        "Window.MoveSpeed", "Window.ResizeSpeed", "Hotkeys.GlobalSuspend",
        "UI.Theme"};

    for (const auto &[key, val] : settings) {
      if (validKeys.find(key) == validKeys.end()) {
        std::cerr << "Warning: Unknown config key '" << key << "'\n";
      }
    }
  }

  template <typename T>
  T Get(const std::string &key, T defaultValue, T min, T max) const {
    T value = Get(key, defaultValue);
    if (value < min || value > max) {
      std::cerr << "Config value out of range: " << key << "=" << value
                << " (Valid: " << min << "-" << max << ")\n";
      return defaultValue;
    }
    return value;
  }

  // Helpers for gaming apps (comma-separated string)
  std::vector<std::string> GetGamingApps() const {
    std::string apps = Get<std::string>("General.GamingApps", "");
    std::vector<std::string> result;
    std::istringstream iss(apps);
    std::string token;
    while (std::getline(iss, token, ',')) {
      if (!token.empty())
        result.push_back(token);
    }
    return result;
  }
  std::vector<std::string> GetGamingAppsExclude() const {
    std::string apps = Get<std::string>("General.GamingAppsExclude", "");
    std::vector<std::string> result;
    std::istringstream iss(apps);
    std::string token;
    while (std::getline(iss, token, ',')) {
      if (!token.empty())
        result.push_back(token);
    }
    return result;
  }
  std::vector<std::string> GetGamingAppsExcludeTitle() const {
    std::string apps = Get<std::string>("General.GamingAppsExcludeTitle", "");
    std::vector<std::string> result;
    std::istringstream iss(apps);
    std::string token;
    while (std::getline(iss, token, ',')) {
      if (!token.empty())
        result.push_back(token);
    }
    return result;
  }
  std::vector<std::string> GetGamingAppsTitle() const {
    std::string apps = Get<std::string>("General.GamingAppsTitle", "");
    std::vector<std::string> result;
    std::istringstream iss(apps);
    std::string token;
    while (std::getline(iss, token, ',')) {
      if (!token.empty())
        result.push_back(token);
    }
    return result;
  }
  void SetGamingApps(const std::vector<std::string> &apps) {
    std::ostringstream oss;
    for (size_t i = 0; i < apps.size(); ++i) {
      oss << apps[i];
      if (i + 1 < apps.size())
        oss << ",";
    }
    Set<std::string>("General.GamingApps", oss.str());
  }

  // Debug settings
  bool debug() const { return Get<bool>("Debug.Debug", true); }
  bool GetVerboseKeyLogging() const {
    return Get<bool>("Debug.VerboseKeyLogging", false);
  }
  bool GetVerboseWindowLogging() const {
    return Get<bool>("Debug.VerboseWindowLogging", false);
  }
  bool GetVerboseConditionLogging() const {
    return Get<bool>("Debug.VerboseConditionLogging", false);
  }

  // Brightness settings
  double GetDefaultBrightness() const {
    return Get<double>("General.DefaultBrightness", DEFAULT_BRIGHTNESS);
  }
  double GetStartupBrightness() const {
    return Get<double>("General.StartupBrightness", STARTUP_BRIGHTNESS);
  }
  int GetStartupGamma() const {
    return Get<int>("General.StartupGamma", STARTUP_GAMMA);
  }
  double GetBrightnessAmount() const {
    return Get<double>("General.BrightnessAmount", DEFAULT_BRIGHTNESS_AMOUNT);
  }
  int GetGammaAmount() const {
    return Get<int>("General.GammaAmount", DEFAULT_GAMMA_AMOUNT);
  }
  std::vector<std::string> GetConfigs() const {
    std::vector<std::string> configs;
    for (const auto &[key, val] : settings) {
      configs.push_back(key + "=" + val);
    }
    return configs;
  }

private:
  std::unordered_map<std::string, std::string> settings;
  std::unordered_map<std::string,
                     std::vector<std::function<void(std::string, std::string)>>>
      watchers;

  // File watching members
  std::atomic<bool> watching{false};
  std::thread watchingThread;

  template <typename T> static T Convert(const std::string &val) {
    std::istringstream iss(val);
    T result;
    iss >> result;
    return result;
  }

  // Get last modified time of the config file
  std::filesystem::file_time_type
  GetLastModified(const std::string &filepath) const {
    try {
      if (std::filesystem::exists(filepath)) {
        return std::filesystem::last_write_time(filepath);
      }
    } catch (const std::filesystem::filesystem_error &e) {
      // Handle error as needed
    }
    return std::filesystem::file_time_type::min();
  }
};

// Template specializations for Configs
template <> inline bool Configs::Convert<bool>(const std::string &val) {
  return val == "true" || val == "1" || val == "yes";
}

template <> inline int Configs::Convert<int>(const std::string &val) {
  return std::stoi(val);
}

template <> inline float Configs::Convert<float>(const std::string &val) {
  return std::stof(val);
}

// Make these functions inline to avoid multiple definition errors
inline void BackupConfig(const std::string &path = "havel.cfg") {
  std::string configPath = ConfigPaths::GetConfigPath(path);
  namespace fs = std::filesystem;
  try {
    fs::path backupPath = configPath + ".bak";
    if (fs::exists(configPath)) {
      fs::copy_file(configPath, backupPath,
                    fs::copy_options::overwrite_existing);
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
      fs::copy_file(backupPath, configPath,
                    fs::copy_options::overwrite_existing);
    }
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Config restore failed: " << e.what() << "\n";
  }
}

} // namespace havel

// Global extern variable for easy access to config
extern havel::Configs &g_Configs;

namespace havel {
// Inline function to access the global config instance
inline Configs &Conf() { return Configs::Get(); }
} // namespace havel