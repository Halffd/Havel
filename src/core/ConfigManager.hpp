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
#include "IO.hpp"
#include "../window/WindowManager.hpp"
#include "../common/types.hpp"
#include "../process/Launcher.hpp"
namespace havel {
// Path handling helper functions
namespace ConfigPaths {
    // Config base directory
    static const std::string CONFIG_DIR = "config/";
    
    // Config file paths
    static const std::string MAIN_CONFIG = CONFIG_DIR + "main.cfg";
    // Default config values (now in Configs class)

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
    // Default config values
    static inline const std::vector<std::string> DEFAULT_GAMING_APPS = {
        "steam_app_default", "retroarch", "ryujinx", "pcsx2", "dolphin-emu", "rpcs3", "cemu", "yuzu", "duckstation", "ppsspp", "xemu", "wine", "lutris", "heroic", "gamescope", "games", "minecraft", "nierautomata",
        
        // Minecraft Java Edition versions (complete list)
        // Pre-classic
        "rd-132211", "rd-132328", "rd-20090515", "rd-161348",
        
        // Classic versions
        "c0.0.11a", "c0.0.12a", "c0.0.13a", "c0.0.13a_03", "c0.0.14a", "c0.0.14a_08", "c0.0.15a", "c0.0.16a", "c0.0.16a_02",
        "c0.0.17a", "c0.0.18a", "c0.0.18a_02", "c0.0.19a", "c0.0.19a_06", "c0.0.20a", "c0.0.20a_01", "c0.0.21a", "c0.0.22a",
        "c0.0.23a", "c0.24_SURVIVAL_TEST", "c0.25_SURVIVAL_TEST", "c0.26_SURVIVAL_TEST", "c0.27_SURVIVAL_TEST", "c0.28",
        "c0.29_SURVIVAL_TEST", "c0.30_SURVIVAL_TEST", "c0.0.23a_01",
        
        // Indev versions
        "in-20091223-1", "in-20091223-2", "in-20100104", "in-20100106", "in-20100107", "in-20100109", "in-20100111",
        "in-20100113", "in-20100114", "in-20100115", "in-20100122", "in-20100124", "in-20100125", "in-20100128",
        "in-20100129", "in-20100130", "in-20100131", "in-20100201-1", "in-20100201-2", "in-20100202", "in-20100203",
        "in-20100204", "in-20100206", "in-20100208", "in-20100212", "in-20100219", "in-20100223",
        
        // Infdev versions  
        "inf-20100227", "inf-20100313", "inf-20100320", "inf-20100325", "inf-20100327", "inf-20100330", "inf-20100413",
        "inf-20100415", "inf-20100420", "inf-20100607", "inf-20100608", "inf-20100611", "inf-20100615", "inf-20100616",
        "inf-20100617-1", "inf-20100617-2", "inf-20100618", "inf-20100624", "inf-20100625-1", "inf-20100625-2",
        "inf-20100627", "inf-20100629", "inf-20100630",
        
        // Alpha versions
        "a1.0.0", "a1.0.1", "a1.0.1_01", "a1.0.2", "a1.0.2_01", "a1.0.2_02", "a1.0.3", "a1.0.4", "a1.0.5",
        "a1.0.6", "a1.0.6_01", "a1.0.6_02", "a1.0.6_03", "a1.0.7", "a1.0.8", "a1.0.8_01", "a1.0.9", "a1.0.10",
        "a1.0.11", "a1.0.12", "a1.0.13", "a1.0.13_01", "a1.0.14", "a1.0.15", "a1.0.16", "a1.0.16_01", "a1.0.16_02",
        "a1.0.17", "a1.0.17_02", "a1.0.17_04", "a1.1.0", "a1.1.1", "a1.1.2", "a1.1.2_01", "a1.2.0", "a1.2.0_01",
        "a1.2.0_02", "a1.2.1", "a1.2.1_01", "a1.2.2", "a1.2.3", "a1.2.3_01", "a1.2.3_02", "a1.2.3_04", "a1.2.4_01",
        "a1.2.5", "a1.2.6",
        
        // Beta versions
        "b1.0", "b1.0_01", "b1.0.2", "b1.1", "b1.1_01", "b1.1_02", "b1.2", "b1.2_01", "b1.2_02", "b1.3", "b1.3_01",
        "b1.4", "b1.4_01", "b1.5", "b1.5_01", "b1.6", "b1.6.1", "b1.6.2", "b1.6.3", "b1.6.4", "b1.6.5", "b1.6.6",
        "b1.7", "b1.7.2", "b1.7.3", "b1.8", "b1.8.1", "b1.9-pre1", "b1.9-pre2", "b1.9-pre3", "b1.9-pre4", "b1.9-pre5",
        "b1.9-pre6",
        
        // Release versions (1.0 - 1.21.4)
        "1.0.0", "1.1", "1.2.1", "1.2.2", "1.2.3", "1.2.4", "1.2.5", "1.3.1", "1.3.2", "1.4.2", "1.4.4", "1.4.5",
        "1.4.6", "1.4.7", "1.5", "1.5.1", "1.5.2", "1.6.1", "1.6.2", "1.6.4", "1.7.2", "1.7.4", "1.7.5", "1.7.6",
        "1.7.7", "1.7.8", "1.7.9", "1.7.10", "1.8", "1.8.1", "1.8.2", "1.8.3", "1.8.4", "1.8.5", "1.8.6", "1.8.7",
        "1.8.8", "1.8.9", "1.9", "1.9.1", "1.9.2", "1.9.3", "1.9.4", "1.10", "1.10.1", "1.10.2", "1.11", "1.11.1",
        "1.11.2", "1.12", "1.12.1", "1.12.2", "1.13", "1.13.1", "1.13.2", "1.14", "1.14.1", "1.14.2", "1.14.3",
        "1.14.4", "1.15", "1.15.1", "1.15.2", "1.16", "1.16.1", "1.16.2", "1.16.3", "1.16.4", "1.16.5", "1.17",
        "1.17.1", "1.18", "1.18.1", "1.18.2", "1.19", "1.19.1", "1.19.2", "1.19.3", "1.19.4", "1.20", "1.20.1",
        "1.20.2", "1.20.3", "1.20.4", "1.20.5", "1.20.6", "1.21", "1.21.1", "1.21.2", "1.21.3", "1.21.4", "1.21.5"
        
        // Latest snapshots (as of 2024)
        "24w44a", "24w45a", "24w46a", "1.21.4-pre1", "1.21.4-pre2", "1.21.4-rc1",
        
        // Common modded versions
        "1.7.10-Forge", "1.12.2-Forge", "1.16.5-Forge", "1.18.2-Forge", "1.19.2-Forge", "1.20.1-Forge",
        "1.7.10-OptiFine", "1.12.2-OptiFine", "1.16.5-OptiFine", "1.18.2-OptiFine", "1.19.2-OptiFine", "1.20.1-OptiFine",
        "1.12.2-Fabric", "1.16.5-Fabric", "1.18.2-Fabric", "1.19.2-Fabric", "1.20.1-Fabric", "1.21-Fabric",
        
        // Popular modpack versions
        "1.7.10-FTB", "1.12.2-FTB", "1.16.5-FTB", "1.18.2-FTB", "1.19.2-FTB", "1.20.1-FTB",
        "1.7.10-Tekkit", "1.12.2-Tekkit", "1.16.5-Tekkit", "1.18.2-Tekkit", "1.19.2-Tekkit", "1.20.1-Tekkit",
        "1.12.2-SkyFactory", "1.16.5-SkyFactory", "1.18.2-SkyFactory", "1.19.2-SkyFactory", "1.20.1-SkyFactory",
        
        // Bedrock Edition (if you want to include them)
        "bedrock-1.21.51", "bedrock-1.21.50", "bedrock-1.21.44", "bedrock-1.21.43", "bedrock-1.21.41",
        "bedrock-1.21.40", "bedrock-1.21.31", "bedrock-1.21.30", "bedrock-1.21.23", "bedrock-1.21.22",
        "bedrock-1.21.21", "bedrock-1.21.20", "bedrock-1.21.2", "bedrock-1.21.1", "bedrock-1.21.0"
    };
    static constexpr double DEFAULT_BRIGHTNESS = 0.85;
    static constexpr double STARTUP_BRIGHTNESS = 0.3;
    static constexpr int STARTUP_GAMMA = 7500;
    static constexpr double DEFAULT_BRIGHTNESS_AMOUNT = 0.02;
    static constexpr int DEFAULT_GAMMA_AMOUNT = 200;
    static inline const std::string GAMING_APPS_KEY = "General.GamingApps";

public:
    static Configs& Get() {
        static Configs instance;
        return instance;
    }

    void EnsureConfigFile(const std::string& filename = "main.cfg") {
        std::string configPath = ConfigPaths::GetConfigPath(filename);
        ConfigPaths::EnsureConfigDir();
        namespace fs = std::filesystem;
        if (!fs::exists(configPath)) {
            try {
                std::ofstream file(configPath);
                if (!file.is_open()) throw std::runtime_error("Could not create config file: " + configPath);
                // Write sensible defaults
                file << "[Debug]" << std::endl;
                file << "VerboseKeyLogging=false" << std::endl;
                file << "VerboseWindowLogging=false" << std::endl;
                file << "VerboseConditionLogging=false" << std::endl;
                file << "[General]" << std::endl;
                file << "GamingApps=";
                for (size_t i = 0; i < Configs::DEFAULT_GAMING_APPS.size(); ++i) {
                    file << Configs::DEFAULT_GAMING_APPS[i];
                    if (i + 1 < Configs::DEFAULT_GAMING_APPS.size()) file << ",";
                }
                file << std::endl;
                file << "DefaultBrightness=" << Configs::DEFAULT_BRIGHTNESS << std::endl;
                file << "StartupBrightness=" << Configs::STARTUP_BRIGHTNESS << std::endl;
                file << "StartupGamma=" << Configs::STARTUP_GAMMA << std::endl;
                file << "BrightnessAmount=" << Configs::DEFAULT_BRIGHTNESS_AMOUNT << std::endl;
                file << "GammaAmount=" << Configs::DEFAULT_GAMMA_AMOUNT << std::endl;
                file.close();
            } catch (const std::exception& e) {
                std::cerr << "Failed to create default config: " << e.what() << std::endl;
            }
        }
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

    // Helpers for gaming apps (comma-separated string)
    std::vector<std::string> GetGamingApps() const {
        std::string apps = Get<std::string>("General.GamingApps", "");
        std::vector<std::string> result;
        std::istringstream iss(apps);
        std::string token;
        while (std::getline(iss, token, ',')) {
            if (!token.empty()) result.push_back(token);
        }
        return result;
    }
    void SetGamingApps(const std::vector<std::string>& apps) {
        std::ostringstream oss;
        for (size_t i = 0; i < apps.size(); ++i) {
            oss << apps[i];
            if (i + 1 < apps.size()) oss << ",";
        }
        Set<std::string>("General.GamingApps", oss.str());
    }
    
    // Helpers for debug and brightness settings
    bool GetVerboseKeyLogging() const { return Get<bool>("Debug.VerboseKeyLogging", false); }
    bool GetVerboseWindowLogging() const { return Get<bool>("Debug.VerboseWindowLogging", false); }
    bool GetVerboseConditionLogging() const { return Get<bool>("Debug.VerboseConditionLogging", false); }
    double GetDefaultBrightness() const { return Get<double>("General.DefaultBrightness", DEFAULT_BRIGHTNESS); }
    double GetStartupBrightness() const { return Get<double>("General.StartupBrightness", STARTUP_BRIGHTNESS); }
    int GetStartupGamma() const { return Get<int>("General.StartupGamma", STARTUP_GAMMA); }
    double GetBrightnessAmount() const { return Get<double>("General.BrightnessAmount", DEFAULT_BRIGHTNESS_AMOUNT); }
    int GetGammaAmount() const { return Get<int>("General.GammaAmount", DEFAULT_GAMMA_AMOUNT); }

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
                    LaunchParams params;
                    params.method = Method::Async;
                    Launcher::run(parts[1], params);
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