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
#include "window/WindowModule.hpp"
#include "brightness/BrightnessModule.hpp"
#include "audio/AudioModule.hpp"
#include "screenshot/ScreenshotModule.hpp"
#include "clipboard/ClipboardModule.hpp"
#include "automation/PixelModule.hpp"
#include "automation/AutomationModule.hpp"
#include "launcher/LauncherModule.hpp"
#include "media/MediaModule.hpp"
#include "help/HelpModule.hpp"
#include "filesystem/FileManagerModule.hpp"
#include "system/DetectorModule.hpp"
#include "gui/GUIModule.hpp"
#include "alttab/AltTabModule.hpp"
#include "mapmanager/MapManagerModule.hpp"
#include "io/IOModule.hpp"
#include "async/AsyncModule.hpp"
#include "system/SystemModule.hpp"
#include "timer/TimerModule.hpp"
#include "config/ConfigModule.hpp"
#include "app/AppModule.hpp"
#include "network/HTTPModule.hpp"
#include "runtime/RuntimeModule.hpp"
#include "mode/ModeModule.hpp"
#include "hotkey/HotkeyModule.hpp"
#include "browser/BrowserModule.hpp"
#include "ffi/FFIModule.hpp"
#include "process/ProcessModule.hpp"
#include "concurrency/ConcurrencyModule.hpp"

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
    auto modeManager = hostAPI->GetModeManager();
    auto modeService = std::make_shared<host::ModeService>(
        std::shared_ptr<havel::ModeManager>(modeManager, [](havel::ModeManager*){}));
    registry.registerService<host::ModeService>(modeService);

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
    
    info("ServiceRegistry initialized with {} services", registry.size());
}

/**
 * Create HostBridgeDependencies with service registry
 */
compiler::HostBridgeDependencies createHostBridgeDependencies(std::shared_ptr<IHostAPI> hostAPI) {
    compiler::HostBridgeDependencies deps;
    deps.services = &host::ServiceRegistry::instance();
    deps.mode_manager = hostAPI ? hostAPI->GetModeManager() : nullptr;
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
    loader.addHost("media", modules::registerMediaModule);
    loader.addHost("help", modules::registerHelpModule);
    loader.addHost("filesystem", modules::registerFileManagerModule);
    loader.addHost("system", modules::registerSystemModule);
    loader.addHost("detector", modules::registerDetectorModule);
    loader.addHost("gui", modules::registerGUIModule);
    loader.addHost("alttab", modules::registerAltTabModule);
    loader.addHost("mapmanager", modules::registerMapManagerModule);
    loader.addHost("io", modules::registerIOModule);
    loader.addHost("async", modules::registerAsyncModule);
    loader.addHost("timer", modules::registerTimerModule);
    loader.addHost("config", modules::registerConfigModule);
    loader.addHost("app", modules::registerAppModule);
    loader.addHost("http", modules::registerHTTPModule);
    // loader.addHost("runtime", modules::registerRuntimeModule);  // Needs Interpreter*
    loader.addHost("mode", modules::registerModeModule);
    loader.addHost("hotkey", modules::registerHotkeyModule);
    loader.addHost("browser", modules::registerBrowserModule);
    loader.addHost("concurrency", modules::registerConcurrencyModule);
    // loader.addHost("ffi", modules::ffi::registerFFIModule);  // No IHostAPI
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
    loader.load(env, "media", hostAPI);
    loader.load(env, "help", hostAPI);
    loader.load(env, "filesystem", hostAPI);
    loader.load(env, "system", hostAPI);
    loader.load(env, "detector", hostAPI);
    loader.load(env, "gui", hostAPI);
    loader.load(env, "alttab", hostAPI);
    loader.load(env, "mapmanager", hostAPI);
    loader.load(env, "io", hostAPI);
    loader.load(env, "async", hostAPI);
    loader.load(env, "timer", hostAPI);
    loader.load(env, "config", hostAPI);
    loader.load(env, "app", hostAPI);
    loader.load(env, "http", hostAPI);
    loader.load(env, "runtime", hostAPI);
    loader.load(env, "mode", hostAPI);
    loader.load(env, "hotkey", hostAPI);
    loader.load(env, "browser", hostAPI);
    loader.load(env, "concurrency", hostAPI);
    loader.load(env, "window", hostAPI);
    loader.load(env, "config", hostAPI);
    // loader.load(env, "ffi", hostAPI);  // Not registered
}

} // namespace havel
