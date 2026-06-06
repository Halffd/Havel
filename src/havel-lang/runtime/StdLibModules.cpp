#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "loader/Loader.hpp"
#include "c/ModulePlugin.h"

namespace havel {

static void registerLazyFromPlugin(compiler::HostBridge &bridge, const char *name, const std::vector<std::string> &aliases = {}) {
    auto &vm = *bridge.context().vm;
    std::string modName(name);
    vm.registerLazyModule(modName, [&bridge, modName](compiler::VMApi &a) {
        auto plugin = bridge.extensionLoader().loadModulePlugin(modName);
        if (plugin) {
            plugin->register_fn(static_cast<void *>(&a));
        }
    }, aliases);
}

void registerStdLibWithVM(compiler::HostBridge &bridge) {
    compiler::VMApi api(*bridge.context().vm);

    bridge.extensionLoader().addModulePaths();

    static const char *eagerModules[] = {
        "math", "string", "object", "type", "array",
    };

    for (auto name : eagerModules) {
        auto plugin = bridge.extensionLoader().loadModulePlugin(name);
        if (plugin) {
            plugin->register_fn(static_cast<void *>(&api));
        }
    }

    static const struct { const char *name; const char *aliases[8]; } lazyModules[] = {
        {"regex", {}},
        {"time", {}},
        {"timer", {}},
        {"hotkey", {"Hotkey"}},
        {"fs", {}},
        {"random", {}},
        {"log", {"debug"}},
        {"sys", {"system", "process", "jit"}},
        {"shell", {}},
        {"ptr", {}},
        {"fmt", {}},
        {"pack", {}},
        {"bit", {}},
        {"option", {}},
        {"bytecodebuilder", {"bc"}},
        {"http", {}},
        {"browser", {}},
        {"config", {"cfg", "conf"}},
        {"window", {}},
        {"display", {}},
        {"help", {}},
        {"mouse", {}},
        {"automation", {"pixel"}},
        {"image", {}},
        {"media", {}},
        {"app", {}},
        {"audio", {}},
        {"brightness", {}},
        {"filemanager", {}},
        {"io", {}},
        {"mapmanager", {}},
        {"mode", {}},
        {"ffi", {}},
        {"zoom", {}},
        {"alttab", {}},
        {"clipboard", {}},
        {"historyclipboard", {"clipboardHistory"}},
        {"monitoringclipboard", {"clipboardMonitor"}},
    };

    for (auto &entry : lazyModules) {
        std::vector<std::string> aliases;
        for (int i = 0; i < 8 && entry.aliases[i]; ++i) {
            aliases.emplace_back(entry.aliases[i]);
        }
        registerLazyFromPlugin(bridge, entry.name, aliases);
    }
}

}
