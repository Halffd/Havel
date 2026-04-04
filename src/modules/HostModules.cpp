// HostModules.cpp
// Register all host modules
// Simple, explicit registration

#include "havel-lang/runtime/ModuleLoader.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "../utils/Logger.hpp"
#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "../host/ServiceRegistry.hpp"
#include "../host/io/IOService.hpp"
#include "../host/hotkey/HotkeyService.hpp"
#include "../host/window/WindowService.hpp"
#include "../host/mode/ModeService.hpp"
#include "../host/process/ProcessService.hpp"
#include "../host/clipboard/ClipboardService.hpp"
#include "../host/audio/AudioService.hpp"
#include "../host/brightness/BrightnessService.hpp"
#include "../host/screenshot/ScreenshotService.hpp"
#include "../host/automation/PixelAutomationService.hpp"
#include "../host/automation/AutomationService.hpp"
#include "../host/chunker/TextChunkerService.hpp"
#include "../host/browser/BrowserService.hpp"
#include "../host/io/MapManagerService.hpp"
#include "../host/window/AltTabService.hpp"
#include "../host/async/AsyncService.hpp"
#include "../host/timer/TimerService.hpp"
#include "../host/media/MediaService.hpp"
#include "../host/filesystem/FileSystemService.hpp"
#include "../host/network/NetworkService.hpp"
#include "../host/app/AppService.hpp"

// Module registration functions (from .cpp files, no headers needed)
namespace havel::modules {
    void registerWindowQueryModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerBrightnessModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerAudioModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerScreenshotModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerClipboardModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerPixelModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerAutomationModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerLauncherModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerMediaModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerHelpModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerFileManagerModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerSystemModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerDetectorModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerGUIModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerAltTabModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerMapManagerModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerIOModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerInputModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerHotkeyModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerModeModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerBrowserModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerFFIModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerProcessModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerShellExecutorModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerRuntimeModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerConfigModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerConcurrencyModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerHTTPModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerTimerModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerAsyncModule(Environment&, std::shared_ptr<IHostAPI>);
    void registerAppModule(Environment&, std::shared_ptr<IHostAPI>);
}

namespace havel {

/**
 * Initialize service registry with all services
 * Called ONCE at application startup, before any modules are loaded.
 * All services MUST be registered here - modules will fail if services are missing.
 */
void initializeServiceRegistry(std::shared_ptr<IHostAPI> hostAPI) {
    if (!hostAPI) {
        throw std::runtime_error("initializeServiceRegistry: hostAPI is null");
    }

    auto& registry = host::ServiceRegistry::instance();

    // Register all services - explicit construction, no fallbacks
    if (!hostAPI->GetIO()) {
        throw std::runtime_error("initializeServiceRegistry: IO not available");
    }
    auto ioService = std::make_shared<host::IOService>(hostAPI->GetIO());
    registry.registerService<host::IOService>(ioService);

    // HotkeyManager is optional - only register HotkeyService if available
    if (hostAPI->GetHotkeyManager()) {
        auto hotkeyManager = hostAPI->GetHotkeyManager();
        auto hotkeyService = std::make_shared<host::HotkeyService>(
            std::shared_ptr<havel::HotkeyManager>(hotkeyManager, [](havel::HotkeyManager*){}));
        registry.registerService<host::HotkeyService>(hotkeyService);
    }

    // WindowManager is required for window operations
    if (!hostAPI->GetWindowManager()) {
        throw std::runtime_error("initializeServiceRegistry: WindowManager not available");
    }
    auto windowService = std::make_shared<host::WindowService>(hostAPI->GetWindowManager());
    registry.registerService<host::WindowService>(windowService);

    // ModeManager is optional - ModeService is created later in createHostBridgeDependencies()
    // when VM is available because it needs VM* for callback management
    if (hostAPI->GetModeManager()) {
        // ModeService will be created in createHostBridgeDependencies()
    }

    // ProcessManager is optional
    if (hostAPI->GetProcessManager()) {
        auto processService = std::make_shared<host::ProcessService>();
        registry.registerService<host::ProcessService>(processService);
    }

    // Clipboard service doesn't need constructor args
    auto clipboardService = std::make_shared<host::ClipboardService>();
    registry.registerService<host::ClipboardService>(clipboardService);

    // AudioManager is optional
    if (hostAPI->GetAudioManager()) {
        auto audioService = std::make_shared<host::AudioService>(hostAPI->GetAudioManager());
        registry.registerService<host::AudioService>(audioService);
    }

    // BrightnessManager is optional
    if (hostAPI->GetBrightnessManager()) {
        auto brightnessService = std::make_shared<host::BrightnessService>(hostAPI->GetBrightnessManager());
        registry.registerService<host::BrightnessService>(brightnessService);
    }
    
    // Screenshot service singleton (uses Qt directly)
    auto screenshotService = std::shared_ptr<host::ScreenshotService>(
        &host::ScreenshotService::getInstance(),
        [](host::ScreenshotService*) {});
    registry.registerService<host::ScreenshotService>(screenshotService);

    // PixelAutomation service doesn't need constructor args (uses Qt/OpenCV directly)
    auto pixelService = std::make_shared<host::PixelAutomationService>();
    registry.registerService<host::PixelAutomationService>(pixelService);

    // TextChunker service doesn't need constructor args (pure C++ logic)
    auto chunkerService = std::make_shared<host::TextChunkerService>();
    registry.registerService<host::TextChunkerService>(chunkerService);

    // Browser service doesn't need constructor args (uses HTTP/WebSocket)
    auto browserService = std::make_shared<host::BrowserService>();
    registry.registerService<host::BrowserService>(browserService);

    // MapManager service needs IO pointer
    auto mapManagerService = std::make_shared<host::MapManagerService>(hostAPI->GetIO() ? std::shared_ptr<IO>(hostAPI->GetIO(), [](IO*){}) : std::shared_ptr<IO>());
    registry.registerService<host::MapManagerService>(mapManagerService);

    // AltTab service doesn't need constructor args (uses Qt directly)
    auto altTabService = std::make_shared<host::AltTabService>();
    registry.registerService<host::AltTabService>(altTabService);

    // Automation service needs IO pointer
    auto automationService = std::make_shared<host::AutomationService>(hostAPI->GetIO() ? std::shared_ptr<IO>(hostAPI->GetIO(), [](IO*){}) : std::shared_ptr<IO>());
    registry.registerService<host::AutomationService>(automationService);

    // Async service doesn't need constructor args (pure C++ with std::thread)
    auto asyncService = std::make_shared<host::AsyncService>();
    registry.registerService<host::AsyncService>(asyncService);

    // File system service doesn't need constructor args (pure C++ with std::filesystem)
    auto fsService = std::make_shared<host::FileSystemService>();
    registry.registerService<host::FileSystemService>(fsService);

    info("ServiceRegistry initialized with {} services", registry.size());
}

/**
 * Create HostContext with injected dependencies
 *
 * @param hostAPI Host API for accessing managers
 * @return HostContext with all available services
 */
havel::HostContext createHostContext(std::shared_ptr<IHostAPI> hostAPI) {
    havel::HostContext ctx;

    if (!hostAPI) {
        return ctx;
    }

    // Core services - wrap raw pointers in shared_ptr without ownership (no deleter)
    ctx.io = hostAPI ? hostAPI->GetIO() : nullptr;

    // Optional services (nullptr if not available)
    ctx.hotkeyManager = hostAPI ? hostAPI->GetHotkeyManager() : nullptr;

    ctx.windowManager = hostAPI ? hostAPI->GetWindowManager() : nullptr;

    ctx.modeManager = hostAPI ? hostAPI->GetModeManager() : nullptr;

    ctx.brightnessManager = hostAPI ? hostAPI->GetBrightnessManager() : nullptr;

    ctx.audioManager = hostAPI ? hostAPI->GetAudioManager() : nullptr;

    ctx.guiManager = hostAPI ? hostAPI->GetGUIManager() : nullptr;

    ctx.screenshotManager = hostAPI ? hostAPI->GetScreenshotManager() : nullptr;

    ctx.clipboardManager = hostAPI ? hostAPI->GetClipboardManager() : nullptr;

    ctx.pixelAutomation = hostAPI ? hostAPI->GetPixelAutomation() : nullptr;

    ctx.automationManager = hostAPI ? hostAPI->GetAutomationManager() : nullptr;

    ctx.fileManager = hostAPI ? hostAPI->GetFileManager() : nullptr;

    ctx.processManager = hostAPI ? hostAPI->GetProcessManager() : nullptr;


    return ctx;
}

/**
 * Register all host modules
 * These modules require IHostAPI
 * 
 * MIGRATED TO BYTECODE VM:
 * Host modules now register through HostBridge with VM
 * Old interpreter-based modules are stubbed out
 */
void registerHostModules(ModuleLoader& loader) {
    // MIGRATED: All host modules moved to bytecode VM registration
    // See HostBridge::install() for VM-native host function registration
    (void)loader;  // Suppress unused warning
}

/**
 * Load all host modules into environment
 */
void loadHostModules(Environment& env, ModuleLoader& loader, std::shared_ptr<IHostAPI> hostAPI) {
    // MIGRATED: Environment-based loading replaced by bytecode VM
    (void)env;
    (void)loader;
    (void)hostAPI;
}

} // namespace havel
