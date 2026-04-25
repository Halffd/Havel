#include "PointerModule.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerPointerModule(VMApi &api) {
    api.registerFunction("ptr", [](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("ptr() requires a value");
        const auto &v = args[0];
        if (v.isPtr())
            return v;
        if (v.isInt()) {
            uint64_t addr = static_cast<uint64_t>(v.asInt());
            return Value::makePtr(reinterpret_cast<void *>(addr));
        }
        if (v.isDouble()) {
            uint64_t addr = static_cast<uint64_t>(v.asDouble());
            return Value::makePtr(reinterpret_cast<void *>(addr));
        }
        if (v.isNull())
            return Value::makePtr(nullptr);
        throw std::runtime_error("ptr() expects an integer address or null");
    });

    api.registerFunction("deref", [](const std::vector<Value> &args) {
        if (args.empty())
            throw std::runtime_error("deref() requires a pointer");
        const auto &v = args[0];
        if (!v.isPtr())
            throw std::runtime_error("deref() expects a pointer");
        void *ptr = v.asPtr();
        if (!ptr)
            throw std::runtime_error("deref(): null pointer");
        uint64_t addr = reinterpret_cast<uint64_t>(ptr);
        return Value(static_cast<int64_t>(addr));
    });

    api.registerFunction("offset", [](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("offset() requires a pointer and offset");
        const auto &base = args[0];
        const auto &off = args[1];
        if (!base.isPtr())
            throw std::runtime_error("offset() first arg must be a pointer");
        uint64_t addr = reinterpret_cast<uint64_t>(base.asPtr());
        int64_t delta = off.isInt() ? off.asInt() : static_cast<int64_t>(off.asDouble());
        return Value::makePtr(reinterpret_cast<void *>(addr + delta));
    });

    api.registerFunction("ptreq", [](const std::vector<Value> &args) {
        if (args.size() < 2)
            throw std::runtime_error("ptreq() requires two pointers");
        const auto &a = args[0];
        const auto &b = args[1];
        if (a.isPtr() && b.isPtr())
            return Value(a.asPtr() == b.asPtr());
        uint64_t va = 0, vb = 0;
        if (a.isPtr()) va = reinterpret_cast<uint64_t>(a.asPtr());
        else if (a.isInt()) va = static_cast<uint64_t>(a.asInt());
        if (b.isPtr()) vb = reinterpret_cast<uint64_t>(b.asPtr());
        else if (b.isInt()) vb = static_cast<uint64_t>(b.asInt());
        return Value(va == vb);
    });
}

} // namespace havel::stdlib
