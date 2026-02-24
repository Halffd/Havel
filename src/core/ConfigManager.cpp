#include "ConfigManager.hpp"
#include <string>

namespace ConfigPaths {
    std::string CONFIG_DIR = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString() + "/havel/";
    std::string MAIN_CONFIG = CONFIG_DIR + "havel.cfg";
    std::string INPUT_CONFIG = CONFIG_DIR + "input.cfg";
    std::string HOTKEYS_DIR = CONFIG_DIR + "hotkeys/";
}
namespace havel {
    // Define the global config variable
    Configs& g_Configs = havel::Configs::Get();

} // namespace havel
