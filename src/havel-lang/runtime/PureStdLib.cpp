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
void registerArrayModule(const compiler::VMApi &api);
void registerRegexModule(const compiler::VMApi &api);

void registerTimeModule(const compiler::VMApi &api);
void registerTimerModule(const compiler::VMApi &api);
void registerFsModule(const compiler::VMApi &api);
void registerRandomModule(const compiler::VMApi &api);
void registerDebugModule(const compiler::VMApi &api);
void registerSysModule(const compiler::VMApi &api);
void registerShellModule(const compiler::VMApi &api);
void registerPointerModule(const compiler::VMApi &api);
void registerFormatModule(const compiler::VMApi &api);
void registerPackModule(const compiler::VMApi &api);
void registerBitModule(const compiler::VMApi &api);
void registerOptionModule(const compiler::VMApi &api);
void registerBytecodeBuilderModule(const compiler::VMApi &api);
void registerLogModule(const compiler::VMApi &api);
} // namespace havel::stdlib

namespace havel::modules {
void registerWindowMonitorModule(const compiler::VMApi &api);
void registerDisplayModule(const compiler::VMApi &api);
void registerBrightnessModule(const compiler::VMApi &api);
} // namespace havel::modules

namespace havel {

namespace {
void registerLazyStdlib(compiler::VM &vm) {
  vm.registerLazyModule("regex", [](compiler::VMApi &api) { stdlib::registerRegexModule(api); });
  vm.registerLazyModule("time", [](compiler::VMApi &api) { stdlib::registerTimeModule(api); });
    vm.registerLazyModule("timer", [](compiler::VMApi &api) { stdlib::registerTimerModule(api); });
    vm.registerLazyModule("fs", [](compiler::VMApi &api) { stdlib::registerFsModule(api); });
    vm.registerLazyModule("random", [](compiler::VMApi &api) { stdlib::registerRandomModule(api); });
    vm.registerLazyModule("debug", [](compiler::VMApi &api) { stdlib::registerDebugModule(api); });
    vm.registerLazyModule("sys", [](compiler::VMApi &api) { stdlib::registerSysModule(api); });
    vm.registerLazyModule("shell", [](compiler::VMApi &api) { stdlib::registerShellModule(api); });
    vm.registerLazyModule("pointer", [](compiler::VMApi &api) { stdlib::registerPointerModule(api); });
    vm.registerLazyModule("format", [](compiler::VMApi &api) { stdlib::registerFormatModule(api); });
    vm.registerLazyModule("pack", [](compiler::VMApi &api) { stdlib::registerPackModule(api); });
    vm.registerLazyModule("bit", [](compiler::VMApi &api) { stdlib::registerBitModule(api); });
    vm.registerLazyModule("option", [](compiler::VMApi &api) { stdlib::registerOptionModule(api); });
    vm.registerLazyModule("bytecodeBuilder", [](compiler::VMApi &api) { stdlib::registerBytecodeBuilderModule(api); });
    vm.registerLazyModule("log", [](compiler::VMApi &api) { stdlib::registerLogModule(api); });
    vm.registerLazyModule("window", [](compiler::VMApi &api) { modules::registerWindowMonitorModule(api); });
    vm.registerLazyModule("display", [](compiler::VMApi &api) { modules::registerDisplayModule(api); });
    vm.registerLazyModule("brightness", [](compiler::VMApi &api) { modules::registerBrightnessModule(api); });
}

void registerStdLibSet(compiler::VM &vm, bool coreOnly) {
    compiler::VMApi api(vm);

    // Core modules: always eager (used by nearly every script)
    stdlib::registerMathModule(api);
    stdlib::registerStringModule(api);
    stdlib::registerObjectModule(api);
    stdlib::registerTypeModule(api);
    stdlib::registerArrayModule(api);

    if (coreOnly) {
        return;
    }

    // Extended modules: lazy — init on first use
    registerLazyStdlib(vm);
}
} // namespace

void registerPureStdLib(compiler::VM &vm) {
    registerStdLibSet(vm, false);
}

void registerCoreStdLib(compiler::VM &vm) {
    registerStdLibSet(vm, true);
}

} // namespace havel
