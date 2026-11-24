#include "ModModule.hpp"

#include "havel/mod.h"
#include "modding/ModManager.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using havel::compiler::Value;
using havel::compiler::VMApi;
using havel::modding::ModManager;
using havel::modding::ModInfo;
using havel::modding::HookInfo;

namespace havel::stdlib {

/* Both string flavours a script can hand a host function: a literal
 * (isStringValId, stored in the bytecode chunk's constant pool) and a
 * computed/interpolated string (isStringId, a runtime heap entry).
 * resolveString() covers both. Accepting only isStringId() here rejected
 * every plain "name" literal with "must be a string". */
static bool isStringValue(const Value &v) {
    return v.isStringId() || v.isStringValId();
}

static std::string requireString(const VMApi &api, const std::vector<Value> &args,
                                 size_t i, const char *what) {
    if (args.size() <= i)
        throw std::runtime_error(std::string(what) + ": argument " + std::to_string(i + 1) +
                                                         " is required");
    const Value &v = args[i];
    if (!isStringValue(v))
        throw std::runtime_error(std::string(what) + ": argument " + std::to_string(i + 1) +
                                 " must be a string");
    /* api.resolveString, not Value::toString: an interpolated or sliced
     * string is a runtime string ID whose own toString() prints the
     * defensive "<string:N>" placeholder. Every path built by interpolation
     * would then fail "No such file or directory <string:N>". */
    return api.resolveString(v);
}

static void *pointerArg(const std::vector<Value> &args, size_t i, const char *what) {
    if (args.size() <= i)
        throw std::runtime_error(std::string(what) + ": argument " + std::to_string(i + 1) +
                                 " is required");
    const Value &v = args[i];
    if (v.isPtr()) return v.asPtr();
    if (v.isInt()) return reinterpret_cast<void *>(static_cast<uint64_t>(v.asInt()));
    throw std::runtime_error(std::string(what) + ": argument " + std::to_string(i + 1) +
                             " must be a pointer or an integer address");
}

static Value modInfoValue(const VMApi &api, const ModInfo &info) {
    Value obj = api.makeObject();
    api.setField(obj, "name", api.makeString(info.name));
    api.setField(obj, "path", api.makeString(info.path));
    api.setField(obj, "loadedPath", api.makeString(info.loadedPath));
    api.setField(obj, "generation", api.makeNumber(static_cast<int64_t>(info.generation)));
    return obj;
}

static Value hookInfoValue(const VMApi &api, const HookInfo &h) {
    Value obj = api.makeObject();
    api.setField(obj, "symbol", api.makeString(h.symbol));
    api.setField(obj, "target", Value::makePtr(h.target));
    api.setField(obj, "replacement", Value::makePtr(h.replacement));
    api.setField(obj, "trampoline", Value::makePtr(h.trampoline));
    api.setField(obj, "mod", h.modName.empty() ? api.makeString("")
                                               : api.makeString(h.modName));
    api.setField(obj, "targetMod", h.targetMod.empty() ? api.makeString("")
                                                       : api.makeString(h.targetMod));
    return obj;
}

void registerModModule(const VMApi &api) {
    auto &mgr = ModManager::instance();

    api.registerFunction("mod.load", [&mgr, api](const std::vector<Value> &args) {
        std::string path = requireString(api, args, 0, "mod.load");
        bool globalScope = true;
        if (args.size() > 1 && args[1].isObjectId()) {
            Value g = api.getField(args[1], "global");
            if (g.isBool()) globalScope = g.asBool();
        }
        auto info = mgr.load(path, globalScope);
        return modInfoValue(api, info);
    });

    api.registerFunction("mod.reload", [&mgr, api](const std::vector<Value> &args) {
        std::string name = requireString(api, args, 0, "mod.reload");
        auto info = mgr.reload(name);
        return modInfoValue(api, info);
    });

    api.registerFunction("mod.unload", [&mgr, api](const std::vector<Value> &args) {
        std::string name = requireString(api, args, 0, "mod.unload");
        return Value(mgr.unload(name));
    });

    api.registerFunction("mod.list", [&mgr, api](const std::vector<Value> &args) {
        (void)args;
        Value arr = api.makeArray();
        for (const auto &info : mgr.list())
            api.push(arr, modInfoValue(api, info));
        return arr;
    });

    /* mod.sym(modName, symbol) -> pointer, or null when the symbol does not
     * resolve. modName "" means the global namespace plus every loaded mod. */
    api.registerFunction("mod.sym", [&mgr, api](const std::vector<Value> &args) {
        std::string modName = args.size() > 0 && isStringValue(args[0]) ? api.resolveString(args[0]) : "";
        std::string symbol = args.size() > 1 && isStringValue(args[1]) ? api.resolveString(args[1]) : "";
        if (symbol.empty())
            throw std::runtime_error("mod.sym: a symbol name is required");
        void *s = mgr.resolve(modName, symbol);
        if (!s) return Value::makeNull();
        return Value::makePtr(s);
    });

    /* mod.hook(symbol, replacement) installs a detour and returns the
     * trampoline - a callable pointer to the ORIGINAL function. Call it from
     * the ffi module or pass it back into a C function; it is not a Havel
     * value. The detour is per-symbol: hooking twice is an error, and the
     * replacement must match the target's calling convention. */
    api.registerFunction("mod.hook", [&mgr, api](const std::vector<Value> &args) {
        std::string symbol = requireString(api, args, 0, "mod.hook");
        void *replacement = pointerArg(args, 1, "mod.hook");
        std::string owner;
        if (args.size() > 2 && args[2].isStringId()) owner = args[2].toString();
        void *trampoline = mgr.hook(symbol, replacement, owner);
        (void)api;
        return Value::makePtr(trampoline);
    });

    api.registerFunction("mod.unhook", [&mgr, api](const std::vector<Value> &args) {
        std::string symbol = requireString(api, args, 0, "mod.unhook");
        return Value(mgr.unhook(symbol));
    });

    api.registerFunction("mod.hooks", [&mgr, api](const std::vector<Value> &args) {
        (void)args;
        Value arr = api.makeArray();
        for (const auto &h : mgr.hooks())
            api.push(arr, hookInfoValue(api, h));
        return arr;
    });

    auto modObj = api.makeObject();
    api.setField(modObj, "load", api.makeFunctionRef("mod.load"));
    api.setField(modObj, "reload", api.makeFunctionRef("mod.reload"));
    api.setField(modObj, "unload", api.makeFunctionRef("mod.unload"));
    api.setField(modObj, "list", api.makeFunctionRef("mod.list"));
    api.setField(modObj, "sym", api.makeFunctionRef("mod.sym"));
    api.setField(modObj, "hook", api.makeFunctionRef("mod.hook"));
    api.setField(modObj, "unhook", api.makeFunctionRef("mod.unhook"));
    api.setField(modObj, "hooks", api.makeFunctionRef("mod.hooks"));
    api.setGlobal("mod", modObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(mod, "1.0.0", "Native mod loading and hot reload",
    havel::stdlib::registerModModule(*api);
)
#endif