// HostModules.cpp
// Service registry initialization for host modules
// Module registration is done via StdLibModules.cpp using VMApi

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
#ifdef HAVE_QT_EXTENSION
#include "../host/clipboard/ClipboardService.hpp"
#include "../host/clipboard/MonitoringClipboard.hpp"
#endif
#include "../host/audio/AudioService.hpp"
#include "../host/brightness/BrightnessService.hpp"
#include "../host/screenshot/ScreenshotService.hpp"
#include "../host/automation/PixelAutomationService.hpp"
#include "../host/automation/AutomationService.hpp"
#include "../host/chunker/TextChunkerService.hpp"
#include "../host/browser/BrowserService.hpp"
#include "../host/io/MapManagerService.hpp"
#include "../host/window/AltTabService.hpp"
#include "../host/timer/TimerService.hpp"
#include "../host/media/MediaService.hpp"
#include "../host/image/ImageService.hpp"
#include "../host/filesystem/FileSystemService.hpp"
#include "../host/network/NetworkService.hpp"
#include "../host/mouse/MouseService.hpp"
#include "../host/app/AppService.hpp"

namespace havel {

/**
 * Initialize service registry with all services
 * Called ONCE at application startup, before any modules are loaded.
 * All services MUST be registered here - modules will fail if services are missing.
 */
void initializeServiceRegistry(std::shared_ptr<IHostAPI> hostAPI) {
 if (!hostAPI) {
 debug("initializeServiceRegistry: hostAPI is null, skipping service registration");
 return;
 }

 auto& registry = host::ServiceRegistry::instance();

  if (hostAPI->GetIO()) {
    auto ioService = std::make_shared<host::IOService>(hostAPI->GetIO());
    registry.registerService<host::IOService>(ioService);
    host::MouseService::setIO(hostAPI->GetIO());
  } else {
 debug("initializeServiceRegistry: IO not available, skipping IO-dependent services");
 }

 if (hostAPI->GetHotkeyManager()) {
 auto hotkeyManager = hostAPI->GetHotkeyManager();
 auto hotkeyService = std::make_shared<host::HotkeyService>(
 std::shared_ptr<havel::HotkeyManager>(hotkeyManager, [](havel::HotkeyManager*){}));
 registry.registerService<host::HotkeyService>(hotkeyService);
 }

 if (hostAPI->GetWindowManager()) {
 auto windowService = std::make_shared<host::WindowService>(hostAPI->GetWindowManager());
 registry.registerService<host::WindowService>(windowService);
 }

  if (hostAPI->GetModeManager()) {
    auto modeService = std::make_shared<host::ModeService>(nullptr, hostAPI->GetModeManager());
    registry.registerService<host::ModeService>(modeService);
  }

    // ProcessManager is optional
    if (hostAPI->GetProcessManager()) {
        auto processService = std::make_shared<host::ProcessService>();
        registry.registerService<host::ProcessService>(processService);
    }

    // Clipboard service doesn't need constructor args
#ifdef HAVE_QT_EXTENSION
    auto clipboardService = std::make_shared<host::ClipboardService>();
    registry.registerService<host::ClipboardService>(clipboardService);

    auto monitoringClipboard = std::make_shared<host::MonitoringClipboard>();
    registry.registerService<host::MonitoringClipboard>(monitoringClipboard);
#endif

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
#ifdef HAVE_QT_EXTENSION
    auto altTabService = std::make_shared<host::AltTabService>();
    registry.registerService<host::AltTabService>(altTabService);
#endif

  // Timer service (tracking/management)
  auto timerService = std::make_shared<host::TimerService>();
  registry.registerService<host::TimerService>(timerService);

 // Automation service needs IO pointer
 if (hostAPI->GetIO()) {
 auto automationService = std::make_shared<host::AutomationService>(std::shared_ptr<IO>(hostAPI->GetIO(), [](IO*){}));
 registry.registerService<host::AutomationService>(automationService);
 }



  // File system service doesn't need constructor args (pure C++ with std::filesystem)
  auto fsService = std::make_shared<host::FileSystemService>();
  registry.registerService<host::FileSystemService>(fsService);

  // Image service (OpenCV image processing, no deps)
  try {
    auto imageService = std::make_shared<host::ImageService>();
    registry.registerService<host::ImageService>(imageService);
  } catch (const std::exception& e) {
    debug("initializeServiceRegistry: ImageService failed: {}", e.what());
  }

  // Media service (MPRIS/D-Bus, may fail if no DBus session)
  try {
    auto mediaService = std::make_shared<host::MediaService>();
    registry.registerService<host::MediaService>(mediaService);
  } catch (const std::exception& e) {
    debug("initializeServiceRegistry: MediaService failed: {}", e.what());
  }

  // App service (system info, env, clipboard helpers)
  try {
    auto appService = std::make_shared<host::AppService>();
    registry.registerService<host::AppService>(appService);
  } catch (const std::exception& e) {
    debug("initializeServiceRegistry: AppService failed: {}", e.what());
  }

  // Network service (HTTP via curl)
  try {
    auto networkService = std::make_shared<host::NetworkService>();
    registry.registerService<host::NetworkService>(networkService);
  } catch (const std::exception& e) {
    debug("initializeServiceRegistry: NetworkService failed: {}", e.what());
  }

    debug("ServiceRegistry initialized with {} services", registry.size());
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
