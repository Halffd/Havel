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
  // Try Qt standard location first
  QString qtPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!qtPath.isEmpty()) {
    return qtPath.toStdString() + "/havel/";
  }

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
      std::cerr << "Config directory path is empty, using fallback\n";
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
    std::cerr << "Failed to create config directories: " << e.what()
              << "\nDirectory: " << GetConfigDirStatic() << " " << GetHotkeysDirStatic()
              << " Config: " << GetMainConfigStatic() << "\n";
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

    // Parse into temporary ConfigObject
    ConfigObject newConfig;
    std::string line, currentSection;

    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#' || line[0] == ';')
        continue;
      if (line[0] == '[' && line.back() == ']') {
        currentSection = trim(line.substr(1, line.size() - 2));
        continue;
      }
      size_t delim = line.find('=');
      if (delim != std::string::npos) {
        std::string key = trim(line.substr(0, delim));
        std::string value = trim(line.substr(delim + 1));

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
    std::cerr << "Config reload failed: " << e.what() << std::endl;
  }
}

void havel::Configs::Load(const std::string &filename) {
  path = ConfigPaths::GetConfigPath(filename);

  std::ifstream file(path);

  std::string line, currentSection;

  if (!file.is_open()) {
    std::cerr << "Could not open config file: " << path << std::endl;
    return;
  }

  while (std::getline(file, line)) {
    // Skip empty lines and comments
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    // Trim whitespace
    line = trim(line);

    // Section header
    if (line[0] == '[' && line.back() == ']') {
      currentSection = line.substr(1, line.size() - 2);
      continue;
    }

    // Key=value pair
    size_t delim = line.find('=');
    if (delim != std::string::npos) {
      std::string key = trim(line.substr(0, delim));
      std::string value = trim(line.substr(delim + 1));

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

    // Use ConfigObject's toString method
    file << config.toString();
  } catch (const std::exception &e) {
    std::cerr << "Config save failed: " << e.what() << std::endl;
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
      file << "DefaultBrightness=" << Configs::DEFAULT_BRIGHTNESS << std::endl;
      file << "StartupBrightness=" << Configs::STARTUP_BRIGHTNESS << std::endl;
      file << "StartupGamma=" << Configs::STARTUP_GAMMA << std::endl;
      file << "BrightnessAmount=" << Configs::DEFAULT_BRIGHTNESS_AMOUNT
           << std::endl;
      file << "GammaAmount=" << Configs::DEFAULT_GAMMA_AMOUNT << std::endl;
      file.close();
    } catch (const std::exception &e) {
      std::cerr << "Failed to create default config: " << e.what() << std::endl;
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
      std::cerr << "inotify_init failed, falling back to polling\n";
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
      std::cerr << "inotify_add_watch failed\n";
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
