/* FormatModule.cpp - C++ shim for fmt module
 * Base64 encode/decode stays in C++ (byte-level ops).
 * hex/oct/bin/format are loaded from pure-Havel sidecar.
 * Pointer-based hex also stays in C++.
 */
#include "FormatModule.hpp"
#include "../compiler/vm/VM.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static const char B64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const uint8_t *data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out += B64_TABLE[(n >> 18) & 0x3F];
        out += B64_TABLE[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? B64_TABLE[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? B64_TABLE[n & 0x3F] : '=';
    }
    return out;
}

static std::vector<uint8_t> base64Decode(const std::string &in) {
    static const int8_t d64[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (d64[c] == -1) break;
        val = (val << 6) + d64[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string intToBase(int64_t val, int base, const std::string &prefix) {
    if (val == 0) return prefix + "0";
    uint64_t uval = (val < 0 && base == 16)
        ? static_cast<uint64_t>(val)
        : static_cast<uint64_t>(std::abs(val));
    std::string digits;
    const char *table = "0123456789abcdef";
    while (uval > 0) {
        digits += table[uval % base];
        uval /= base;
    }
    std::reverse(digits.begin(), digits.end());
    if (val < 0 && base != 16) digits = "-" + digits;
    return prefix + digits;
}

void registerFormatModule(const VMApi &api) {
    // C++ shim: b64 encode/decode (byte-level ops, pointer hex)
    api.registerFunction("fmt.b64", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("b64() requires a string or array");
        const auto &v = args[0];
        if (v.isStringValId() || v.isStringId()) {
            std::string s = api.toString(v);
            std::string encoded = base64Encode(
                reinterpret_cast<const uint8_t *>(s.data()), s.size());
            return api.makeString(std::move(encoded));
        }
        if (v.isArrayId()) {
            uint32_t len = api.length(v);
            std::vector<uint8_t> bytes;
            bytes.reserve(len);
            for (uint32_t i = 0; i < len; ++i) {
                Value elem = api.getAt(v, i);
                if (elem.isInt())
                    bytes.push_back(static_cast<uint8_t>(elem.asInt() & 0xFF));
                else
                    bytes.push_back(0);
            }
            std::string encoded = base64Encode(bytes.data(), bytes.size());
            return api.makeString(std::move(encoded));
        }
        throw std::runtime_error("b64() expects a string or byte array");
    });

    api.registerFunction("fmt.b64decode", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("b64decode() requires a string");
        const auto &v = args[0];
        if (!v.isStringValId() && !v.isStringId())
            throw std::runtime_error("b64decode() expects a string");
        std::string s = api.toString(v);
        auto decoded = base64Decode(s);
        Value arr = api.makeArray();
        for (auto b : decoded)
            api.push(arr, Value(static_cast<int64_t>(b)));
        return arr;
    });

    // C++ shim: pointer-based hex (can't do reinterpret_cast in Havel)
    api.registerFunction("fmt._hexPtr", [api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("_hexPtr() requires a value");
        const auto &v = args[0];
        if (v.isPtr()) {
            uint64_t addr = reinterpret_cast<uint64_t>(v.asPtr());
            return api.makeString(intToBase(static_cast<int64_t>(addr), 16, "0x"));
        }
        if (v.isInt())
            return api.makeString(intToBase(v.asInt(), 16, "0x"));
        if (v.isDouble())
            return api.makeString(intToBase(
                static_cast<int64_t>(v.asDouble()), 16, "0x"));
        throw std::runtime_error("_hexPtr() expects a pointer or number");
    });

    // Build namespace object with C++ functions
    auto fmtObj = api.makeObject();
    api.setField(fmtObj, "b64", api.makeFunctionRef("fmt.b64"));
    api.setField(fmtObj, "b64decode", api.makeFunctionRef("fmt.b64decode"));
    api.setField(fmtObj, "_hexPtr", api.makeFunctionRef("fmt._hexPtr"));
    api.setGlobal("fmt", fmtObj);

    // Load pure-Havel sidecar for hex/oct/bin
    auto &vm = api.vm();
    Value exports;
    try {
        exports = vm.loadModule("fmt/fmt");
    } catch (...) {
    }

    if (exports.isObjectId()) {
        auto *obj = vm.getHeap().object(exports.asObjectId());
        if (obj) {
            for (const auto& [name, value] : *obj) {
                if (name.empty() || name[0] == '_') continue;
                api.setField(fmtObj, name, value);
                api.setGlobal(name, value);
            }
        }
    } else {
    }
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(fmt, "1.0.0", "String formatting stdlib module",
    havel::stdlib::registerFormatModule(*api);
)
#endif
