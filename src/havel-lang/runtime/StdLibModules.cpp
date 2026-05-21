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
void registerPhysicsModule(const compiler::VMApi &api);
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

  stdlib::registerMathModule(api);
  stdlib::registerStringModule(api);
  stdlib::registerObjectModule(api);
  stdlib::registerTypeModule(api);
  stdlib::registerArrayModule(api);
  stdlib::registerRegexModule(api);
  stdlib::registerPhysicsModule(api);
  stdlib::registerTimeModule(api);
  stdlib::registerTimerModule(api);
#ifndef HAVEL_PURE_VM
  stdlib::registerHotkeyModule(api);
#endif
  stdlib::registerFsModule(api);
  stdlib::registerRandomModule(api);
  stdlib::registerLogModule(api);
  stdlib::registerDebugModule(api);
  stdlib::registerSysModule(api);
  stdlib::registerShellModule(api);
  stdlib::registerPointerModule(api);
  stdlib::registerFormatModule(api);
  stdlib::registerPackModule(api);
  stdlib::registerBitModule(api);
    stdlib::registerOptionModule(api);
    stdlib::registerBytecodeBuilderModule(api);

#ifndef HAVEL_PURE_VM
  stdlib::registerHttpModule(api);
  stdlib::registerBrowserModule(api);
  modules::registerConfigModule(api);
  modules::registerWindowMonitorModule(api);
  modules::registerHelpModule(api);
  modules::registerMouseModule(api);
  modules::registerAutomationModule(api);
#ifdef HAVE_QT_EXTENSION
  modules::registerUIModule(api);
  modules::registerPixelModule(api);
#endif
  modules::registerImageModule(api);
  modules::registerMediaModule(api);
#ifdef HAVE_QT_EXTENSION
  modules::registerScreenshotModule(api);
  modules::registerAltTabModule(api);
#endif
  modules::registerAppModule(api);
  modules::registerAudioModule(api);
  modules::registerBrightnessModule(api);
  modules::registerFileManagerModule(api);
  modules::registerIOModule(api);
  modules::registerMapManagerModule(api);
  modules::registerModeModule(api);
#ifdef HAVE_QT_EXTENSION
  modules::registerClipboardModule(api);
  modules::registerHistoryClipboardModule(api);
  modules::registerMonitoringClipboardModule(api);
#endif
  modules::ffi::registerFFIModule(api);
#endif
}


} // namespace havel
