#include "core/init/HavelLauncher.hpp"
#include "utils/Logger.hpp"
#include "core/ConfigManager.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "lexer") {
        havel::init::HavelLauncher launcher;
        return launcher.run(argc, argv);
    }

    // Initialize config
    try {
        auto& config = havel::Configs::Get();
        config.EnsureConfigFile();
        config.Load();
        havel::info("Config path: {}", config.getPath());
    } catch (const std::exception& e) {
        havel::error("Critical: Failed to initialize config: {}", e.what());
        return 1;
    }

    // X11 error handler
    XSetIOErrorHandler([](Display*) -> int {
        havel::error("X11 connection lost - exiting gracefully");
        exit(1);
        return 0;
    });

    // Delegate everything to HavelLauncher
    havel::init::HavelLauncher launcher;
    return launcher.run(argc, argv);
}