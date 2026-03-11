/*
 * ModuleLoader.cpp
 *
 * Loads all host modules into the Havel environment.
 * Uses map-based registration for cleaner architecture.
 */
#include "ModuleLoader.hpp"
#include "havel-lang/runtime/Interpreter.hpp"
#include "havel-lang/runtime/HostModuleRegistry.hpp"
#include "havel-lang/stdlib/StringModule.hpp"
#include "havel-lang/stdlib/ArrayModule.hpp"
#include "havel-lang/stdlib/MathModule.hpp"
#include "havel-lang/stdlib/TypeModule.hpp"
#include "havel-lang/stdlib/FileModule.hpp"
#include "havel-lang/stdlib/RegexModule.hpp"
#include "havel-lang/stdlib/ProcessModule.hpp"
#include "havel-lang/stdlib/UtilityModule.hpp"
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
#include "process/Launcher.hpp"

namespace havel::modules {

// Forward declaration for module registration
static void registerBuiltinModules();

void loadHostModules(Environment& env, Interpreter* interpreter) {
    if (!interpreter) {
        return;
    }

    // Get HostContext from interpreter - pass by reference to avoid dangling refs
    HostContext& ctx = interpreter->getHostContext();

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
    havel::stdlib::registerUtilityModule(&env);

    // =========================================================================
    // HOST MODULES (loaded via registry)
    // =========================================================================
    
    // Register modules on first load
    static bool modulesRegistered = false;
    if (!modulesRegistered) {
        registerBuiltinModules();
        modulesRegistered = true;
    }
    
    // Load all registered modules using HostContext
    auto& registry = HostModuleRegistry::getInstance();
    registry.loadAllModules(env, ctx);
}

void registerAllModules() {
    registerBuiltinModules();
}

// ============================================================================
// Module Registration
// ============================================================================

static void registerBuiltinModules() {
    auto& registry = HostModuleRegistry::getInstance();
    
    // Window management
    registry.registerModule("window", registerWindowModule, "Window management functions", true);
    
    // Brightness control
    registry.registerModule("brightness", registerBrightnessModule, "Display brightness control", true);
    
    // Audio control
    registry.registerModule("audio", registerAudioModule, "Audio and volume control", true);
    
    // Clipboard operations
    registry.registerModule("clipboard", registerClipboardModule, "Clipboard operations", true);
    
    // Process launcher
    registry.registerModule("launcher", registerLauncherModule, "Process launching", true);
    
    // Media playback
    registry.registerModule("media", registerMediaModule, "Media playback control", true);
    
    // Help system
    registry.registerModule("help", registerHelpModule, "Help and documentation", true);
    
    // File system
    registry.registerModule("filesystem", registerFileManagerModule, "File system operations", true);
    
    // System information
    registry.registerModule("system", [](Environment& env, HostContext& ctx) {
        registerDetectorModule(env, ctx);
        registerSystemModule(env, ctx);
    }, "System information and detection", true);
    
    // I/O operations
    registry.registerModule("io", registerIOModule, "Input/output operations", true);
    
    // Async/concurrency
    registry.registerModule("async", registerAsyncModule, "Async and concurrency primitives", true);
    
    // Timer
    registry.registerModule("timer", registerTimerModule, "Timer and scheduling", true);
    
    // Config
    registry.registerModule("config", registerConfigModule, "Configuration management", true);
    
    // App
    registry.registerModule("app", registerAppModule, "Application lifecycle", true);
    
    // HTTP/Network
    registry.registerModule("http", registerHTTPModule, "HTTP client", true);
    
    // Runtime - requires Interpreter*, skip for now
    // registry.registerModule("runtime", [](Environment& env, HostContext& ctx) {
    //     registerRuntimeModule(env, nullptr);
    // }, "Runtime introspection", false);
    
    // Mode management
    registry.registerModule("mode", registerModeModule, "Mode management", true);
    
    // Hotkey
    registry.registerModule("hotkey", registerHotkeyModule, "Hotkey management", true);
    
    // Browser automation
    registry.registerModule("browser", registerBrowserModule, "Browser automation", true);
    
    // FFI - takes only Environment
    registry.registerModule("ffi", [](Environment& env, HostContext&) {
        ffi::registerFFIModule(env);
    }, "Foreign function interface", false);
    
    // GUI
    registry.registerModule("gui", registerGUIModule, "GUI operations", true);
    
    // Alt-tab
    registry.registerModule("alttab", registerAltTabModule, "Alt-tab switching", false);
    
    // Map manager
    registry.registerModule("mapmanager", registerMapManagerModule, "Map management", false);
    
    // Screenshot
    registry.registerModule("screenshot", registerScreenshotModule, "Screenshot capture", true);
    
    // Pixel automation
    registry.registerModule("pixel", registerPixelModule, "Pixel and image automation", true);
    
    // Automation
    registry.registerModule("automation", registerAutomationModule, "Automation workflows", true);
}

} // namespace havel::modules
