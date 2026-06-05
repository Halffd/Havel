#include "havel-lang/compiler/runtime/HostBridge.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "loader/Loader.hpp"
#include "c/ModulePlugin.h"

namespace havel {

static void registerLazyFromPlugin(compiler::HostBridge &bridge, const char *name) {
    auto &vm = *bridge.context().vm;
    std::string modName(name);
    vm.registerLazyModule(modName, [&bridge, modName](compiler::VMApi &a) {
        auto plugin = bridge.extensionLoader().loadModulePlugin(modName);
        if (plugin) {
            plugin->register_fn(static_cast<void *>(&a));
        }
    });
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

    static const char *lazyModules[] = {
        "regex", "time", "timer",
        "hotkey",
        "fs", "random", "log", "sys", "shell",
        "ptr", "fmt", "pack", "bit", "option", "bytecodebuilder",
        "http", "browser",
        "config", "window", "display", "help", "mouse", "automation",
        "image", "media", "app", "audio", "brightness", "filemanager",
        "io", "mapmanager", "mode", "ffi", "zoom",
        "alttab", "clipboard", "historyclipboard", "monitoringclipboard",
    };

    for (auto name : lazyModules) {
        registerLazyFromPlugin(bridge, name);
    }
}

}
