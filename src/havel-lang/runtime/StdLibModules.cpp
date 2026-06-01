// StdLibModules.cpp
// Register all standard library modules with VM

#ifndef HAVEL_PURE_VM
#include "../../modules/alttab/AltTabModule.hpp"
#include "../../modules/app/AppModule.hpp"
#include "../../modules/audio/AudioModule.hpp"
#include "../../modules/automation/AutomationModule.hpp"
#include "../../modules/automation/PixelModule.hpp"
#include "../../modules/brightness/BrightnessModule.hpp"
#include "../../modules/clipboard/ClipboardModule.hpp"
#include "../../modules/clipboard/HistoryClipboardModule.hpp"
#include "../../modules/clipboard/MonitoringClipboardModule.hpp"
#include "../../modules/config/ConfigModule.hpp"
#include "../../modules/ffi/FFIModule.hpp"
#include "../../modules/filesystem/FileManagerModule.hpp"
#include "../../modules/help/HelpModule.hpp"
#include "../../modules/image/ImageModule.hpp"
#include "../../modules/io/IOModule.hpp"
#include "../../modules/mapmanager/MapManagerModule.hpp"
#include "../../modules/media/MediaModule.hpp"
#include "../../modules/mode/ModeModule.hpp"
#include "../../modules/mouse/MouseModule.hpp"
#include "../../modules/screenshot/ScreenshotModule.hpp"
#include "../../modules/ui/UIModule.hpp"
#include "../../modules/window/WindowMonitorModule.hpp"
#include "../../modules/display/DisplayModule.hpp"
#endif

#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "loader/ModulePlugin.h"

namespace havel::stdlib {
void registerMathModule(const compiler::VMApi &api);
void registerStringModule(const compiler::VMApi &api);
void registerObjectModule(const compiler::VMApi &api);
void registerTypeModule(const compiler::VMApi &api);
void registerArrayModule(const compiler::VMApi &api);
 void registerRegexModule(const compiler::VMApi &api);
 void registerTimeModule(const compiler::VMApi &api);
void registerTimerModule(const compiler::VMApi &api);
#ifndef HAVEL_PURE_VM
void registerHotkeyModule(const compiler::VMApi &api);
#endif
void registerFsModule(const compiler::VMApi &api);
void registerRandomModule(const compiler::VMApi &api);
void registerLogModule(const compiler::VMApi &api);
void registerDebugModule(const compiler::VMApi &api);
void registerSysModule(const compiler::VMApi &api);
void registerShellModule(const compiler::VMApi &api);
void registerPointerModule(const compiler::VMApi &api);
void registerFormatModule(const compiler::VMApi &api);
void registerPackModule(const compiler::VMApi &api);
void registerBitModule(const compiler::VMApi &api);
void registerOptionModule(const compiler::VMApi &api);
void registerBytecodeBuilderModule(const compiler::VMApi &api);
#ifndef HAVEL_PURE_VM
void registerHttpModule(const compiler::VMApi &api);
void registerBrowserModule(const compiler::VMApi &api);
#endif
} // namespace havel::stdlib

namespace havel {

static void tryPluginOrLazy(compiler::HostBridge &bridge,
                            const std::string &name,
                            std::function<void(compiler::VMApi &)> staticInit) {
    auto &vm = *bridge.context().vm;
    auto plugin = bridge.extensionLoader().loadModulePlugin(name);
    if (plugin) {
        auto regFn = plugin->register_fn;
        vm.registerLazyModule(name, [regFn](compiler::VMApi &a) {
            regFn(static_cast<void *>(&a));
        });
    } else {
        vm.registerLazyModule(name, std::move(staticInit));
    }
}

void registerStdLibWithVM(compiler::HostBridge &bridge) {
    compiler::VMApi api(*bridge.context().vm);
    auto &vm = *bridge.context().vm;

    bridge.extensionLoader().addModulePaths();

    // Core modules: eager (used by nearly every script)
    stdlib::registerMathModule(api);
    stdlib::registerStringModule(api);
    stdlib::registerObjectModule(api);
    stdlib::registerTypeModule(api);
    stdlib::registerArrayModule(api);

    // Extended stdlib: lazy — try .so plugin first, fall back to static
    tryPluginOrLazy(bridge, "regex", [](compiler::VMApi &a) { stdlib::registerRegexModule(a); });
    tryPluginOrLazy(bridge, "time", [](compiler::VMApi &a) { stdlib::registerTimeModule(a); });
    tryPluginOrLazy(bridge, "timer", [](compiler::VMApi &a) { stdlib::registerTimerModule(a); });
#ifndef HAVEL_PURE_VM
    tryPluginOrLazy(bridge, "hotkey", [](compiler::VMApi &a) { stdlib::registerHotkeyModule(a); });
#endif
    tryPluginOrLazy(bridge, "fs", [](compiler::VMApi &a) { stdlib::registerFsModule(a); });
    tryPluginOrLazy(bridge, "random", [](compiler::VMApi &a) { stdlib::registerRandomModule(a); });
    tryPluginOrLazy(bridge, "log", [](compiler::VMApi &a) { stdlib::registerLogModule(a); });
    tryPluginOrLazy(bridge, "debug", [](compiler::VMApi &a) { stdlib::registerDebugModule(a); });
    tryPluginOrLazy(bridge, "sys", [](compiler::VMApi &a) { stdlib::registerSysModule(a); });
    tryPluginOrLazy(bridge, "shell", [](compiler::VMApi &a) { stdlib::registerShellModule(a); });
    tryPluginOrLazy(bridge, "pointer", [](compiler::VMApi &a) { stdlib::registerPointerModule(a); });
    tryPluginOrLazy(bridge, "format", [](compiler::VMApi &a) { stdlib::registerFormatModule(a); });
    tryPluginOrLazy(bridge, "pack", [](compiler::VMApi &a) { stdlib::registerPackModule(a); });
    tryPluginOrLazy(bridge, "bit", [](compiler::VMApi &a) { stdlib::registerBitModule(a); });
    tryPluginOrLazy(bridge, "option", [](compiler::VMApi &a) { stdlib::registerOptionModule(a); });
    tryPluginOrLazy(bridge, "bytecodeBuilder", [](compiler::VMApi &a) { stdlib::registerBytecodeBuilderModule(a); });

#ifndef HAVEL_PURE_VM
    // Host modules: lazy — try .so plugin first, fall back to static
    tryPluginOrLazy(bridge, "http", [](compiler::VMApi &a) { stdlib::registerHttpModule(a); });
    tryPluginOrLazy(bridge, "browser", [](compiler::VMApi &a) { stdlib::registerBrowserModule(a); });
    tryPluginOrLazy(bridge, "config", [](compiler::VMApi &a) { modules::registerConfigModule(a); });
    tryPluginOrLazy(bridge, "window", [](compiler::VMApi &a) { modules::registerWindowMonitorModule(a); });
    tryPluginOrLazy(bridge, "display", [](compiler::VMApi &a) { modules::registerDisplayModule(a); });
    tryPluginOrLazy(bridge, "help", [](compiler::VMApi &a) { modules::registerHelpModule(a); });
    tryPluginOrLazy(bridge, "mouse", [](compiler::VMApi &a) { modules::registerMouseModule(a); });
    tryPluginOrLazy(bridge, "automation", [](compiler::VMApi &a) { modules::registerAutomationModule(a); });
#ifdef HAVE_QT_EXTENSION
    tryPluginOrLazy(bridge, "ui", [](compiler::VMApi &a) { modules::registerUIModule(a); });
    tryPluginOrLazy(bridge, "pixel", [](compiler::VMApi &a) { modules::registerPixelModule(a); });
#endif
    tryPluginOrLazy(bridge, "image", [](compiler::VMApi &a) { modules::registerImageModule(a); });
    tryPluginOrLazy(bridge, "media", [](compiler::VMApi &a) { modules::registerMediaModule(a); });
#ifdef HAVE_QT_EXTENSION
    tryPluginOrLazy(bridge, "screenshot", [](compiler::VMApi &a) { modules::registerScreenshotModule(a); });
    tryPluginOrLazy(bridge, "alttab", [](compiler::VMApi &a) { modules::registerAltTabModule(a); });
#endif
    tryPluginOrLazy(bridge, "app", [](compiler::VMApi &a) { modules::registerAppModule(a); });
    tryPluginOrLazy(bridge, "audio", [](compiler::VMApi &a) { modules::registerAudioModule(a); });
    tryPluginOrLazy(bridge, "brightness", [](compiler::VMApi &a) { modules::registerBrightnessModule(a); });
    tryPluginOrLazy(bridge, "filemanager", [](compiler::VMApi &a) { modules::registerFileManagerModule(a); });
    tryPluginOrLazy(bridge, "io", [](compiler::VMApi &a) { modules::registerIOModule(a); });
    tryPluginOrLazy(bridge, "mapmanager", [](compiler::VMApi &a) { modules::registerMapManagerModule(a); });
    tryPluginOrLazy(bridge, "mode", [](compiler::VMApi &a) { modules::registerModeModule(a); });
#ifdef HAVE_QT_EXTENSION
    tryPluginOrLazy(bridge, "clipboard", [](compiler::VMApi &a) { modules::registerClipboardModule(a); });
    tryPluginOrLazy(bridge, "historyclipboard", [](compiler::VMApi &a) { modules::registerHistoryClipboardModule(a); });
    tryPluginOrLazy(bridge, "monitoringclipboard", [](compiler::VMApi &a) { modules::registerMonitoringClipboardModule(a); });
#endif
    tryPluginOrLazy(bridge, "ffi", [](compiler::VMApi &a) { modules::ffi::registerFFIModule(a); });
#endif
}


} // namespace havel
