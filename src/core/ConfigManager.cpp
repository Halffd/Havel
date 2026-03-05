#include "ConfigManager.hpp"
#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>

namespace havel {
namespace ConfigPaths {
    // Get config directory with fallback to $HOME/.config/havel/
    std::string GetDefaultConfigDir() {
        // Try Qt standard location first
        QString qtPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!qtPath.isEmpty()) {
            return qtPath.toStdString() + "/havel/";
        }

        // Fallback to $HOME/.config/havel/
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/.config/havel/";
        }

        // Last resort: current directory
        return "./havel/";
    }

    std::string CONFIG_DIR = GetDefaultConfigDir();
    std::string MAIN_CONFIG = CONFIG_DIR + "havel.cfg";
    std::string HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";

    void SetConfigPath(const std::string &path, const std::string &basename) {
        CONFIG_DIR = path;
        MAIN_CONFIG = CONFIG_DIR + basename;
        HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";
    }

    std::string GetConfigPath() {
        return CONFIG_DIR;
    }

    std::string GetConfigPath(const std::string &filename) {
        if (filename.find('/') != std::string::npos) {
            return filename;
        }
        return CONFIG_DIR + filename;
    }

    void EnsureConfigDir() {
        namespace fs = std::filesystem;
        try {
            if (CONFIG_DIR.empty()) {
                std::cerr << "Config directory path is empty, using fallback\n";
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
            std::cerr << "Failed to create config directories: " << e.what()
                      << "\nDirectory: " << CONFIG_DIR << " " << HOTKEYS_DIR
                      << " Config: " << MAIN_CONFIG << "\n";
        }
    }
}

// Configs class implementation
havel::Configs& havel::Configs::Get() {
    static Configs instance;
    return instance;
}

havel::Configs::~Configs() {
    StopFileWatching();
}

std::filesystem::file_time_type havel::Configs::GetLastModified(const std::string &filepath) const {
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
        std::string line;
        std::string section;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;
            if (line[0] == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            size_t delim = line.find('=');
            if (delim != std::string::npos) {
                std::string key = line.substr(0, delim);
                std::string value = line.substr(delim + 1);
                // Trim whitespace
                while (!key.empty() && std::isspace(key.back()))
                    key.pop_back();
                while (!value.empty() && std::isspace(value.front()))
                    value.erase(0, 1);
                std::string fullKey = section + "." + key;
                values[fullKey] = value;
                // Notify watchers
                for (auto &[watchKey, callback] : watchers) {
                    if (watchKey == fullKey || watchKey.empty()) {
                        try {
                            callback(value);
                        } catch (...) {
                        }
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Config reload failed: " << e.what() << std::endl;
    }
}

void havel::Configs::Load(const std::string &filename) {
    path = ConfigPaths::GetConfigPath(filename);

    std::ifstream file(path);

    std::string line, currentSection;
    int lineNumber = 0;

    if (!file.is_open()) {
        std::cerr << "Could not open config file: " << path << std::endl;
        return;
    }

    while (std::getline(file, line)) {
        lineNumber++;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        // Trim whitespace
        while (!line.empty() && std::isspace(line.front()))
            line.erase(0, 1);
        while (!line.empty() && std::isspace(line.back()))
            line.pop_back();

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        // Key=value pair
        size_t delim = line.find('=');
        if (delim != std::string::npos) {
            std::string key = line.substr(0, delim);
            std::string value = line.substr(delim + 1);

            // Trim
            while (!key.empty() && std::isspace(key.back()))
                key.pop_back();
            while (!value.empty() && std::isspace(value.front()))
                value.erase(0, 1);

            std::string fullKey = currentSection + "." + key;
            values[fullKey] = value;
        }
    }
}

void havel::Configs::Save(const std::string &filename) {
    std::string savePath = filename.empty() ? path : ConfigPaths::GetConfigPath(filename);
    ConfigPaths::EnsureConfigDir();
    try {
        std::ofstream file(savePath);
        if (!file.is_open())
            throw std::runtime_error("Could not save config file: " + savePath);

        // Group values by section
        std::map<std::string, std::map<std::string, std::string>> sections;
        for (const auto &[key, value] : values) {
            size_t delim = key.find('.');
            if (delim != std::string::npos) {
                sections[key.substr(0, delim)][key.substr(delim + 1)] = value;
            } else {
                sections["General"][key] = value;
            }
        }

        // Write sections
        for (const auto &[section, pairs] : sections) {
            file << "[" << section << "]" << std::endl;
            for (const auto &[key, value] : pairs) {
                file << key << "=" << value << std::endl;
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Config save failed: " << e.what() << std::endl;
    }
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

std::string havel::Configs::getPath() {
    return path;
}

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
    for (const auto &[key, value] : values) {
        std::cout << key << " = " << value << std::endl;
    }
    std::cout << "====================" << std::endl;
}

} // namespace ConfigPaths

// Global config variable
namespace havel {
Configs& g_Configs = Configs::Get();
} // namespace havel
