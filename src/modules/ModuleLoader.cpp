/*
 * ModuleLoader.cpp
 * 
 * Loads all host modules into the Havel environment.
 */
#include "ModuleLoader.hpp"
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
// #include "hotkey/HotkeyModule.hpp"
// #include "process/ProcessModule.hpp"

namespace havel::modules {

void loadHostModules(Environment& env, Interpreter* interpreter) {
    if (!interpreter) {
        return;  // Can't load modules without interpreter
    }
    
    // Get HostContext from interpreter
    HostContext ctx = interpreter->getHostContext();
    
    // Load window management module
    registerWindowModule(env, ctx);
    
    // Load brightness management module
    registerBrightnessModule(env, ctx);
    
    // Load audio management module
    registerAudioModule(env, ctx);
    
    // Load screenshot module
    registerScreenshotModule(env, ctx);
    
    // Load clipboard module
    registerClipboardModule(env, ctx);
    
    // Load pixel/image recognition module
    registerPixelModule(env, ctx);
    
    // Load automation module
    registerAutomationModule(env, ctx);

    // Load launcher module
    registerLauncherModule(env, ctx);
    
    // Load media module
    registerMediaModule(env, ctx);
    
    // Load help module
    registerHelpModule(env, ctx);
    
    // Load file manager module
    registerFileManagerModule(env, ctx);
    
    // Load detector module
    registerDetectorModule(env, ctx);
    
    // Load GUI module
    registerGUIModule(env, ctx);
    
    // Load Alt-Tab module
    registerAltTabModule(env, ctx);
    
    // Load MapManager module
    registerMapManagerModule(env, ctx);
    
    // Load IO module
    registerIOModule(env, ctx);
    
    // Load async module
    registerAsyncModule(env, ctx);
    
    // Load system module
    registerSystemModule(env, ctx);
    
    // Load timer module
    registerTimerModule(env, ctx);
    
    // Load config module
    registerConfigModule(env, ctx);
    
    // Load app module
    registerAppModule(env, ctx);
    
    // Load HTTP module
    registerHTTPModule(env, ctx);
    
    // Load runtime utilities module (app, debug, runOnce)
    registerRuntimeModule(env, interpreter);
    
    // TODO: Load remaining modules as they are extracted:
    // registerHotkeyModule(env, ctx);
    // registerProcessModule(env, ctx);
}

} // namespace havel::modules
