// StdLibModules.cpp
// Register all standard library modules with VM

#ifndef HAVEL_PURE_VM
#include "../../modules/automation/AutomationModule.hpp"
#include "../../modules/automation/PixelModule.hpp"
#include "../../modules/config/ConfigModule.hpp"
#include "../../modules/ffi/FFIModule.hpp"
#include "../../modules/help/HelpModule.hpp"
#include "../../modules/hotkey/HotkeyModule.hpp"
#include "../../modules/image/ImageModule.hpp"
#include "../../modules/media/MediaModule.hpp"
#include "../../modules/mouse/MouseModule.hpp"
#include "../../modules/screenshot/ScreenshotModule.hpp"
#include "../../modules/ui/UIModule.hpp"
#include "../../modules/window/WindowMonitorModule.hpp"
#endif

#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {
void registerMathModule(compiler::VMApi &api);
void registerStringModule(compiler::VMApi &api);
void registerObjectModule(compiler::VMApi &api);
void registerTypeModule(compiler::VMApi &api);
void registerRegexModule(compiler::VMApi &api);
void registerPhysicsModule(compiler::VMApi &api);
void registerTimeModule(compiler::VMApi &api);
#ifndef HAVEL_PURE_VM
void registerHotkeyModule(compiler::VMApi &api);
#endif
void registerFsModule(compiler::VMApi &api);
void registerRandomModule(compiler::VMApi &api);
void registerLogModule(compiler::VMApi &api);
void registerDebugModule(compiler::VMApi &api);
void registerSysModule(compiler::VMApi &api);
void registerShellModule(compiler::VMApi &api);
void registerPointerModule(compiler::VMApi &api);
void registerFormatModule(compiler::VMApi &api);
void registerPackModule(compiler::VMApi &api);
void registerBitModule(compiler::VMApi &api);
#ifndef HAVEL_PURE_VM
void registerHttpModule(compiler::VMApi &api);
void registerBrowserModule(compiler::VMApi &api);
#endif
} // namespace havel::stdlib

namespace havel {

void registerStdLibWithVM(compiler::HostBridge &bridge) {
  compiler::VMApi api(*bridge.context().vm);

  stdlib::registerMathModule(api);
  stdlib::registerStringModule(api);
  stdlib::registerObjectModule(api);
  stdlib::registerTypeModule(api);
  stdlib::registerRegexModule(api);
  stdlib::registerPhysicsModule(api);
  stdlib::registerTimeModule(api);
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

#ifndef HAVEL_PURE_VM
  stdlib::registerHttpModule(api);
  stdlib::registerBrowserModule(api);
  modules::registerConfigModule(api);
  modules::registerWindowMonitorModule(api);
  modules::registerHelpModule(api);
  modules::registerMouseModule(api);
  modules::registerUIModule(api);
  modules::registerAutomationModule(api);
  modules::registerPixelModule(api);
  modules::registerImageModule(api);
  modules::registerMediaModule(api);
  modules::registerScreenshotModule(api);
  modules::ffi::registerFFIModule(api);
#endif
}

void registerPureStdLib(compiler::VM &vm) {
  compiler::VMApi api(vm);

  // Register PURE stdlib modules (no OS access)
  stdlib::registerMathModule(api);
  stdlib::registerStringModule(api);
  stdlib::registerObjectModule(api);
  stdlib::registerTypeModule(api);
  stdlib::registerRegexModule(api);
  stdlib::registerPhysicsModule(api);
  stdlib::registerTimeModule(api);
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
}

} // namespace havel
