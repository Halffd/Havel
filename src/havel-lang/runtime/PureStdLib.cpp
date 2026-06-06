#include "havel-lang/compiler/vm/VMApi.hpp"
#include "dl/Loader.hpp"
#include "c/ModulePlugin.h"
#include "host/ServiceRegistry.hpp"

namespace havel {

namespace {

static Loader &sharedLoader() {
 static Loader loader;
 static std::once_flag flag;
 std::call_once(flag, [&]() { loader.addModulePaths(); });
 return loader;
}

void registerLazyFromPlugin(compiler::VM &vm, const std::string &name,
 const std::vector<std::string> &aliases = {}) {
 vm.registerLazyModule(name, [name](compiler::VMApi &a) {
 auto plugin = sharedLoader().loadModulePlugin(name);
 if (plugin) {
 plugin->register_fn(static_cast<void *>(&a));
 }
 }, aliases);
}

void registerStdLibSet(compiler::VM &vm, bool coreOnly) {
 compiler::VMApi api(vm);
 api.serviceRegistry = &host::ServiceRegistry::instance();
    vm.setServiceRegistry(&host::ServiceRegistry::instance());
    vm.setPluginLoader(&sharedLoader());

    auto available = sharedLoader().scanModules();

 for (auto &mod : available) {
 if (mod.eager) {
 auto plugin = sharedLoader().loadModulePlugin(mod.name);
 if (plugin) {
 plugin->register_fn(static_cast<void *>(&api));
 }
 } else if (!coreOnly) {
 registerLazyFromPlugin(vm, mod.name, mod.aliases);
 }
    }
}

}

void registerPureStdLib(compiler::VM &vm) {
    registerStdLibSet(vm, false);
}

void registerCoreStdLib(compiler::VM &vm) {
    registerStdLibSet(vm, true);
}

}
