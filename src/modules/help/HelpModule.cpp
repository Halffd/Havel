/* HelpModule.cpp - Loader for pure-Havel help sidecar
 * All logic moved to modules/help/help.hv
 * This file only loads the sidecar and registers its exports as globals.
 */
#include "HelpModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

void registerHelpModule(const compiler::VMApi &api) {
    auto &vm = api.vm();

    Value exports;
    try {
        exports = vm.loadModule("help/help");
    } catch (...) {
    }

    if (exports.isObjectId()) {
        auto *obj = vm.getHeap().object(exports.asObjectId());
        if (obj) {
            for (const auto& [name, value] : *obj) {
                if (name.empty() || name[0] == '_') continue;
                api.setGlobal(name, value);
            }
        }
    }
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(help, "1.0.0", "Help system module",
    havel::modules::registerHelpModule(*api);
)
#endif
