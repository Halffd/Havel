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

namespace havel {

/**
 * Register all host modules
 * These modules require IHostAPI
 */
void registerHostModules(ModuleLoader& loader) {
    loader.addHost("window", registerWindowModule);
    loader.addHost("brightness", registerBrightnessModule);
    loader.addHost("audio", registerAudioModule);
    loader.addHost("screenshot", registerScreenshotModule);
    loader.addHost("clipboard", registerClipboardModule);
    loader.addHost("pixel", registerPixelModule);
    loader.addHost("automation", registerAutomationModule);
    loader.addHost("launcher", registerLauncherModule);
    loader.addHost("media", registerMediaModule);
    loader.addHost("help", registerHelpModule);
    loader.addHost("filesystem", registerFileManagerModule);
    loader.addHost("system", registerSystemModule);
    loader.addHost("gui", registerGUIModule);
    loader.addHost("alttab", registerAltTabModule);
    loader.addHost("mapmanager", registerMapManagerModule);
    loader.addHost("io", registerIOModule);
    loader.addHost("async", registerAsyncModule);
    loader.addHost("timer", registerTimerModule);
    loader.addHost("config", registerConfigModule);
    loader.addHost("app", registerAppModule);
    loader.addHost("http", registerHTTPModule);
    loader.addHost("runtime", registerRuntimeModule);
    loader.addHost("mode", registerModeModule);
    loader.addHost("hotkey", registerHotkeyModule);
    loader.addHost("browser", registerBrowserModule);
    loader.addHost("ffi", registerFFIModule);
}

/**
 * Load all host modules into environment
 */
void loadHostModules(Environment& env, ModuleLoader& loader, IHostAPI* hostAPI) {
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
    loader.load(env, "ffi", hostAPI);
}

} // namespace havel
