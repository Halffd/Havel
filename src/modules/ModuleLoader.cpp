/*
 * ModuleLoader.cpp
 *
 * Loads all host modules into the Havel environment.
 * Heavy modules (screenshot, pixel, automation) use lazy loading
 * to avoid loading PNG/zlib dependencies at startup.
 */
#include "ModuleLoader.hpp"
#include "LazyModuleLoader.hpp"
#include "havel-lang/runtime/Interpreter.hpp"
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
#include "havel-lang/stdlib/PhysicsModule.hpp"

namespace havel::modules {

void loadHostModules(Environment& env, Interpreter* interpreter) {
    if (!interpreter) {
        return;  // Can't load modules without interpreter
    }

    // Get HostContext from interpreter
    HostContext ctx = interpreter->getHostContext();

    // =========================================================================
    // CORE MODULES (loaded immediately - essential for basic operation)
    // These have minimal dependencies and no heavy image processing
    // =========================================================================
    
    // Window management
    registerWindowModule(env, ctx);
    
    // Brightness control
    registerBrightnessModule(env, ctx);
    
    // Audio control
    registerAudioModule(env, ctx);
    
    // Clipboard operations
    registerClipboardModule(env, ctx);
    
    // Process launcher
    registerLauncherModule(env, ctx);
    
    // Media playback control
    registerMediaModule(env, ctx);
    
    // Help system
    registerHelpModule(env, ctx);
    
    // File system operations
    registerFileManagerModule(env, ctx);
    
    // System information
    registerDetectorModule(env, ctx);
    
    // I/O operations (keyboard/mouse)
    registerIOModule(env, ctx);
    
    // Timer functionality
    registerTimerModule(env, ctx);
    
    // Configuration
    registerConfigModule(env, ctx);
    
    // Application control
    registerAppModule(env, ctx);
    
    // Mode management
    registerModeModule(env, ctx);
    
    // Hotkey management
    registerHotkeyModule(env, ctx);
    
    // Runtime utilities
    registerRuntimeModule(env, interpreter);
    
    // Standard library modules
    registerPhysicsModule(env, ctx);

    // =========================================================================
    // HEAVY MODULES (lazy-loaded - only when first accessed)
    // These modules pull in large dependencies (PNG, zlib, OpenCV, Qt, etc.)
    // =========================================================================
    
    static LazyModuleLoader lazyLoader(env, interpreter);
    
    // Screenshot module - loads libpng, zlib (~29% CPU when active)
    lazyLoader.registerLazy("screenshot", registerScreenshotModule);
    
    // Pixel/image recognition - loads OpenCV, libpng, zlib
    lazyLoader.registerLazy("pixel", registerPixelModule);
    
    // Automation module - loads image processing dependencies
    lazyLoader.registerLazy("automation", registerAutomationModule);
    
    // GUI module - loads Qt widgets, image handling
    lazyLoader.registerLazy("gui", registerGUIModule);
    
    // Map manager - loads image handling
    lazyLoader.registerLazy("mapmanager", registerMapManagerModule);
    
    // Browser automation - loads CDP, image handling
    lazyLoader.registerLazy("browser", registerBrowserModule);
    
    // HTTP module - loads network stack
    lazyLoader.registerLazy("http", registerHTTPModule);
    
    // Alt-tab window switcher - loads Qt, image handling
    lazyLoader.registerLazy("alttab", registerAltTabModule);
    
    // System module - loads additional system libraries
    lazyLoader.registerLazy("system", registerSystemModule);
    
    // Async module - loads threading libraries
    lazyLoader.registerLazy("async", registerAsyncModule);
    
    // Create lazy proxies in environment
    // When user accesses screenshot.full(), the module loads automatically
    env.Define("screenshot", lazyLoader.createLazyWrapper("screenshot", registerScreenshotModule));
    env.Define("pixel", lazyLoader.createLazyWrapper("pixel", registerPixelModule));
    env.Define("automation", lazyLoader.createLazyWrapper("automation", registerAutomationModule));
    env.Define("gui", lazyLoader.createLazyWrapper("gui", registerGUIModule));
    env.Define("mapmanager", lazyLoader.createLazyWrapper("mapmanager", registerMapManagerModule));
    env.Define("browser", lazyLoader.createLazyWrapper("browser", registerBrowserModule));
    env.Define("http", lazyLoader.createLazyWrapper("http", registerHTTPModule));
    env.Define("alttab", lazyLoader.createLazyWrapper("alttab", registerAltTabModule));
    env.Define("system", lazyLoader.createLazyWrapper("system", registerSystemModule));
    env.Define("async", lazyLoader.createLazyWrapper("async", registerAsyncModule));
}

} // namespace havel::modules
