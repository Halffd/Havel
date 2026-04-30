#include "ConfigManager.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

namespace havel {
namespace ConfigPaths {
// Get config directory with fallback to $HOME/.config/havel/
std::string GetDefaultConfigDir() {
#ifdef HAVE_QT_EXTENSION
  // Try Qt standard location first
  QString qtPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!qtPath.isEmpty()) {
    return qtPath.toStdString() + "/havel/";
  }
#endif

  // Fallback to $HOME/.config/havel/
  const char *home = std::getenv("HOME");
  if (home) {
    return std::string(home) + "/.config/havel/";
  }

  // Last resort: current directory
  return "./havel/";
}

// Lazy initialization to avoid Qt calls during static initialization
static std::string& GetConfigDirStatic() {
  static std::string CONFIG_DIR = GetDefaultConfigDir();
  return CONFIG_DIR;
}

static std::string& GetMainConfigStatic() {
  static std::string MAIN_CONFIG = GetConfigDirStatic() + "havel.cfg";
  return MAIN_CONFIG;
}

static std::string& GetHotkeysDirStatic() {
  static std::string HOTKEYS_DIR = GetConfigDirStatic() + "hotkeys/";
  return HOTKEYS_DIR;
}

void SetConfigPath(const std::string &path, const std::string &basename) {
  GetConfigDirStatic() = path;
  GetMainConfigStatic() = path + basename;
  GetHotkeysDirStatic() = path + "hotkeys/";
}

std::string GetConfigPath() { return GetConfigDirStatic(); }

std::string GetConfigPath(const std::string &filename) {
  if (filename.find('/') != std::string::npos) {
    return filename;
  }
  return GetConfigDirStatic() + filename;
}

void EnsureConfigDir() {
  namespace fs = std::filesystem;
  try {
    if (GetConfigDirStatic().empty()) {
      havel::warning("Config directory path is empty, using fallback");
      GetConfigDirStatic() = "./havel/";
      GetMainConfigStatic() = GetConfigDirStatic() + "havel.cfg";
      GetHotkeysDirStatic() = GetConfigDirStatic() + "hotkeys/";
    }

    if (!fs::exists(GetConfigDirStatic())) {
      fs::create_directories(GetConfigDirStatic());
    }
    if (!fs::exists(GetHotkeysDirStatic())) {
      fs::create_directories(GetHotkeysDirStatic());
    }
} catch (const fs::filesystem_error &e) {
    havel::error("Failed to create config directories: {} (Directory: {} {} Config: {})",
                 e.what(), GetConfigDirStatic(), GetHotkeysDirStatic(), GetMainConfigStatic());
}
}
} // namespace ConfigPaths

// Configs class implementation
havel::Configs &havel::Configs::Get() {
  static Configs instance;
  return instance;
}

havel::Configs::~Configs() {
  StopFileWatching();
  // Don't save during destruction - static strings may already be destroyed
  // causing use-after-free. Config should be saved explicitly via config.save()
  // if (!path.empty()) {
  //   ForceSave(); // Ensure pending saves are completed
  // }
}

std::filesystem::file_time_type
havel::Configs::GetLastModified(const std::string &filepath) const {
  try {
    if (std::filesystem::exists(filepath)) {
      return std::filesystem::last_write_time(filepath);
    }
  } catch (...) {
  }
  return std::filesystem::file_time_type::min();
}

void havel::Configs::Reload() {
    if (path.empty())
        return;
    try {
        std::ifstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Could not open config file: " + path);

        // Parse into temporary ConfigObject (TOML format)
        ConfigObject newConfig;
        std::string line, currentSection;

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;
            line = trim(line);
            if (line.empty()) continue;

            // Section header
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                // Handle [[array_of_tables]]
                if (currentSection.size() >= 2 && currentSection.front() == '[' && currentSection.back() == ']') {
                    currentSection = currentSection.substr(1, currentSection.size() - 2);
                }
                continue;
            }

            size_t delim = line.find('=');
            if (delim != std::string::npos) {
                std::string key = trim(line.substr(0, delim));
                std::string value = trim(line.substr(delim + 1));

                // Strip surrounding quotes from string values (TOML)
                if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                          (value.front() == '\'' && value.back() == '\''))) {
                    value = value.substr(1, value.size() - 2);
                }

                if (!currentSection.empty()) {
                    newConfig.set(currentSection + "." + key, value);
                } else {
                    newConfig.set(key, value);
                }
            }
        }

        // Notify watchers of changed values
        for (const auto &key : newConfig.keys()) {
            std::string oldValue = config.getString(key);
            std::string newValue = newConfig.getString(key);
            if (oldValue != newValue) {
                for (auto &[watchKey, callback] : watchers) {
                    if (watchKey == key || watchKey.empty()) {
                        try {
                            callback(newValue);
                        } catch (...) {
                        }
                    }
                }
            }
        }

        // Replace config
        config = newConfig;
    } catch (const std::exception &e) {
        havel::error("Config reload failed: {}", e.what());
    }
}

void havel::Configs::Load(const std::string &filename) {
    path = ConfigPaths::GetConfigPath(filename);

    std::ifstream file(path);

    std::string line, currentSection;

    if (!file.is_open()) {
        havel::error("Could not open config file: {}", path);
        return;
    }

    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        // Trim whitespace
        line = trim(line);
        if (line.empty()) continue;

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            // Handle [[array_of_tables]]
            if (currentSection.size() >= 2 && currentSection.front() == '[' && currentSection.back() == ']') {
                currentSection = currentSection.substr(1, currentSection.size() - 2);
            }
            continue;
        }

        // Key=value pair
        size_t delim = line.find('=');
        if (delim != std::string::npos) {
            std::string key = trim(line.substr(0, delim));
            std::string value = trim(line.substr(delim + 1));

            // Strip surrounding quotes from string values (TOML)
            if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                      (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }

            if (!currentSection.empty()) {
                config.set(currentSection + "." + key, value);
            } else {
                config.set(key, value);
            }
        }
    }
}

void havel::Configs::Save(const std::string &filename) {
    // Don't save if no config path (pure mode)
    if (path.empty() && filename.empty()) {
        return;
    }

    std::string savePath =
        filename.empty() ? path : ConfigPaths::GetConfigPath(filename);
    ConfigPaths::EnsureConfigDir();
    try {
        std::ofstream file(savePath);
        if (!file.is_open())
            throw std::runtime_error("Could not save config file: " + savePath);

        // Group keys by section for TOML format
        std::map<std::string, std::map<std::string, std::string>> sections;
        std::map<std::string, std::string> rootKeys;

        for (const auto &[key, value] : config.values()) {
            size_t dotPos = key.find('.');
            if (dotPos != std::string::npos) {
                std::string sec = key.substr(0, dotPos);
                std::string subKey = key.substr(dotPos + 1);
                sections[sec][subKey] = value;
            } else {
                rootKeys[key] = value;
            }
        }

        // Helper to write a TOML value (quote strings)
        auto writeValue = [](std::ofstream &f, const std::string &value) {
            // If value is a known TOML bare type, write as-is
            if (value == "true" || value == "false") {
                f << value;
                return;
            }
            // Check if it's a number
            try {
                size_t pos;
                std::stod(value, &pos);
                if (pos == value.size()) {
                    f << value;
                    return;
                }
            } catch (...) {
            }
            // Check if already quoted
            if (value.size() >= 2 &&
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                f << value;
                return;
            }
            // Check if it's a TOML array or inline table
            if (!value.empty() && (value.front() == '[' || value.front() == '{')) {
                f << value;
                return;
            }
            // Default: quote as string
            f << "\"" << value << "\"";
        };

        // Write root-level keys
        for (const auto &[key, value] : rootKeys) {
            file << key << " = ";
            writeValue(file, value);
            file << "\n";
        }

        // Write each section
        for (const auto &[sec, keys] : sections) {
            if (!rootKeys.empty() || &sec != &sections.begin()->first) {
                file << "\n";
            }
            file << "[" << sec << "]\n";
            for (const auto &[key, value] : keys) {
                file << key << " = ";
                writeValue(file, value);
                file << "\n";
            }
        }
    } catch (const std::exception &e) {
        havel::error("Config save failed: {}", e.what());
    }
}

void havel::Configs::RequestSave() {
  // Set pending flag
  savePending = true;

  // Start or reuse save thread
  std::lock_guard<std::mutex> lock(saveMutex);
  if (!saveThread.joinable()) {
    saveThread = std::thread([this]() {
      while (savePending.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(SAVE_DELAY_MS));
        if (savePending.load()) {
          savePending = false;
          Save();
        }
      }
    });
  }
}

void havel::Configs::ForceSave() {
  // Cancel pending save
  savePending = false;

  // Join save thread if running
  std::lock_guard<std::mutex> lock(saveMutex);
  if (saveThread.joinable()) {
    saveThread.join();
  }

  // Save immediately
  Save();
}

void havel::Configs::EnsureConfigFile(const std::string &filename) {
    path = ConfigPaths::GetConfigPath(filename);
    ConfigPaths::EnsureConfigDir();
    namespace fs = std::filesystem;
    if (!fs::exists(path)) {
        try {
            std::ofstream file(path);
            if (!file.is_open())
                throw std::runtime_error("Could not create config file: " + path);
            // Write sensible defaults in TOML format
            file << "[Debug]\n";
            file << "VerboseKeyLogging = false\n";
            file << "VerboseWindowLogging = false\n";
            file << "VerboseConditionLogging = false\n";
            file << "\n";
            file << "[Compiler]\n";
            file << "JIT = true\n";
            file << "\n";
            file << "[General]\n";
            file << "GamingApps = \"";
            for (size_t i = 0; i < Configs::DEFAULT_GAMING_APPS.size(); ++i) {
                file << Configs::DEFAULT_GAMING_APPS[i];
                if (i + 1 < Configs::DEFAULT_GAMING_APPS.size()) {
                    file << ",";
                }
            }
            file << "\"\n";
            file << "DefaultBrightness = " << Configs::DEFAULT_BRIGHTNESS << "\n";
            file << "StartupBrightness = " << Configs::STARTUP_BRIGHTNESS << "\n";
            file << "StartupGamma = " << Configs::STARTUP_GAMMA << "\n";
            file << "BrightnessAmount = " << Configs::DEFAULT_BRIGHTNESS_AMOUNT << "\n";
            file << "GammaAmount = " << Configs::DEFAULT_GAMMA_AMOUNT << "\n";
            file.close();
        } catch (const std::exception &e) {
            havel::error("Failed to create default config: {}", e.what());
        }
    }
}

std::string havel::Configs::getPath() { return path; }

void havel::Configs::SetPath(const std::string &newPath) {
  std::string dir = std::filesystem::path(newPath).parent_path();
  std::string basename = std::filesystem::path(newPath).filename();
  ConfigPaths::SetConfigPath(dir, basename);
}

void havel::Configs::StartFileWatching(const std::string &filename) {
  std::lock_guard<std::mutex> lock(watchingMutex);

  if (watchingThread.joinable()) {
    watchingThread.join();
  }

  EnsureConfigFile(filename);
  watching = true;

  // Use inotify for efficient file watching
  watchingThread = std::thread([this, filename]() {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd == -1) {
      havel::warning("inotify_init failed, falling back to polling");
      // Fallback to polling
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
        }
      }
      return;
    }

    int wd = inotify_add_watch(fd, path.c_str(), IN_MODIFY | IN_CLOSE_WRITE);
    if (wd == -1) {
        havel::error("inotify_add_watch failed");
      close(fd);
      return;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (watching.load()) {
      int ret = poll(&pfd, 1, 1000); // 1 second timeout
      if (ret > 0 && (pfd.revents & POLLIN)) {
        // Read events
        char buf[4096] __attribute__((aligned(8)));
        int len = read(fd, buf, sizeof(buf));
        if (len > 0) {
          // File was modified, reload
          Reload();
        }
      }
    }

    inotify_rm_watch(fd, wd);
    close(fd);
  });
}

void havel::Configs::StopFileWatching() {
  std::lock_guard<std::mutex> lock(watchingMutex);

  watching = false;
  if (watchingThread.joinable()) {
    watchingThread.join();
  }
}

void havel::Configs::Watch(const std::string &key, WatchCallback callback) {
  watchers[key] = callback;
}

void havel::Configs::Print() const {
  std::cout << "=== Config Values ===" << std::endl;
  for (const auto &[key, value] : config.values()) {
    std::cout << key << " = " << value << std::endl;
  }
  std::cout << "====================" << std::endl;
}

} // namespace havel
