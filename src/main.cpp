#include "core/init/HavelLauncher.hpp"
#include "utils/ExitHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StartupTiming.hpp"
#include "core/config/ConfigManager.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <X11/Xlib.h>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    auto t0 = havel::startup_now();
    
    // Determine self-hosted modules path (default: out/ relative to executable)
    std::string selfHostedPath;
    bool useCppModules = false;
    
    // Parse --cpp flag early
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cpp") {
            useCppModules = true;
        } else if (arg == "--self-hosted-path" && i + 1 < argc) {
            selfHostedPath = argv[++i];
        }
    }
    
    // Default: use self-hosted modules from out/ directory
    if (!useCppModules && selfHostedPath.empty()) {
        // Try to determine executable path
        try {
            auto exePath = fs::read_symlink("/proc/self/exe");
            selfHostedPath = (exePath.parent_path() / "out").string();
        } catch (...) {
            // Fallback to current directory
            selfHostedPath = "./out";
        }
    }
    
    if (argc >= 2 && std::string(argv[1]) == "lexer") {
        havel::init::HavelLauncher launcher;
        launcher.setSelfHostedConfig(useCppModules, selfHostedPath);
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
    launcher.setSelfHostedConfig(useCppModules, selfHostedPath);
    return launcher.run(argc, argv);
}
