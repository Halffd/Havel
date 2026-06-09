// HostModules.cpp
// Service registry initialization for host modules

#include "havel-lang/runtime/Modules.hpp"
#include "havel-lang/runtime/HostAPI.hpp"
#include "../utils/Logger.hpp"
#include "../host/ServiceRegistry.hpp"
#include "../core/io/IO.hpp"
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
#include <cstdio>

#define BR_DEBUG(fmt, ...) fprintf(stderr, "[BR-DEBUG] " fmt "\n", ##__VA_ARGS__)

namespace havel {

void declareAllServices() {
	auto& registry = host::ServiceRegistry::instance();
  registry.declareService<IO>("io", "core");
	registry.declareService<host::HotkeyService>("hotkey", "core");
	registry.declareService<host::WindowService>("window", "core");
	registry.declareService<host::ModeService>("mode", "core");
	registry.declareService<host::ProcessService>("process", "core");
	registry.declareService<host::ClipboardService>("clipboard", "qt");
	registry.declareService<host::MonitoringClipboard>("monitoring-clipboard", "qt");
	registry.declareService<host::TextChunkerService>("chunker", "util");
	registry.declareService<host::BrowserService>("browser", "util");
	registry.declareService<host::MapManagerService>("map-manager", "io");
	registry.declareService<host::AltTabService>("alt-tab", "qt");
	registry.declareService<host::TimerService>("timer", "util");
	registry.declareService<host::AutomationService>("automation", "io");
	registry.declareService<host::BrightnessService>("brightness", "core");
	registry.declareService<host::FileSystemService>("filesystem", "util");
	registry.declareService<host::ImageService>("image", "util");
	registry.declareService<host::MediaService>("media", "util");
	registry.declareService<host::AppService>("app", "util");
	registry.declareService<host::NetworkService>("network", "util");
}

void initializeServiceRegistry(std::shared_ptr<IHostAPI> hostAPI,
							   const host::ServiceFilter& includes,
							   const host::ServiceFilter& excludes) {
	BR_DEBUG("initializeServiceRegistry called, hostAPI=%p", (void*)hostAPI.get());
	if (!hostAPI) {
		debug("initializeServiceRegistry: hostAPI is null, skipping service registration");
		return;
	}

	auto& registry = host::ServiceRegistry::instance();
	declareAllServices();

  if (hostAPI->GetIO() && registry.shouldRegister("io", includes, excludes)) {
    auto* io_raw = hostAPI->GetIO();
    auto io_ptr = std::shared_ptr<IO>(io_raw, [](IO*){});
    registry.registerService<IO>(io_ptr);
    host::MouseService::setIO(io_raw);
  } else {
    debug("initializeServiceRegistry: IO not available or excluded, skipping IO-dependent services");
  }

	if (hostAPI->GetHotkeyManager() && registry.shouldRegister("hotkey", includes, excludes)) {
		auto hotkeyManager = hostAPI->GetHotkeyManager();
		auto hotkeyService = std::make_shared<host::HotkeyService>(
			std::shared_ptr<havel::HotkeyManager>(hotkeyManager, [](havel::HotkeyManager*){}));
		registry.registerService<host::HotkeyService>(hotkeyService);
	}

	if (hostAPI->GetWindowManager() && registry.shouldRegister("window", includes, excludes)) {
		auto windowService = std::make_shared<host::WindowService>(hostAPI->GetWindowManager());
		registry.registerService<host::WindowService>(windowService);
	}

	if (hostAPI->GetModeManager() && registry.shouldRegister("mode", includes, excludes)) {
 auto modeService = std::make_shared<host::ModeService>(hostAPI.get(), hostAPI->GetModeManager());
		registry.registerService<host::ModeService>(modeService);
	}

	if (hostAPI->GetProcessManager() && registry.shouldRegister("process", includes, excludes)) {
		auto processService = std::make_shared<host::ProcessService>();
		registry.registerService<host::ProcessService>(processService);
	}

#ifdef HAVE_QT_EXTENSION
	if (registry.shouldRegister("clipboard", includes, excludes)) {
		auto clipboardService = std::make_shared<host::ClipboardService>();
		registry.registerService<host::ClipboardService>(clipboardService);
	}

	if (registry.shouldRegister("monitoring-clipboard", includes, excludes)) {
		auto monitoringClipboard = std::make_shared<host::MonitoringClipboard>();
		registry.registerService<host::MonitoringClipboard>(monitoringClipboard);
	}
#endif

	if (registry.shouldRegister("chunker", includes, excludes)) {
		auto chunkerService = std::make_shared<host::TextChunkerService>();
		registry.registerService<host::TextChunkerService>(chunkerService);
	}

	if (registry.shouldRegister("browser", includes, excludes)) {
		auto browserService = std::make_shared<host::BrowserService>();
		registry.registerService<host::BrowserService>(browserService);
	}

	if (registry.shouldRegister("map-manager", includes, excludes)) {
		auto mapManagerService = std::make_shared<host::MapManagerService>(hostAPI->GetIO() ? std::shared_ptr<IO>(hostAPI->GetIO(), [](IO*){}) : std::shared_ptr<IO>());
		registry.registerService<host::MapManagerService>(mapManagerService);
	}

#ifdef HAVE_QT_EXTENSION
	if (registry.shouldRegister("alt-tab", includes, excludes)) {
		auto altTabService = std::make_shared<host::AltTabService>();
		registry.registerService<host::AltTabService>(altTabService);
	}
#endif

	if (registry.shouldRegister("timer", includes, excludes)) {
		auto timerService = std::make_shared<host::TimerService>();
		registry.registerService<host::TimerService>(timerService);
	}

	if (hostAPI->GetIO() && registry.shouldRegister("automation", includes, excludes)) {
		auto automationService = std::make_shared<host::AutomationService>(std::shared_ptr<IO>(hostAPI->GetIO(), [](IO*){}));
		registry.registerService<host::AutomationService>(automationService);
	}

	if (hostAPI->GetBrightnessManager() && registry.shouldRegister("brightness", includes, excludes)) {
		BR_DEBUG("registering BrightnessService with manager=%p", (void*)hostAPI->GetBrightnessManager());
		auto brightnessService = std::make_shared<host::BrightnessService>(hostAPI->GetBrightnessManager());
		registry.registerService<host::BrightnessService>(brightnessService);
		BR_DEBUG("BrightnessService registered, service count=%zu", registry.size());
	} else {
		BR_DEBUG("skipping BrightnessService: manager=%p shouldRegister=%d",
			(void*)(hostAPI ? hostAPI->GetBrightnessManager() : nullptr),
			registry.shouldRegister("brightness", includes, excludes));
	}

	if (registry.shouldRegister("filesystem", includes, excludes)) {
		auto fsService = std::make_shared<host::FileSystemService>();
		registry.registerService<host::FileSystemService>(fsService);
	}

	if (registry.shouldRegister("image", includes, excludes)) {
		try {
			auto imageService = std::make_shared<host::ImageService>();
			registry.registerService<host::ImageService>(imageService);
		} catch (const std::exception& e) {
			debug("initializeServiceRegistry: ImageService failed: {}", e.what());
		}
	}

	if (registry.shouldRegister("media", includes, excludes)) {
		try {
			auto mediaService = std::make_shared<host::MediaService>();
			registry.registerService<host::MediaService>(mediaService);
		} catch (const std::exception& e) {
			debug("initializeServiceRegistry: MediaService failed: {}", e.what());
		}
	}

	if (registry.shouldRegister("app", includes, excludes)) {
		try {
			auto appService = std::make_shared<host::AppService>();
			registry.registerService<host::AppService>(appService);
		} catch (const std::exception& e) {
			debug("initializeServiceRegistry: AppService failed: {}", e.what());
		}
	}

	if (registry.shouldRegister("network", includes, excludes)) {
		try {
			auto networkService = std::make_shared<host::NetworkService>();
			registry.registerService<host::NetworkService>(networkService);
		} catch (const std::exception& e) {
			debug("initializeServiceRegistry: NetworkService failed: {}", e.what());
		}
	}

	debug("ServiceRegistry initialized with {} services", registry.size());
}

havel::HostContext createHostContext(std::shared_ptr<IHostAPI> hostAPI) {
	havel::HostContext ctx;

	if (!hostAPI) {
		return ctx;
	}

	ctx.io = hostAPI ? hostAPI->GetIO() : nullptr;
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

void registerHostModules(ModuleLoader& loader) {
	(void)loader;
}

void loadHostModules(Environment& env, ModuleLoader& loader, std::shared_ptr<IHostAPI> hostAPI) {
	(void)env;
	(void)loader;
	(void)hostAPI;
}

} // namespace havel
