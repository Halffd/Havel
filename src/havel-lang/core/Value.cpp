/*
 * Value.cpp
 *
 * Core value type implementation.
 */
#include "Value.hpp"
#include <sstream>
#include <iomanip>

namespace havel::core {

std::string Value::toString() const {
    if (isNull()) return "null";
    if (isBool()) return asBool() ? "true" : "false";
    if (isInt()) return std::to_string(asInt());
    if (isDouble()) {
        std::ostringstream oss;
        oss << std::setprecision(10) << asDouble();
        return oss.str();
    }
    if (isString()) return "\"" + asString() + "\"";
    if (isArray()) {
        auto arr = asArray();
        if (!arr) return "[]";
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < arr->size(); ++i) {
            if (i > 0) oss << ", ";
            oss << (*arr)[i].toString();
        }
        oss << "]";
        return oss.str();
    }
    if (isObject()) {
        auto obj = asObject();
        if (!obj) return "{}";
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (const auto& [key, val] : *obj) {
            if (!first) oss << ", ";
            oss << "\"" << key << "\": " << val.toString();
            first = false;
        }
        oss << "}";
        return oss.str();
    }
    if (isFunction()) return "<function>";
    return "<unknown>";
}

} // namespace havel::core
