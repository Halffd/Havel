// HostModules.cpp
// Register all host modules
// Simple, explicit registration

#include "../havel-lang/runtime/ModuleLoader.hpp"
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

namespace havel {

/**
 * Register all host modules
 * These modules require IHostAPI
 */
void registerHostModules(ModuleLoader& loader) {
    loader.addHost("window", modules::registerWindowModule);
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
    // loader.load(env, "ffi", hostAPI);  // Not registered
}

} // namespace havel
