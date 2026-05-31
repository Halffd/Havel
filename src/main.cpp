#include "core/init/HavelLauncher.hpp"
#include "utils/Logger.hpp"
#include "core/config/ConfigManager.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <X11/Xlib.h>

int main(int argc, char* argv[]) {
    auto main_start = std::chrono::steady_clock::now();
    if (argc >= 2 && std::string(argv[1]) == "lexer") {
        havel::init::HavelLauncher launcher;
        return launcher.run(argc, argv);
    }

	// Initialize config (always load - pure mode just skips GUI/hotkeys)
	{
		try {
			auto& config = havel::Configs::Get();
			config.EnsureConfigFile();
			config.Load();
		} catch (const std::exception& e) {
			havel::error("Critical: Failed to initialize config: {}", e.what());
			return 1;
		}
	}

    // X11 error handler
    XSetIOErrorHandler([](Display*) -> int {
        havel::error("X11 connection lost - exiting gracefully");
        exit(1);
        return 0;
    });

    auto pre_run = std::chrono::steady_clock::now();
    auto pre_ms = std::chrono::duration_cast<std::chrono::microseconds>(pre_run - main_start).count();
    havel::info("[startup] main-pre-run = {:.2f}ms", pre_ms / 1000.0);

    // Delegate everything to HavelLauncher
    havel::init::HavelLauncher launcher;
    return launcher.run(argc, argv);
}