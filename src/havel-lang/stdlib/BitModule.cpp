#include "BitModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <cstdint>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

#if defined(_MSC_VER)
#include <intrin.h>
static int popcount64(uint64_t v) { return static_cast<int>(__popcnt64(v)); }
static int ctz64(uint64_t v) {
    unsigned long idx;
    if (!_BitScanForward64(&idx, v)) return 64;
    return static_cast<int>(idx);
}
static int clz64(uint64_t v) {
    unsigned long idx;
    if (!_BitScanReverse64(&idx, v)) return 64;
    return 63 - static_cast<int>(idx);
}
static int parity64(uint64_t v) { return popcount64(v) & 1; }
#else
static int popcount64(uint64_t v) { return __builtin_popcountll(v); }
static int ctz64(uint64_t v) { return v == 0 ? 64 : __builtin_ctzll(v); }
static int clz64(uint64_t v) { return v == 0 ? 64 : __builtin_clzll(v); }
static int parity64(uint64_t v) { return __builtin_parityll(v); }
#endif

static int64_t getInt(const Value &v) {
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
    if (v.isBool()) return v.asBool() ? 1 : 0;
    throw std::runtime_error("bit: expected integer value");
}

void registerBitModule(const VMApi &api) {
    api.registerFunction("bit._popcount", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit._popcount() requires an argument");
        return Value(static_cast<int64_t>(popcount64(
            static_cast<uint64_t>(getInt(args[0])))));
    });

    api.registerFunction("bit._ctz", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit._ctz() requires an argument");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        if (v == 0) return Value(static_cast<int64_t>(-1));
        return Value(static_cast<int64_t>(ctz64(v)));
    });

    api.registerFunction("bit._clz", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit._clz() requires an argument");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        if (v == 0) return Value(static_cast<int64_t>(-1));
        return Value(static_cast<int64_t>(63 - clz64(v)));
    });

    api.registerFunction("bit._parity", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit._parity() requires an argument");
        return Value(static_cast<int64_t>(parity64(
            static_cast<uint64_t>(getInt(args[0])))));
    });

    api.registerFunction("bit._rshift", [](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit._rshift() requires value and shift");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        int s = static_cast<int>(getInt(args[1]));
        return Value(static_cast<int64_t>(s >= 0 ? v >> s : v << (-s)));
    });

    api.registerFunction("bit._and", [](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit._and() requires two arguments");
        return Value(getInt(args[0]) & getInt(args[1]));
    });

    api.registerFunction("bit._or", [](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit._or() requires two arguments");
        return Value(getInt(args[0]) | getInt(args[1]));
    });

    api.registerFunction("bit._xor", [](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit._xor() requires two arguments");
        return Value(getInt(args[0]) ^ getInt(args[1]));
    });

    api.registerFunction("bit._not", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit._not() requires an argument");
        return Value(~getInt(args[0]));
    });

    auto bitObj = api.makeObject();
    api.setField(bitObj, "_popcount", api.makeFunctionRef("bit._popcount"));
    api.setField(bitObj, "_ctz", api.makeFunctionRef("bit._ctz"));
    api.setField(bitObj, "_clz", api.makeFunctionRef("bit._clz"));
    api.setField(bitObj, "_parity", api.makeFunctionRef("bit._parity"));
    api.setField(bitObj, "_rshift", api.makeFunctionRef("bit._rshift"));
    api.setField(bitObj, "_and", api.makeFunctionRef("bit._and"));
    api.setField(bitObj, "_or", api.makeFunctionRef("bit._or"));
    api.setField(bitObj, "_xor", api.makeFunctionRef("bit._xor"));
    api.setField(bitObj, "_not", api.makeFunctionRef("bit._not"));
    api.setGlobal("bit", bitObj);

    auto &vm = api.vm();
    Value exports;
    try {
        exports = vm.loadModule("bit/bit");
    } catch (...) {
    }

    if (exports.isObjectId()) {
        auto *obj = vm.getHeap().object(exports.asObjectId());
        if (obj) {
            for (const auto& [name, value] : *obj) {
                if (name.empty() || name[0] == '_') continue;
                api.setField(bitObj, name, value);
                api.setGlobal(name, value);
            }
        }
    }
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"
extern "C" HAVEL_MODULE_EXPORT void havel_module_register(void *vmapi_ptr);
static const HavelModuleABI havel_mod_abi_bit = {
    HAVEL_MODULE_ABI_VERSION,
    "bit",
    "1.0.0",
    "Bitwise operations stdlib module",
    havel_module_register,
    nullptr,
    {nullptr},
    1
};
extern "C" HAVEL_MODULE_EXPORT const HavelModuleABI *havel_module_info(void) {
    return &havel_mod_abi_bit;
}
extern "C" HAVEL_MODULE_EXPORT void havel_module_register(void *vmapi_ptr) {
    auto *api = static_cast<havel::compiler::VMApi*>(vmapi_ptr);
    if (api) {
        havel::stdlib::registerBitModule(*api);
    }
}
#endif
