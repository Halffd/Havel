#include "core/init/HavelLauncher.hpp"
#include "utils/ExitHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StartupTiming.hpp"
#include "core/config/ConfigManager.hpp"
#include <iostream>
#include <string>
#include <X11/Xlib.h>

int main(int argc, char* argv[]) {
    auto t0 = havel::startup_now();
    if (argc >= 2 && std::string(argv[1]) == "lexer") {
        havel::init::HavelLauncher launcher;
        return launcher.run(argc, argv);
    }

    {
        try {
            auto& config = havel::Configs::Get();
            config.EnsureConfigFile();
            havel::startup_timing_report("config-ensure", t0);
            auto t1 = havel::startup_now();
            config.Load();
            havel::startup_timing_report("config-load", t1);
        } catch (const std::exception& e) {
            havel::error("Critical: Failed to initialize config: {}", e.what());
            return 1;
        }
    }

    XSetIOErrorHandler([](Display*) -> int {
        havel::error("X11 connection lost - exiting gracefully");
        havel::exit(havel::ExitReason::Forced, 1);
        return 0;
    });

    havel::startup_timing_report("main-pre-run", t0);

    havel::init::HavelLauncher launcher;
    return launcher.run(argc, argv);
}
