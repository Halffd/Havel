#include "qt.hpp"
#include "gui/HavelApp.hpp"
#include <iostream>
#include <string>
#include "core/ConfigManager.hpp"
#include "utils/Logger.hpp"
using namespace havel;

int main(int argc, char* argv[]) {
    // Initialize config first
    try {
        auto& config = Configs::Get();
        config.EnsureConfigFile();
        config.Load();
    } catch (const std::exception& e) {
        std::cerr << "Critical: Failed to initialize config: " << e.what() << std::endl;
        return 1;
    }

    QApplication app(argc, argv);
    app.setApplicationName("Havel");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Havel");
    app.setQuitOnLastWindowClosed(false); // Keep running in tray

    // Parse arguments
    bool isStartup = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--startup") {
            isStartup = true;
            break;
        }
    }

    try {
        HavelApp havelApp(isStartup);
        
        if (!havelApp.isInitialized()) {
            std::cerr << "Failed to initialize HavelApp" << std::endl;
            return 1;
        }
        
        info("Havel started successfully - running in system tray");
        return app.exec();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        error(std::string("Fatal error: ") + e.what());
        return 1;
    }
}