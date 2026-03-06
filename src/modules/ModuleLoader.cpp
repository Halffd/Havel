/*
 * ModuleLoader.cpp
 * 
 * Loads all host modules into the Havel environment.
 */
#include "ModuleLoader.hpp"
#include "havel-lang/runtime/Interpreter.hpp"
#include "window/WindowModule.hpp"
// #include "hotkey/HotkeyModule.hpp"
// #include "audio/AudioModule.hpp"
// #include "brightness/BrightnessModule.hpp"
// #include "screenshot/ScreenshotModule.hpp"
// #include "clipboard/ClipboardModule.hpp"
// #include "process/ProcessModule.hpp"
// #include "automation/AutomationModule.hpp"

namespace havel::modules {

void loadHostModules(Environment& env, Interpreter* interpreter) {
    if (!interpreter) {
        return;  // Can't load modules without interpreter
    }
    
    // Get HostContext from interpreter
    HostContext ctx = interpreter->getHostContext();
    
    // Load window management module
    registerWindowModule(env, ctx);
    
    // TODO: Load remaining modules as they are extracted:
    // registerHotkeyModule(env, ctx);
    // registerAudioModule(env, ctx);
    // registerBrightnessModule(env, ctx);
    // registerScreenshotModule(env, ctx);
    // registerClipboardModule(env, ctx);
    // registerProcessModule(env, ctx);
    // registerAutomationModule(env, ctx);
}

} // namespace havel::modules
