#pragma once

#include "compiler/core/BytecodeIR.hpp"
#include "../../errors/ErrorSystem.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace havel::compiler {

#define COMPILER_THROW(msg) \
do { \
    ::havel::errors::ErrorReporter::instance().report( \
    HAVEL_ERROR(::havel::errors::ErrorStage::VM, msg)); \
    throw std::runtime_error(msg); \
} while (0)

inline std::string operatorSymbolToMethodName(const std::string &sym) {
    static const std::unordered_map<std::string, std::string> map = {
        {"+", "op_add"}, {"-", "op_sub"}, {"*", "op_mul"}, {"/", "op_div"},
        {"%", "op_mod"}, {"**", "op_pow"}, {"\\", "op_backslash"},
        {"\\\\", "op_double_backslash"}, {"%%", "op_double_modulo"},
        {"==", "op_eq"}, {"!=", "op_ne"}, {"<", "op_lt"}, {">", "op_gt"},
        {"<=", "op_le"}, {">=", "op_ge"},
        {"!", "op_not"}, {"~", "op_bit_not"}, {"#", "op_length"}, {"|>", "op_pipe"},
        {"+=", "op_iadd"}, {"-=", "op_isub"}, {"*=", "op_imul"}, {"/=", "op_idiv"},
        {"%=", "op_imod"}, {"**=", "op_ipow"},
        {"\\=", "op_backslash_assign"}, {"%%=", "op_double_modulo_assign"},
        {"&=", "op_bit_and_assign"}, {"|=", "op_bit_or_assign"}, {"^=", "op_bit_xor_assign"},
        {"<<=", "op_shift_left_assign"}, {">>=", "op_shift_right_assign"},
        {"[]", "op_index"}, {"[]=", "op_index_set"},
        {"is", "op_is"}, {"matches", "op_matches"},
    };
    auto it = map.find(sym);
    return (it != map.end()) ? it->second : sym;
}

inline uint64_t getFeedbackMask(const Value& v) {
    if (v.isDouble()) return TYPE_HINT_NUMBER;
    if (v.isInt()) return TYPE_HINT_INT;
    if (v.isBool()) return TYPE_HINT_BOOL;
    if (v.isNull()) return TYPE_HINT_NULL;
    if (v.isStringId() || v.isStringValId() || v.isRegexValId()) return TYPE_HINT_STRING;
    if (v.isArrayId()) return TYPE_HINT_ARRAY;
    if (v.isObjectId()) return TYPE_HINT_OBJECT;
    if (v.isFunctionObjId() || v.isClosureId() || v.isHostFuncId()) return TYPE_HINT_FUNCTION;
    return 0;
}

inline bool valuesEqual(const Value &a, const Value &b) {
    if (a.isNull() != b.isNull()) return false;
    if (a.isBool() != b.isBool()) return false;
    if (a.isInt() != b.isInt()) return false;
    if (a.isDouble() != b.isDouble()) return false;
    if (a.isStringValId() != b.isStringValId()) return false;
    if (a.isArrayId() != b.isArrayId()) return false;
    if (a.isObjectId() != b.isObjectId()) return false;
    if (a.isRangeId() != b.isRangeId()) return false;

    if (a.isNull()) return true;
    if (a.isBool()) return a.asBool() == b.asBool();
    if (a.isInt()) return a.asInt() == b.asInt();
    if (a.isDouble()) return a.asDouble() == b.asDouble();
    if (a.isStringValId()) return a.asStringValId() == b.asStringValId();

    if (a.isArrayId()) return a.asArrayId() == b.asArrayId();
    if (a.isObjectId()) return a.asObjectId() == b.asObjectId();
    if (a.isRangeId()) return a.asRangeId() == b.asRangeId();

    return false;
}

inline uint64_t envU64(const char* name, uint64_t fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try { return static_cast<uint64_t>(std::stoull(v)); } catch (...) { return fallback; }
}

inline std::optional<int64_t> indexFromValue(const Value &value) {
    if (value.isInt()) return value.asInt();
    if (value.isDouble()) return static_cast<int64_t>(value.asDouble());
    return std::nullopt;
}

inline std::optional<std::string> keyFromValue(const Value &value, const GCHeap *heap = nullptr, const BytecodeChunk *chunk = nullptr) {
    if (value.isStringId()) {
        if (heap) {
            if (auto *s = heap->string(value.asStringId())) return *s;
        }
        return "<string:" + std::to_string(value.asStringId()) + ">";
    }
    if (value.isStringValId()) {
        if (chunk) return chunk->getString(value.asStringValId());
        return "<string:" + std::to_string(value.asStringValId()) + ">";
    }
    if (value.isInt()) return std::to_string(value.asInt());
    if (value.isDouble()) { std::ostringstream out; out << value.asDouble(); return out.str(); }
    if (value.isBool()) return value.asBool() ? "true" : "false";
    return std::nullopt;
}

inline std::string formatSourceLocation(const BytecodeFunction &function, size_t ip) {
    if (ip >= function.instruction_locations.size()) return "<unknown>";
    const auto &location = function.instruction_locations[ip];
    if (location.line == 0 && location.column == 0) return "<unknown>";
    return std::to_string(location.line) + ":" + std::to_string(location.column);
}

inline SourceLocation nearestSourceLocation(const BytecodeFunction &function, size_t ip) {
    if (function.instruction_locations.empty()) return {};
    size_t idx = std::min(ip, function.instruction_locations.size() - 1);
    while (true) {
        const auto &loc = function.instruction_locations[idx];
        if (loc.line > 0) return loc;
        if (idx == 0) break;
        --idx;
    }
    return {};
}

} // namespace havel::compiler
