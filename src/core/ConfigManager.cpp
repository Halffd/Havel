#include "ConfigManager.hpp"
#include <string>
#include <cstdlib>

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
    std::string INPUT_CONFIG = CONFIG_DIR + "input.cfg";
    std::string HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";
}

namespace havel {
    // Define the global config variable
    Configs& g_Configs = havel::Configs::Get();

} // namespace havel
