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

void registerStdLibWithVM(compiler::HostBridge &bridge) {
    compiler::VMApi api(*bridge.context().vm);
    auto &vm = *bridge.context().vm;

    // Core modules: eager (used by nearly every script)
    stdlib::registerMathModule(api);
    stdlib::registerStringModule(api);
    stdlib::registerObjectModule(api);
    stdlib::registerTypeModule(api);
    stdlib::registerArrayModule(api);

    // Extended stdlib: lazy — init on first use
  vm.registerLazyModule("regex", [](compiler::VMApi &a) { stdlib::registerRegexModule(a); });
  vm.registerLazyModule("time", [](compiler::VMApi &a) { stdlib::registerTimeModule(a); });
    vm.registerLazyModule("timer", [](compiler::VMApi &a) { stdlib::registerTimerModule(a); });
#ifndef HAVEL_PURE_VM
    vm.registerLazyModule("hotkey", [](compiler::VMApi &a) { stdlib::registerHotkeyModule(a); });
#endif
    vm.registerLazyModule("fs", [](compiler::VMApi &a) { stdlib::registerFsModule(a); });
    vm.registerLazyModule("random", [](compiler::VMApi &a) { stdlib::registerRandomModule(a); });
    vm.registerLazyModule("log", [](compiler::VMApi &a) { stdlib::registerLogModule(a); });
    vm.registerLazyModule("debug", [](compiler::VMApi &a) { stdlib::registerDebugModule(a); });
    vm.registerLazyModule("sys", [](compiler::VMApi &a) { stdlib::registerSysModule(a); });
    vm.registerLazyModule("shell", [](compiler::VMApi &a) { stdlib::registerShellModule(a); });
    vm.registerLazyModule("pointer", [](compiler::VMApi &a) { stdlib::registerPointerModule(a); });
    vm.registerLazyModule("format", [](compiler::VMApi &a) { stdlib::registerFormatModule(a); });
    vm.registerLazyModule("pack", [](compiler::VMApi &a) { stdlib::registerPackModule(a); });
    vm.registerLazyModule("bit", [](compiler::VMApi &a) { stdlib::registerBitModule(a); });
    vm.registerLazyModule("option", [](compiler::VMApi &a) { stdlib::registerOptionModule(a); });
    vm.registerLazyModule("bytecodeBuilder", [](compiler::VMApi &a) { stdlib::registerBytecodeBuilderModule(a); });

#ifndef HAVEL_PURE_VM
    // Host modules: lazy
    vm.registerLazyModule("http", [](compiler::VMApi &a) { stdlib::registerHttpModule(a); });
    vm.registerLazyModule("browser", [](compiler::VMApi &a) { stdlib::registerBrowserModule(a); });
    vm.registerLazyModule("config", [](compiler::VMApi &a) { modules::registerConfigModule(a); });
    vm.registerLazyModule("window", [](compiler::VMApi &a) { modules::registerWindowMonitorModule(a); });
    vm.registerLazyModule("display", [](compiler::VMApi &a) { modules::registerDisplayModule(a); });
    vm.registerLazyModule("help", [](compiler::VMApi &a) { modules::registerHelpModule(a); });
    vm.registerLazyModule("mouse", [](compiler::VMApi &a) { modules::registerMouseModule(a); });
    vm.registerLazyModule("automation", [](compiler::VMApi &a) { modules::registerAutomationModule(a); });
#ifdef HAVE_QT_EXTENSION
    vm.registerLazyModule("ui", [](compiler::VMApi &a) { modules::registerUIModule(a); });
    vm.registerLazyModule("pixel", [](compiler::VMApi &a) { modules::registerPixelModule(a); });
#endif
    vm.registerLazyModule("image", [](compiler::VMApi &a) { modules::registerImageModule(a); });
    vm.registerLazyModule("media", [](compiler::VMApi &a) { modules::registerMediaModule(a); });
#ifdef HAVE_QT_EXTENSION
    vm.registerLazyModule("screenshot", [](compiler::VMApi &a) { modules::registerScreenshotModule(a); });
    vm.registerLazyModule("alttab", [](compiler::VMApi &a) { modules::registerAltTabModule(a); });
#endif
    vm.registerLazyModule("app", [](compiler::VMApi &a) { modules::registerAppModule(a); });
    vm.registerLazyModule("audio", [](compiler::VMApi &a) { modules::registerAudioModule(a); });
    vm.registerLazyModule("brightness", [](compiler::VMApi &a) { modules::registerBrightnessModule(a); });
    vm.registerLazyModule("filemanager", [](compiler::VMApi &a) { modules::registerFileManagerModule(a); });
    vm.registerLazyModule("io", [](compiler::VMApi &a) { modules::registerIOModule(a); });
    vm.registerLazyModule("mapmanager", [](compiler::VMApi &a) { modules::registerMapManagerModule(a); });
    vm.registerLazyModule("mode", [](compiler::VMApi &a) { modules::registerModeModule(a); });
#ifdef HAVE_QT_EXTENSION
    vm.registerLazyModule("clipboard", [](compiler::VMApi &a) { modules::registerClipboardModule(a); });
    vm.registerLazyModule("historyclipboard", [](compiler::VMApi &a) { modules::registerHistoryClipboardModule(a); });
    vm.registerLazyModule("monitoringclipboard", [](compiler::VMApi &a) { modules::registerMonitoringClipboardModule(a); });
#endif
    vm.registerLazyModule("ffi", [](compiler::VMApi &a) { modules::ffi::registerFFIModule(a); });
#endif
}


} // namespace havel
