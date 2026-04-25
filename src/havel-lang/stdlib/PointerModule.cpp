#include "PointerModule.hpp"
#include <cstdint>
#include <cstring>
#include <stdexcept>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static void* getPtr(const Value &v) {
    if (v.isPtr()) return v.asPtr();
    if (v.isInt()) return reinterpret_cast<void*>(static_cast<uint64_t>(v.asInt()));
    throw std::runtime_error("expected a pointer");
}

template <typename T>
static Value typedRead(const Value &ptrVal) {
    void *p = getPtr(ptrVal);
    if (!p) throw std::runtime_error("null pointer dereference");
    T val;
    std::memcpy(&val, p, sizeof(T));
    if constexpr (std::is_floating_point_v<T>)
        return Value::makeDouble(static_cast<double>(val));
    else
        return Value(static_cast<int64_t>(val));
}

template <typename T>
static Value typedWrite(const std::vector<Value> &args) {
    if (args.size() < 2) throw std::runtime_error("requires pointer and value");
    void *p = getPtr(args[0]);
    if (!p) throw std::runtime_error("null pointer dereference");
    T val;
    if constexpr (std::is_floating_point_v<T>) {
        val = static_cast<T>(args[1].isDouble() ? args[1].asDouble() : static_cast<double>(args[1].asInt()));
    } else {
        val = static_cast<T>(args[1].isInt() ? args[1].asInt() : static_cast<int64_t>(args[1].asDouble()));
    }
    std::memcpy(p, &val, sizeof(T));
    return Value::makeNull();
}

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

    api.registerFunction("deref.i8", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.i8() requires a pointer");
        return typedRead<int8_t>(args[0]);
    });
    api.registerFunction("deref.i16", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.i16() requires a pointer");
        return typedRead<int16_t>(args[0]);
    });
    api.registerFunction("deref.i32", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.i32() requires a pointer");
        return typedRead<int32_t>(args[0]);
    });
    api.registerFunction("deref.i64", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.i64() requires a pointer");
        return typedRead<int64_t>(args[0]);
    });
    api.registerFunction("deref.u8", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.u8() requires a pointer");
        return typedRead<uint8_t>(args[0]);
    });
    api.registerFunction("deref.u16", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.u16() requires a pointer");
        return typedRead<uint16_t>(args[0]);
    });
    api.registerFunction("deref.u32", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.u32() requires a pointer");
        return typedRead<uint32_t>(args[0]);
    });
    api.registerFunction("deref.u64", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.u64() requires a pointer");
        return typedRead<uint64_t>(args[0]);
    });
    api.registerFunction("deref.f32", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.f32() requires a pointer");
        return typedRead<float>(args[0]);
    });
    api.registerFunction("deref.f64", [](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("deref.f64() requires a pointer");
        return typedRead<double>(args[0]);
    });

    api.registerFunction("write.i8", [](const std::vector<Value> &args) {
        return typedWrite<int8_t>(args);
    });
    api.registerFunction("write.i16", [](const std::vector<Value> &args) {
        return typedWrite<int16_t>(args);
    });
    api.registerFunction("write.i32", [](const std::vector<Value> &args) {
        return typedWrite<int32_t>(args);
    });
    api.registerFunction("write.i64", [](const std::vector<Value> &args) {
        return typedWrite<int64_t>(args);
    });
    api.registerFunction("write.u8", [](const std::vector<Value> &args) {
        return typedWrite<uint8_t>(args);
    });
    api.registerFunction("write.u16", [](const std::vector<Value> &args) {
        return typedWrite<uint16_t>(args);
    });
    api.registerFunction("write.u32", [](const std::vector<Value> &args) {
        return typedWrite<uint32_t>(args);
    });
    api.registerFunction("write.u64", [](const std::vector<Value> &args) {
        return typedWrite<uint64_t>(args);
    });
    api.registerFunction("write.f32", [](const std::vector<Value> &args) {
        return typedWrite<float>(args);
    });
    api.registerFunction("write.f64", [](const std::vector<Value> &args) {
        return typedWrite<double>(args);
    });
}

} // namespace havel::stdlib
