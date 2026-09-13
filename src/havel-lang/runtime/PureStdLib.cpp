#include "havel-lang/compiler/vm/VMApi.hpp"
#include "dl/Loader.hpp"
#include "c/ModulePlugin.h"
#include "host/ServiceRegistry.hpp"
#include "havel-lang/stdlib/MathModule.hpp"

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

    // Math registers directly, NOT via the lazy plugin fallback. The math
    // sidecar loadModule("math/math") must run while no other module load is
    // in progress: math/math.hv itself calls math.random(), which used to
    // trigger math plugin registration from inside that module's load and
    // hit the circular-dependency guard — silently dropping randint/clamp/
    // lerp from the math namespace. Registered here (before any script
    // module load) the sidecar loads cleanly.
    {
        compiler::VMApi mathApi(vm);
        havel::stdlib::registerMathModule(mathApi);
    }

    auto available = sharedLoader().scanModules();

 for (auto &mod : available) {
        // math already registered above; re-registering from the plugin
        // would re-run the sidecar load and can re-enter mid-load.
        if (mod.name == "math") continue;
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
