// PureStdLib.cpp
// Register ONLY pure stdlib modules with VM
// Separated from StdLibModules.cpp to avoid pulling in
// OS-dependent module symbols (havel_modules) when only
// pure stdlib registration is needed (e.g., hvtest).

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::stdlib {
void registerMathModule(const compiler::VMApi &api);
void registerStringModule(const compiler::VMApi &api);
void registerObjectModule(const compiler::VMApi &api);
void registerTypeModule(const compiler::VMApi &api);
void registerRegexModule(const compiler::VMApi &api);
void registerPhysicsModule(const compiler::VMApi &api);
void registerTimeModule(const compiler::VMApi &api);
void registerTimerModule(const compiler::VMApi &api);
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
} // namespace havel::stdlib

namespace havel {

void registerPureStdLib(compiler::VM &vm) {
  compiler::VMApi api(vm);

  stdlib::registerMathModule(api);
  stdlib::registerStringModule(api);
  stdlib::registerObjectModule(api);
  stdlib::registerTypeModule(api);
  stdlib::registerRegexModule(api);
  stdlib::registerPhysicsModule(api);
  stdlib::registerTimeModule(api);
  stdlib::registerTimerModule(api);
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
}

} // namespace havel
