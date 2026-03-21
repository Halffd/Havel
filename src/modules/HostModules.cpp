// HostModules.cpp
// Register all host modules
// Simple, explicit registration

#include "../havel-lang/runtime/ModuleLoader.hpp"
#include "../havel-lang/compiler/bytecode/HostBridge.hpp"
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

    if (!hostAPI->GetHotkeyManager()) {
        throw std::runtime_error("initializeServiceRegistry: HotkeyManager not available");
    }
    auto hotkeyManager = hostAPI->GetHotkeyManager();
    auto hotkeyService = std::make_shared<host::HotkeyService>(
        std::shared_ptr<havel::HotkeyManager>(hotkeyManager, [](havel::HotkeyManager*){}));
    registry.registerService<host::HotkeyService>(hotkeyService);

    if (!hostAPI->GetWindowManager()) {
        throw std::runtime_error("initializeServiceRegistry: WindowManager not available");
    }
    auto windowService = std::make_shared<host::WindowService>(hostAPI->GetWindowManager());
    registry.registerService<host::WindowService>(windowService);

    if (!hostAPI->GetModeManager()) {
        throw std::runtime_error("initializeServiceRegistry: ModeManager not available");
    }
    // ModeService is created later in createHostBridgeDependencies() when VM is available
    // because it needs VM* for callback management

    if (!hostAPI->GetProcessManager()) {
        throw std::runtime_error("initializeServiceRegistry: ProcessManager not available");
    }
    auto processService = std::make_shared<host::ProcessService>();
    registry.registerService<host::ProcessService>(processService);

    // Clipboard service doesn't need constructor args
    auto clipboardService = std::make_shared<host::ClipboardService>();
    registry.registerService<host::ClipboardService>(clipboardService);

    if (!hostAPI->GetAudioManager()) {
        throw std::runtime_error("initializeServiceRegistry: AudioManager not available");
    }
    auto audioService = std::make_shared<host::AudioService>(hostAPI->GetAudioManager());
    registry.registerService<host::AudioService>(audioService);

    if (!hostAPI->GetBrightnessManager()) {
        throw std::runtime_error("initializeServiceRegistry: BrightnessManager not available");
    }
    auto brightnessService = std::make_shared<host::BrightnessService>(hostAPI->GetBrightnessManager());
    registry.registerService<host::BrightnessService>(brightnessService);
    
    // Screenshot service doesn't need constructor args (uses Qt directly)
    auto screenshotService = std::make_shared<host::ScreenshotService>();
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
 * Create HostBridgeDependencies with service registry
 * 
 * @param hostAPI Host API for accessing managers
 * @param vm VM instance for callback management (needed for ModeService)
 * @return HostBridgeDependencies with services and VM
 */
compiler::HostBridgeDependencies createHostBridgeDependencies(
    std::shared_ptr<IHostAPI> hostAPI,
    compiler::VM* vm) {
    
    compiler::HostBridgeDependencies deps;
    deps.services = &host::ServiceRegistry::instance();
    deps.mode_manager = hostAPI ? hostAPI->GetModeManager() : nullptr;
    
    // Create ModeService with VM injection (for direct closure pinning)
    if (vm && hostAPI && hostAPI->GetModeManager()) {
        auto modeService = std::make_shared<host::ModeService>(vm, hostAPI->GetModeManager());
        deps.services->registerService<host::ModeService>(modeService);
    }
    
    return deps;
}

/**
 * Register all host modules
 * These modules require IHostAPI
 */
void registerHostModules(ModuleLoader& loader) {
    loader.addHost("window", modules::registerWindowQueryModule);
    loader.addHost("brightness", modules::registerBrightnessModule);
    loader.addHost("audio", modules::registerAudioModule);
    loader.addHost("screenshot", modules::registerScreenshotModule);
    loader.addHost("clipboard", modules::registerClipboardModule);
    loader.addHost("pixel", modules::registerPixelModule);
    loader.addHost("automation", modules::registerAutomationModule);
    loader.addHost("launcher", modules::registerLauncherModule);
    loader.addHost("process", modules::registerProcessModule);
    loader.addHost("help", modules::registerHelpModule);
    loader.addHost("filesystem", modules::registerFileManagerModule);
    loader.addHost("system", modules::registerSystemModule);
    loader.addHost("detector", modules::registerDetectorModule);
    loader.addHost("gui", modules::registerGUIModule);
    loader.addHost("alttab", modules::registerAltTabModule);
    loader.addHost("mapmanager", modules::registerMapManagerModule);
    loader.addHost("io", modules::registerIOModule);
    loader.addHost("async", modules::registerAsyncModule);
    loader.addHost("config", modules::registerConfigModule);
    loader.addHost("http", modules::registerHTTPModule);
    // loader.addHost("runtime", modules::registerRuntimeModule);  // Needs Interpreter*
    loader.addHost("mode", modules::registerModeModule);
    loader.addHost("hotkey", modules::registerHotkeyModule);
    loader.addHost("browser", modules::registerBrowserModule);
    loader.addHost("concurrency", modules::registerConcurrencyModule);
    // loader.addHost("ffi", modules::ffi::registerFFIModule);  // No IHostAPI
    
    // Header-only modules (registered directly, not through loader)
    // timer, media, network, app are registered inline in loadHostModules
}

/**
 * Load all host modules into environment
 */
void loadHostModules(Environment& env, ModuleLoader& loader, std::shared_ptr<IHostAPI> hostAPI) {
    if (!hostAPI) return;

    loader.load(env, "window", hostAPI);
    loader.load(env, "brightness", hostAPI);
    loader.load(env, "audio", hostAPI);
    loader.load(env, "screenshot", hostAPI);
    loader.load(env, "clipboard", hostAPI);
    loader.load(env, "pixel", hostAPI);
    loader.load(env, "automation", hostAPI);
    loader.load(env, "launcher", hostAPI);
    loader.load(env, "help", hostAPI);
    loader.load(env, "filesystem", hostAPI);
    loader.load(env, "system", hostAPI);
    loader.load(env, "detector", hostAPI);
    loader.load(env, "gui", hostAPI);
    loader.load(env, "alttab", hostAPI);
    loader.load(env, "mapmanager", hostAPI);
    loader.load(env, "io", hostAPI);
    loader.load(env, "async", hostAPI);
    loader.load(env, "config", hostAPI);
    loader.load(env, "http", hostAPI);
    loader.load(env, "runtime", hostAPI);
    loader.load(env, "mode", hostAPI);
    loader.load(env, "hotkey", hostAPI);
    loader.load(env, "browser", hostAPI);
    loader.load(env, "concurrency", hostAPI);
    loader.load(env, "window", hostAPI);
    loader.load(env, "config", hostAPI);
    // loader.load(env, "ffi", hostAPI);  // Not registered
    
    // Header-only modules (registered directly)
    host::registerTimerModule(env, hostAPI);
    host::registerMediaModule(env, hostAPI);
    host::registerNetworkModule(env, hostAPI);
    host::registerAppModule(env, hostAPI);
}

} // namespace havel
