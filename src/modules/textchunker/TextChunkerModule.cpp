/* TextChunkerModule.cpp - Loader for pure-Havel textchunker sidecar
 * All logic moved to modules/textchunker/textchunker.hv
 * This file only loads the sidecar and merges its exports.
 */
#include "TextChunkerModule.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static void mergeExports(const VMApi &api, Value targetObj, Value exports) {
    auto &vm = api.vm();
    if (!exports.isObjectId()) return;
    auto *obj = vm.getHeap().object(exports.asObjectId());
    if (!obj) return;
    for (const auto& [name, value] : *obj) {
        if (name.empty() || name[0] == '_') continue;
        api.setField(targetObj, name, value);
        api.setGlobal(name, value);
    }
}

void registerTextChunkerModule(const compiler::VMApi &api) {
    auto &vm = api.vm();

    auto chunkerObj = api.makeObject();
    api.setGlobal("textChunker", chunkerObj);

    Value exports;
    try {
        exports = vm.loadModule("textchunker/textchunker");
    } catch (...) {
    }
    mergeExports(api, chunkerObj, exports);
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(textchunker, "1.0.0", "Text chunker module",
    havel::modules::registerTextChunkerModule(*api);
)
#endif
