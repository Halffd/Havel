/*
 * ConfigManager.hpp
 * 
 * Fixed template issues by:
 * 1. Moving the Configs class definition before the Mappings class
 * 2. Ensuring there are no duplicate template implementations
 * 3. Making the utility functions (BackupConfig, RestoreConfig) inline
 * 4. Making implementation in the header file directly to avoid linker errors
 */
#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <fstream>
#include <sstream>
#include <set>
#include <filesystem>
#include <vector>
#include <iostream>
#include "../common/types.hpp"

namespace havel {

// Forward declarations
class IO;
class WindowManager;

// Path handling helper functions
namespace ConfigPaths {
    // Config base directory
    static const std::string CONFIG_DIR = "config/";
    
    // Config file paths
    static const std::string MAIN_CONFIG = CONFIG_DIR + "main.cfg";
    static const std::string INPUT_CONFIG = CONFIG_DIR + "input.cfg";
    static const std::string HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";
    
    // Get path for a config file
    inline std::string GetConfigPath(const std::string& filename) {
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
            if (!fs::exists(CONFIG_DIR)) {
                fs::create_directories(CONFIG_DIR);
            }
            if (!fs::exists(HOTKEYS_DIR)) {
                fs::create_directories(HOTKEYS_DIR);
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Failed to create config directories: " << e.what() << "\n";
        }
    }
}

// Configs class definition
class Configs {
public:
    static Configs& Get() {
        static Configs instance;
        return instance;
    }

    void Load(const std::string& filename = "main.cfg") {
        std::string configPath = ConfigPaths::GetConfigPath(filename);
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open config file: " << configPath << std::endl;
            return;
        }
        
        std::string line, currentSection;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            if (line[0] == '[') {
                currentSection = line.substr(1, line.find(']')-1);
            }
            else {
                size_t delim = line.find('=');
                if (delim != std::string::npos) {
                    std::string key = currentSection + "." + line.substr(0, delim);
                    std::string value = line.substr(delim+1);
                    settings[key] = value;
                }
            }
        }
    }

    void Save(const std::string& filename = "main.cfg") {
        std::string configPath = ConfigPaths::GetConfigPath(filename);
        ConfigPaths::EnsureConfigDir();
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Error: Could not save config file: " << configPath << std::endl;
            return;
        }
        
        std::string currentSection;
        
        for (const auto& [key, value] : settings) {
            size_t dotPos = key.find('.');
            std::string section = key.substr(0, dotPos);
            std::string name = key.substr(dotPos+1);

            if (section != currentSection) {
                file << "[" << section << "]\n";
                currentSection = section;
            }
            
            file << name << "=" << value << "\n";
        }
    }

    template<typename T>
    T Get(const std::string& key, T defaultValue) const {
        auto it = settings.find(key);
        if (it == settings.end()) return defaultValue;
        return Convert<T>(it->second);
    }

    template<typename T>
    void Set(const std::string& key, T value) {
        std::ostringstream oss;
        oss << value;
        std::string oldValue = settings[key];
        settings[key] = oss.str();
        
        // Notify watchers
        if (watchers.find(key) != watchers.end()) {
            for (auto& watcher : watchers[key]) {
                watcher(oldValue, settings[key]);
            }
        }
    }

    template<typename T>
    void Watch(const std::string& key, std::function<void(T,T)> callback) {
        watchers[key].push_back([=](const std::string& oldVal, const std::string& newVal) {
            T oldT = Convert<T>(oldVal);
            T newT = Convert<T>(newVal);
            callback(oldT, newT);
        });
    }

    void Reload() {
        auto oldSettings = settings;
        settings.clear();
        Load();
        
        for(const auto& [key, newVal] : settings) {
            if(oldSettings[key] != newVal) {
                for(auto& watcher : watchers[key]) {
                    watcher(oldSettings[key], newVal);
                }
            }
        }
    }

    void Validate() const {
        const std::set<std::string> validKeys = {
            "Window.MoveSpeed", "Window.ResizeSpeed", 
            "Hotkeys.GlobalSuspend", "UI.Theme"
        };

        for(const auto& [key, val] : settings) {
            if(validKeys.find(key) == validKeys.end()) {
                std::cerr << "Warning: Unknown config key '" << key << "'\n";
            }
        }
    }

    template<typename T>
    T Get(const std::string& key, T defaultValue, T min, T max) const {
        T value = Get(key, defaultValue);
        if(value < min || value > max) {
            std::cerr << "Config value out of range: " << key 
                      << "=" << value << " (Valid: " 
                      << min << "-" << max << ")\n";
            return defaultValue;
        }
        return value;
    }

private:
    std::unordered_map<std::string, std::string> settings;
    std::unordered_map<std::string, std::vector<std::function<void(std::string, std::string)>>> watchers;

    template<typename T>
    static T Convert(const std::string& val) {
        std::istringstream iss(val);
        T result;
        iss >> result;
        return result;
    }
};

// Template specializations for Configs
template<>
inline std::string Configs::Get<std::string>(const std::string& key, std::string defaultValue) const {
    auto it = settings.find(key);
    return it != settings.end() ? it->second : defaultValue;
}

template<>
inline void Configs::Set<std::string>(const std::string& key, std::string value) {
    std::string oldValue = settings[key];
    settings[key] = value;
    
    // Notify watchers
    if (watchers.find(key) != watchers.end()) {
        for (auto& watcher : watchers[key]) {
            watcher(oldValue, settings[key]);
        }
    }
}

template<>
inline bool Configs::Convert<bool>(const std::string& val) {
    return val == "true" || val == "1" || val == "yes";
}

template<>
inline int Configs::Convert<int>(const std::string& val) {
    return std::stoi(val);
}

template<>
inline float Configs::Convert<float>(const std::string& val) {
    return std::stof(val);
}

// Now Mappings class can properly reference Configs
class Mappings {
public:
    Mappings(havel::IO& ioRef) : io(ioRef) {}

    static Mappings& Get() {
        static IO io; // Create a static IO instance
        static Mappings instance(io);
        return instance;
    }

    void Load(const std::string& filename = "input.cfg") {
        std::string configPath = ConfigPaths::GetConfigPath(filename);
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open input config file: " << configPath << std::endl;
            return;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            size_t delim = line.find('=');
            if (delim != std::string::npos) {
                std::string key = line.substr(0, delim);
                std::string value = line.substr(delim+1);
                hotkeys[key] = value;
            }
        }
    }

    void Save(const std::string& filename = "input.cfg") {
        std::string configPath = ConfigPaths::GetConfigPath(filename);
        ConfigPaths::EnsureConfigDir();
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Error: Could not save input config file: " << configPath << std::endl;
            return;
        }
        
        for (const auto& [key, value] : hotkeys) {
            file << key << "=" << value << "\n";
        }
    }

    void BindHotkeys(IO& io) {
        for (const auto& [keyCombo, command] : hotkeys) {
            if (!command.empty()) {
                try {
                    io.Hotkey(keyCombo, [this, command]() {
                        try {
                            ExecuteCommand(command);
                        } catch(const std::exception& e) {
                            std::cerr << "Error executing command: " << e.what() << "\n";
                        }
                    });
                } catch(const std::exception& e) {
                    std::cerr << "Error binding hotkey " << keyCombo << ": " << e.what() << "\n";
                }
            }
        }
        needsRebind = false;
    }

    void Add(const std::string& keyCombo, const std::string& command) {
        hotkeys[keyCombo] = command;
        needsRebind = true;
    }

    void Remove(const std::string& keyCombo) {
        hotkeys.erase(keyCombo);
        needsRebind = true;
    }

    std::string GetCommand(const std::string& keyCombo) const {
        auto it = hotkeys.find(keyCombo);
        return (it != hotkeys.end()) ? it->second : "";
    }

    void Reload() {
        auto oldHotkeys = hotkeys;
        hotkeys.clear();
        Load();
        
        if(oldHotkeys != hotkeys) {
            needsRebind = true;
        }
    }

    bool CheckRebind() {
        return needsRebind;
    }

private:
    havel::IO& io;
    std::unordered_map<std::string, std::string> hotkeys;
    bool needsRebind = false;

    void ExecuteCommand(const std::string& command) {
        if(command.empty()) return;
        
        if(command[0] == '@') {
            // Split command into parts
            std::istringstream iss(command);
            std::string token;
            std::vector<std::string> parts;
            while(std::getline(iss, token, ' ')) {
                if(!token.empty()) {
                    parts.push_back(token);
                }
            }
            
            if(parts.size() < 2) return;
            
            // Handle different command types
            if(parts[0] == "@run") {
                if(parts.size() >= 2) {
                    havel::WindowManager::Run(parts[1], havel::ProcessMethodType::ForkProcess,
                                        "", "", 0);
                }
            }
            else if(parts[0] == "@send") {
                if(parts.size() >= 2) {
                    std::string sendText = command.substr(command.find(parts[1]));
                    io.Send(sendText);
                }
            }
            else if(parts[0] == "@config") {
                if(parts.size() >= 3) {
                    std::string var = Configs::Get().Get<std::string>(parts[1], "");
                    if(parts[2] == "toggle") {
                        bool current = Configs::Get().Get<bool>(parts[1], false);
                        Configs::Get().Set<bool>(parts[1], !current);
                    }
                }
            }
        } else {
            // Default to sending keystrokes
            io.Send(command);
        }
    }

    void SafeExecute(const std::string& command) noexcept {
        try {
            ExecuteCommand(command);
        } catch(const std::exception& e) {
            std::cerr << "Error executing command: " << e.what() << "\n";
        } catch(...) {
            std::cerr << "Unknown error executing command\n";
        }
    }
};

// Make these functions inline to avoid multiple definition errors
inline void BackupConfig(const std::string& path = "main.cfg") {
    std::string configPath = ConfigPaths::GetConfigPath(path);
    namespace fs = std::filesystem;
    try {
        fs::path backupPath = configPath + ".bak";
        if(fs::exists(configPath)) {
            fs::copy_file(configPath, backupPath, fs::copy_options::overwrite_existing);
        }
    } catch(const fs::filesystem_error& e) {
        std::cerr << "Config backup failed: " << e.what() << "\n";
    }
}

inline void RestoreConfig(const std::string& path = "main.cfg") {
    std::string configPath = ConfigPaths::GetConfigPath(path);
    namespace fs = std::filesystem;
    try {
        fs::path backupPath = configPath + ".bak";
        if(fs::exists(backupPath)) {
            fs::copy_file(backupPath, configPath, fs::copy_options::overwrite_existing);
        }
    } catch(const fs::filesystem_error& e) {
        std::cerr << "Config restore failed: " << e.what() << "\n";
    }
}

} // namespace havel 