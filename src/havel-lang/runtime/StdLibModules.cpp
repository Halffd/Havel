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
void registerMathModule(const compiler::VMApi &api);
void registerStringModule(const compiler::VMApi &api);
void registerObjectModule(const compiler::VMApi &api);
void registerTypeModule(const compiler::VMApi &api);
void registerRegexModule(const compiler::VMApi &api);
void registerPhysicsModule(const compiler::VMApi &api);
void registerTimeModule(const compiler::VMApi &api);
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
