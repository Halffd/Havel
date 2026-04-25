#include "FormatModule.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <array>
#include <cstdarg>

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

void registerFormatModule(VMApi &api) {
    api.registerFunction("hex", [&api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("hex() requires a value");
        const auto &v = args[0];
        if (v.isInt())
            return api.makeString(intToBase(v.asInt(), 16, "0x"));
        if (v.isDouble())
            return api.makeString(intToBase(
                static_cast<int64_t>(v.asDouble()), 16, "0x"));
        if (v.isPtr()) {
            uint64_t addr = reinterpret_cast<uint64_t>(v.asPtr());
            return api.makeString(intToBase(static_cast<int64_t>(addr), 16, "0x"));
        }
        if (v.isArrayId()) {
            uint32_t len = api.length(v);
            std::string result;
            result.reserve(len * 4);
            for (uint32_t i = 0; i < len; ++i) {
                Value elem = api.getAt(v, i);
                int64_t byte = elem.isInt() ? (elem.asInt() & 0xFF) : 0;
                const char *hex_chars = "0123456789abcdef";
                result += hex_chars[(byte >> 4) & 0xF];
                result += hex_chars[byte & 0xF];
            }
            return api.makeString(std::move(result));
        }
        throw std::runtime_error("hex() expects a number, pointer, or array");
    });

    api.registerFunction("oct", [&api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("oct() requires a value");
        const auto &v = args[0];
        if (v.isInt())
            return api.makeString(intToBase(v.asInt(), 8, "0o"));
        if (v.isDouble())
            return api.makeString(intToBase(
                static_cast<int64_t>(v.asDouble()), 8, "0o"));
        if (v.isArrayId()) {
            uint32_t len = api.length(v);
            std::string result;
            for (uint32_t i = 0; i < len; ++i) {
                Value elem = api.getAt(v, i);
                int64_t byte = elem.isInt() ? (elem.asInt() & 0xFF) : 0;
                if (i > 0) result += ' ';
                result += intToBase(byte, 8, "0o");
            }
            return api.makeString(std::move(result));
        }
        throw std::runtime_error("oct() expects a number or array");
    });

    api.registerFunction("bin", [&api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("bin() requires a value");
        const auto &v = args[0];
        if (v.isInt())
            return api.makeString(intToBase(v.asInt(), 2, "0b"));
        if (v.isDouble())
            return api.makeString(intToBase(
                static_cast<int64_t>(v.asDouble()), 2, "0b"));
        if (v.isArrayId()) {
            uint32_t len = api.length(v);
            std::string result;
            for (uint32_t i = 0; i < len; ++i) {
                Value elem = api.getAt(v, i);
                int64_t byte = elem.isInt() ? (elem.asInt() & 0xFF) : 0;
                if (i > 0) result += ' ';
                result += intToBase(byte, 2, "0b");
            }
            return api.makeString(std::move(result));
        }
        throw std::runtime_error("bin() expects a number or array");
    });

    api.registerFunction("b64", [&api](const std::vector<Value> &args) {
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

    api.registerFunction("b64decode", [&api](const std::vector<Value> &args) {
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

    api.registerFunction("format", [&api](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("format() requires a format string");
        const auto &fmtVal = args[0];
        if (!fmtVal.isStringValId() && !fmtVal.isStringId())
            throw std::runtime_error("format(): first argument must be a format string");
        std::string fmt = api.toString(fmtVal);

        std::string result;
        size_t argIdx = 1;
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
                if (argIdx >= args.size())
                    throw std::runtime_error("format(): not enough arguments for {}");
                result += api.toString(args[argIdx++]);
                ++i;
            } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == ':') {
                size_t close = fmt.find('}', i + 2);
                if (close == std::string::npos)
                    throw std::runtime_error("format(): unclosed format specifier");
                std::string spec = fmt.substr(i + 2, close - i - 2);
                if (argIdx >= args.size())
                    throw std::runtime_error("format(): not enough arguments");
                const auto &val = args[argIdx++];
                if (spec == "x" || spec == "hex") {
                    if (val.isInt())
                        result += intToBase(val.asInt(), 16, "");
                    else
                        result += api.toString(val);
                } else if (spec == "o" || spec == "oct") {
                    if (val.isInt())
                        result += intToBase(val.asInt(), 8, "");
                    else
                        result += api.toString(val);
                } else if (spec == "b" || spec == "bin") {
                    if (val.isInt())
                        result += intToBase(val.asInt(), 2, "");
                    else
                        result += api.toString(val);
                } else if (spec == "d" || spec == "dec") {
                    result += api.toString(val);
                } else {
                    result += api.toString(val);
                }
                i = close;
            } else {
                result += fmt[i];
            }
        }
        return api.makeString(std::move(result));
    });
}

} // namespace havel::stdlib
