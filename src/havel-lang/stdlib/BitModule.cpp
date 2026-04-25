#include "BitModule.hpp"
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

void registerBitModule(VMApi &api) {
    api.registerFunction("bit.and", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.and() requires two arguments");
        return Value(getInt(args[0]) & getInt(args[1]));
    });

    api.registerFunction("bit.or", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.or() requires two arguments");
        return Value(getInt(args[0]) | getInt(args[1]));
    });

    api.registerFunction("bit.xor", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.xor() requires two arguments");
        return Value(getInt(args[0]) ^ getInt(args[1]));
    });

    api.registerFunction("bit.not", [&api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit.not() requires an argument");
        return Value(~getInt(args[0]));
    });

    api.registerFunction("bit.lshift", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.lshift() requires value and shift");
        int64_t v = getInt(args[0]);
        int s = static_cast<int>(getInt(args[1]));
        return Value(s >= 0 ? v << s : v >> (-s));
    });

    api.registerFunction("bit.rshift", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.rshift() requires value and shift");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        int s = static_cast<int>(getInt(args[1]));
        return Value(static_cast<int64_t>(s >= 0 ? v >> s : v << (-s)));
    });

    api.registerFunction("bit.arshift", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.arshift() requires value and shift");
        int64_t v = getInt(args[0]);
        int s = static_cast<int>(getInt(args[1]));
        return Value(s >= 0 ? v >> s : v << (-s));
    });

    api.registerFunction("bit.test", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.test() requires value and position");
        int64_t v = getInt(args[0]);
        int p = static_cast<int>(getInt(args[1]));
        if (p < 0 || p >= 64) return Value(false);
        return Value((v >> p) & 1);
    });

    api.registerFunction("bit.set", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.set() requires value and position");
        int64_t v = getInt(args[0]);
        int p = static_cast<int>(getInt(args[1]));
        if (p < 0 || p >= 64) throw std::runtime_error("bit.set(): position out of range");
        return Value(v | (int64_t(1) << p));
    });

    api.registerFunction("bit.clear", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.clear() requires value and position");
        int64_t v = getInt(args[0]);
        int p = static_cast<int>(getInt(args[1]));
        if (p < 0 || p >= 64) throw std::runtime_error("bit.clear(): position out of range");
        return Value(v & ~(int64_t(1) << p));
    });

    api.registerFunction("bit.toggle", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.toggle() requires value and position");
        int64_t v = getInt(args[0]);
        int p = static_cast<int>(getInt(args[1]));
        if (p < 0 || p >= 64) throw std::runtime_error("bit.toggle(): position out of range");
        return Value(v ^ (int64_t(1) << p));
    });

    api.registerFunction("bit.lsb", [&api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit.lsb() requires an argument");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        if (v == 0) return Value(static_cast<int64_t>(-1));
        return Value(static_cast<int64_t>(ctz64(v)));
    });

    api.registerFunction("bit.msb", [&api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit.msb() requires an argument");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        if (v == 0) return Value(static_cast<int64_t>(-1));
        return Value(static_cast<int64_t>(63 - clz64(v)));
    });

    api.registerFunction("bit.count", [&api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit.count() requires an argument");
        return Value(static_cast<int64_t>(popcount64(
            static_cast<uint64_t>(getInt(args[0])))));
    });

    api.registerFunction("bit.parity", [&api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("bit.parity() requires an argument");
        return Value(static_cast<int64_t>(parity64(
            static_cast<uint64_t>(getInt(args[0])))));
    });

    api.registerFunction("bit.rol", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.rol() requires value and shift");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        int s = static_cast<int>(getInt(args[1])) & 63;
        if (s == 0) return Value(static_cast<int64_t>(v));
        return Value(static_cast<int64_t>((v << s) | (v >> (64 - s))));
    });

    api.registerFunction("bit.ror", [&api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("bit.ror() requires value and shift");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        int s = static_cast<int>(getInt(args[1])) & 63;
        if (s == 0) return Value(static_cast<int64_t>(v));
        return Value(static_cast<int64_t>((v >> s) | (v << (64 - s))));
    });

    api.registerFunction("bit.getfield", [&api](const std::vector<Value> &args) {
        if (args.size() < 3)
            throw std::runtime_error("bit.getfield() requires value, position, and width");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        int pos = static_cast<int>(getInt(args[1]));
        int w = static_cast<int>(getInt(args[2]));
        if (pos < 0 || pos >= 64 || w <= 0 || pos + w > 64)
            throw std::runtime_error("bit.getfield(): position or width out of range");
        uint64_t mask = (w >= 64) ? ~uint64_t(0) : (uint64_t(1) << w) - 1;
        return Value(static_cast<int64_t>((v >> pos) & mask));
    });

    api.registerFunction("bit.setfield", [&api](const std::vector<Value> &args) {
        if (args.size() < 4)
            throw std::runtime_error("bit.setfield() requires value, position, width, and newval");
        uint64_t v = static_cast<uint64_t>(getInt(args[0]));
        int pos = static_cast<int>(getInt(args[1]));
        int w = static_cast<int>(getInt(args[2]));
        uint64_t nv = static_cast<uint64_t>(getInt(args[3]));
        if (pos < 0 || pos >= 64 || w <= 0 || pos + w > 64)
            throw std::runtime_error("bit.setfield(): position or width out of range");
        uint64_t mask = (w >= 64) ? ~uint64_t(0) : (uint64_t(1) << w) - 1;
        v &= ~(mask << pos);
        v |= (nv & mask) << pos;
        return Value(static_cast<int64_t>(v));
    });
}

} // namespace havel::stdlib
