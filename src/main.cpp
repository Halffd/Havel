#include "core/init/HavelLauncher.hpp"
#include "host/module/BridgeSelection.hpp"
#include "host/ui/UIManager.hpp"
#include "utils/ExitHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StartupTiming.hpp"
#include "core/config/ConfigManager.hpp"
#include "havel_platform.h"
#include "havel-lang/compiler/core/SourceModuleCompilerHook.hpp"
#include <iostream>
#include <string>
#include <filesystem>

// The toolkit bridge installer lives in the core (src/host/module/bridges/
// ToolkitBridge.cpp), Qt-free. It loads the Qt toolkit plugin at runtime and
// installs the clipboard/gui.notify pipeline functions. The executable no
// longer links havel_gui at all; a Qt version skew degrades to a log line
// here instead of a binary that refuses to boot.
namespace havel::compiler {
void installToolkitBridge(PipelineOptions &options, const HostContext *ctx);
} // namespace havel::compiler

#if HAVEL_PLATFORM_LINUX && defined(HAVE_X11)
#include <X11/Xlib.h>
#endif

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // SDK host: install the parse+compile pipeline behind the VM's
    // module compiler hook before any script (or imported module) runs.
    havel::compiler::registerSourceModuleCompilerHook();
    // Select the toolkit bridge before any pipeline is built. It is the core's
    // Qt-free bridge installer (installs clipboard.get/set/clear,
    // io.getClipboard, gui.notify). Embedders that want no bridge (havel-wm,
    // the AOT runtime) never call setQtBridgeInstaller and stay Qt-free.
    havel::setQtBridgeInstaller(&havel::compiler::installToolkitBridge);
    // Load the Qt toolkit plugin eagerly so its in-process factories (UI/
    // screenshot/clipboard backends, pixel service, screen provider) are
    // registered before the host or the language host can ask for them —
    // the same ordering havel_gui's static initializer used to guarantee.
    // If the plugin cannot load (Qt version skew), this logs once and the
    // features degrade instead of the binary failing to boot.
    havel::host::UIManager::instance().ensureQtToolkit();
#if HAVEL_PLATFORM_LINUX && defined(HAVE_X11)
    XInitThreads();
#endif
    auto t0 = havel::startup_now();
    
    std::string selfHostedPath;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--self-hosted-path" && i + 1 < argc) {
            selfHostedPath = argv[++i];
        }
    }
    
    if (selfHostedPath.empty()) {
        try {
            auto exePath = fs::read_symlink("/proc/self/exe");
            selfHostedPath = (exePath.parent_path().parent_path() / "out").string();
        } catch (...) {
            selfHostedPath = "./out";
        }
    }
    
    if (argc >= 2 && std::string(argv[1]) == "lexer") {
        havel::init::HavelLauncher launcher;
        launcher.setSelfHostedConfig(selfHostedPath);
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
    launcher.setSelfHostedConfig(selfHostedPath);
    int res = launcher.run(argc, argv);
    havel::runExitCleanups();
    return res;
}
