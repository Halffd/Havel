/*
 * ModuleLoader.cpp
 *
 * Loads all host modules into the Havel environment.
 */
#include "ModuleLoader.hpp"
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/stdlib/StringModule.hpp"
#include "havel-lang/stdlib/ArrayModule.hpp"
#include "havel-lang/stdlib/MathModule.hpp"
#include "havel-lang/stdlib/TypeModule.hpp"
#include "havel-lang/stdlib/FileModule.hpp"
#include "havel-lang/stdlib/RegexModule.hpp"
#include "havel-lang/stdlib/ProcessModule.hpp"
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
#include "ffi/FFIModule.hpp"

namespace havel::modules {

void loadHostModules(Environment& env, Interpreter* interpreter) {
    if (!interpreter) {
        return;  // Can't load modules without interpreter
    }

    // Get HostContext from interpreter
    HostContext ctx = interpreter->getHostContext();

    // =========================================================================
    // STANDARD LIBRARY (always loaded - core language functions)
    // =========================================================================
    havel::stdlib::registerStringModule(&env);
    havel::stdlib::registerArrayModule(&env);
    havel::stdlib::registerMathModule(&env);
    havel::stdlib::registerTypeModule(&env);
    havel::stdlib::registerFileModule(&env);
    havel::stdlib::registerRegexModule(&env);
    havel::stdlib::registerProcessModule(&env);

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
    // HEAVY MODULES (loaded at startup - lazy loading removed)
    // These modules pull in large dependencies (PNG, zlib, OpenCV, Qt, etc.)
    // Lazy loading was removed - modules load when first used via services
    // =========================================================================

    // Screenshot module - loads libpng, zlib
    registerScreenshotModule(env, ctx);

    // Pixel/image recognition - loads OpenCV, libpng, zlib
    registerPixelModule(env, ctx);

    // Automation module - loads image processing dependencies
    registerAutomationModule(env, ctx);

    // GUI module - loads Qt widgets, image handling
    registerGUIModule(env, ctx);

    // Map manager - loads image handling
    registerMapManagerModule(env, ctx);

    // Browser automation - loads CDP, image handling
    registerBrowserModule(env, ctx);

    // HTTP module - loads network stack
    registerHTTPModule(env, ctx);

    // Alt-tab window switcher - loads Qt, image handling
    registerAltTabModule(env, ctx);

    // System module - loads additional system libraries
    registerSystemModule(env, ctx);

    // Async module - loads threading libraries
    registerAsyncModule(env, ctx);

    // FFI module - dynamic library loading and C function calls
    ffi::registerFFIModule(env);
}

} // namespace havel::modules
