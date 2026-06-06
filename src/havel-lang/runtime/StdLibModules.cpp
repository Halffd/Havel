#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "loader/Loader.hpp"
#include "c/ModulePlugin.h"

namespace havel {

void registerStdLibWithVM(compiler::HostBridge &bridge) {
 compiler::VMApi api(*bridge.context().vm);
 auto &loader = bridge.extensionLoader();
 auto &vm = *bridge.context().vm;

 loader.addModulePaths();

 auto available = loader.scanModules();

 for (auto &mod : available) {
 if (mod.eager) {
 auto plugin = loader.loadModulePlugin(mod.name);
 if (plugin) {
 plugin->register_fn(static_cast<void *>(&api));
 }
 } else {
 std::string modName = mod.name;
 vm.registerLazyModule(modName, [&loader, modName](compiler::VMApi &a) {
 auto plugin = loader.loadModulePlugin(modName);
 if (plugin) {
 plugin->register_fn(static_cast<void *>(&a));
 }
 }, mod.aliases);
 }
 }

}

}
