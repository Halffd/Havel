#include "PackModule.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <string>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

enum class Endian { LITTLE, BIG, NATIVE };

static Endian nativeEndian() {
    uint16_t test = 0x0001;
    return *reinterpret_cast<uint8_t *>(&test) ? Endian::LITTLE : Endian::BIG;
}

static void writeU16(std::vector<uint8_t> &out, uint16_t v, Endian e) {
    if (e == Endian::LITTLE) {
        out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF);
    } else {
        out.push_back((v >> 8) & 0xFF); out.push_back(v & 0xFF);
    }
}
static void writeU32(std::vector<uint8_t> &out, uint32_t v, Endian e) {
    if (e == Endian::LITTLE) {
        out.push_back(v & 0xFF); out.push_back((v >> 8) & 0xFF);
        out.push_back((v >> 16) & 0xFF); out.push_back((v >> 24) & 0xFF);
    } else {
        out.push_back((v >> 24) & 0xFF); out.push_back((v >> 16) & 0xFF);
        out.push_back((v >> 8) & 0xFF); out.push_back(v & 0xFF);
    }
}
static void writeU64(std::vector<uint8_t> &out, uint64_t v, Endian e) {
    if (e == Endian::LITTLE) {
        for (int i = 0; i < 8; ++i) out.push_back((v >> (i * 8)) & 0xFF);
    } else {
        for (int i = 7; i >= 0; --i) out.push_back((v >> (i * 8)) & 0xFF);
    }
}
static void writeF32(std::vector<uint8_t> &out, float v, Endian e) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    writeU32(out, bits, e);
}
static void writeF64(std::vector<uint8_t> &out, double v, Endian e) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    writeU64(out, bits, e);
}

static uint8_t readU8(const uint8_t *d) { return d[0]; }
static uint16_t readU16(const uint8_t *d, Endian e) {
    if (e == Endian::LITTLE) return d[0] | (uint16_t(d[1]) << 8);
    return (uint16_t(d[0]) << 8) | d[1];
}
static uint32_t readU32(const uint8_t *d, Endian e) {
    if (e == Endian::LITTLE)
        return d[0] | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24);
    return (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) | (uint32_t(d[2]) << 8) | d[3];
}
static uint64_t readU64(const uint8_t *d, Endian e) {
    uint64_t v = 0;
    if (e == Endian::LITTLE) {
        for (int i = 0; i < 8; ++i) v |= uint64_t(d[i]) << (i * 8);
    } else {
        for (int i = 0; i < 8; ++i) v = (v << 8) | d[i];
    }
    return v;
}
static float readF32(const uint8_t *d, Endian e) {
    uint32_t bits = readU32(d, e);
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}
static double readF64(const uint8_t *d, Endian e) {
    uint64_t bits = readU64(d, e);
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
}

struct FormatIter {
    const std::string &fmt;
    size_t pos = 0;
    Endian endian = Endian::NATIVE;

    FormatIter(const std::string &f) : fmt(f) {
        if (!fmt.empty()) {
            if (fmt[0] == '<') { endian = Endian::LITTLE; pos = 1; }
            else if (fmt[0] == '>') { endian = Endian::BIG; pos = 1; }
            else if (fmt[0] == '!') { endian = Endian::BIG; pos = 1; }
            else if (fmt[0] == '=') { endian = nativeEndian(); pos = 1; }
        }
        if (endian == Endian::NATIVE) endian = nativeEndian();
    }

    bool done() const { return pos >= fmt.size(); }

    struct FmtSpec {
        char code = 0;
        int count = 1;
    };

    FmtSpec next() {
        FmtSpec spec;
        if (done()) return spec;
        spec.code = fmt[pos++];
        spec.count = 0;
        while (pos < fmt.size() && fmt[pos] >= '0' && fmt[pos] <= '9') {
            spec.count = spec.count * 10 + (fmt[pos] - '0');
            ++pos;
        }
        if (spec.count == 0) spec.count = 1;
        return spec;
    }
};

static int64_t getIntArg(const Value &v) {
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
    if (v.isBool()) return v.asBool() ? 1 : 0;
    throw std::runtime_error("pack: expected integer value");
}

static double getFloatArg(const Value &v) {
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
    throw std::runtime_error("pack: expected float value");
}

void registerPackModule(VMApi &api) {
    api.registerFunction("pack", [&api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("pack() requires a format string");
        const auto &fmtVal = args[0];
        if (!fmtVal.isStringValId() && !fmtVal.isStringId())
            throw std::runtime_error("pack(): first argument must be a format string");
        std::string fmt = api.toString(fmtVal);
        FormatIter fi(fmt);
        std::vector<uint8_t> out;
        size_t argIdx = 1;

        while (!fi.done()) {
            auto spec = fi.next();
            char c = spec.code;
            int count = spec.count;

            if (c == 'x' || c == '_') {
                for (int i = 0; i < count; ++i) out.push_back(0);
                continue;
            }

            for (int i = 0; i < count; ++i) {
                if (argIdx >= args.size())
                    throw std::runtime_error("pack(): not enough arguments for format");

                const auto &val = args[argIdx++];

                switch (c) {
                case 'i': case 's': {
                    int64_t v = getIntArg(val);
                    out.push_back(static_cast<uint8_t>(v & 0xFF));
                    break;
                }
                case 'u': {
                    uint8_t uv = static_cast<uint8_t>(getIntArg(val) & 0xFF);
                    out.push_back(uv);
                    break;
                }
                case 'I': {
                    int64_t v = getIntArg(val);
                    writeU16(out, static_cast<uint16_t>(v), fi.endian);
                    break;
                }
                case 'U': {
                    uint16_t uv = static_cast<uint16_t>(getIntArg(val) & 0xFFFF);
                    writeU16(out, uv, fi.endian);
                    break;
                }
                case 'l': {
                    int64_t v = getIntArg(val);
                    writeU32(out, static_cast<uint32_t>(v), fi.endian);
                    break;
                }
                case 'L': {
                    uint32_t uv = static_cast<uint32_t>(getIntArg(val) & 0xFFFFFFFF);
                    writeU32(out, uv, fi.endian);
                    break;
                }
                case 'q': {
                    int64_t v = getIntArg(val);
                    writeU64(out, static_cast<uint64_t>(v), fi.endian);
                    break;
                }
                case 'Q': {
                    uint64_t uv = static_cast<uint64_t>(getIntArg(val));
                    writeU64(out, uv, fi.endian);
                    break;
                }
                case 'f': {
                    writeF32(out, static_cast<float>(getFloatArg(val)), fi.endian);
                    break;
                }
                case 'd': {
                    writeF64(out, getFloatArg(val), fi.endian);
                    break;
                }
                case 'c': {
                    std::string s;
                    if (val.isStringValId() || val.isStringId())
                        s = api.toString(val);
                    else
                        s = std::to_string(getIntArg(val));
                    for (size_t j = 0; j < s.size(); ++j)
                        out.push_back(static_cast<uint8_t>(s[j]));
                    argIdx += (count - 1) * 0;
                    i = count;
                    break;
                }
                case 'p': {
                    uint64_t addr = 0;
                    if (val.isPtr())
                        addr = reinterpret_cast<uint64_t>(val.asPtr());
                    else
                        addr = static_cast<uint64_t>(getIntArg(val));
                    writeU64(out, addr, fi.endian);
                    break;
                }
                default:
                    throw std::runtime_error(
                        std::string("pack(): unknown format specifier '") + c + "'");
                }
            }
        }

        Value arr = api.makeArray();
        for (auto b : out)
            api.push(arr, Value(static_cast<int64_t>(b)));
        return arr;
    });

    api.registerFunction("unpack", [&api](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("unpack() requires a format string and byte array");
        const auto &fmtVal = args[0];
        const auto &dataVal = args[1];
        if (!fmtVal.isStringValId() && !fmtVal.isStringId())
            throw std::runtime_error("unpack(): first argument must be a format string");
        if (!dataVal.isArrayId())
            throw std::runtime_error("unpack(): second argument must be a byte array");

        std::string fmt = api.toString(fmtVal);
        uint32_t dataLen = api.length(dataVal);
        size_t offset = 0;
        if (args.size() >= 3 && args[2].isInt())
            offset = static_cast<size_t>(args[2].asInt());

        FormatIter fi(fmt);
        std::vector<uint8_t> raw;
        raw.reserve(dataLen);
        for (uint32_t i = 0; i < dataLen; ++i) {
            Value elem = api.getAt(dataVal, i);
            raw.push_back(elem.isInt()
                ? static_cast<uint8_t>(elem.asInt() & 0xFF) : 0);
        }

        Value result = api.makeArray();

        while (!fi.done()) {
            auto spec = fi.next();
            char c = spec.code;
            int count = spec.count;

            if (c == 'x' || c == '_') {
                offset += count;
                continue;
            }

            for (int i = 0; i < count; ++i) {
                switch (c) {
                case 'i': case 's': {
                    if (offset + 1 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    int8_t v = static_cast<int8_t>(readU8(raw.data() + offset));
                    api.push(result, Value(static_cast<int64_t>(v)));
                    offset += 1;
                    break;
                }
                case 'u': {
                    if (offset + 1 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    api.push(result, Value(static_cast<int64_t>(readU8(raw.data() + offset))));
                    offset += 1;
                    break;
                }
                case 'I': {
                    if (offset + 2 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    int16_t v = static_cast<int16_t>(readU16(raw.data() + offset, fi.endian));
                    api.push(result, Value(static_cast<int64_t>(v)));
                    offset += 2;
                    break;
                }
                case 'U': {
                    if (offset + 2 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    api.push(result, Value(static_cast<int64_t>(readU16(raw.data() + offset, fi.endian))));
                    offset += 2;
                    break;
                }
                case 'l': {
                    if (offset + 4 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    int32_t v = static_cast<int32_t>(readU32(raw.data() + offset, fi.endian));
                    api.push(result, Value(static_cast<int64_t>(v)));
                    offset += 4;
                    break;
                }
                case 'L': {
                    if (offset + 4 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    api.push(result, Value(static_cast<int64_t>(readU32(raw.data() + offset, fi.endian))));
                    offset += 4;
                    break;
                }
                case 'q': {
                    if (offset + 8 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    int64_t v = static_cast<int64_t>(readU64(raw.data() + offset, fi.endian));
                    api.push(result, Value(v));
                    offset += 8;
                    break;
                }
                case 'Q': {
                    if (offset + 8 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    api.push(result, Value(static_cast<int64_t>(readU64(raw.data() + offset, fi.endian))));
                    offset += 8;
                    break;
                }
                case 'f': {
                    if (offset + 4 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    api.push(result, Value(static_cast<double>(readF32(raw.data() + offset, fi.endian))));
                    offset += 4;
                    break;
                }
                case 'd': {
                    if (offset + 8 > raw.size())
                        throw std::runtime_error("unpack(): data underflow at offset");
                    api.push(result, Value(readF64(raw.data() + offset, fi.endian)));
                    offset += 8;
                    break;
                }
                case 'c': {
                    if (offset + count > raw.size())
                        throw std::runtime_error("unpack(): data underflow for c format");
                    std::string s(raw.data() + offset, raw.data() + offset + count);
                    api.push(result, api.makeString(std::move(s)));
                    offset += count;
                    i = count;
                    break;
                }
                case 'p': {
                    if (offset + 8 > raw.size())
                        throw std::runtime_error("unpack(): data underflow for pointer");
                    uint64_t addr = readU64(raw.data() + offset, fi.endian);
                    api.push(result, Value::makePtr(reinterpret_cast<void *>(addr)));
                    offset += 8;
                    break;
                }
                default:
                    throw std::runtime_error(
                        std::string("unpack(): unknown format specifier '") + c + "'");
                }
            }
        }

        return result;
    });
}

} // namespace havel::stdlib
