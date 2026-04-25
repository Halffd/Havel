/*
 * FFIModule.cpp
 *
 * FFI module bindings for the Havel bytecode VM.
 */
#include "FFIModule.hpp"
#include "havel-lang/ffi/FFICall.hpp"
#include "havel-lang/ffi/FFIMemory.hpp"
#include "havel-lang/ffi/FFITypes.hpp"
#include "havel-lang/ffi/FFIAccessors.hpp"
#include "havel-lang/core/Value.hpp"
#include "modules/ModuleRegistry.hpp"
#include <cstring>
#include <cerrno>
#include <memory>
#include <vector>

#ifdef HAVE_LIBFFI

namespace havel::modules::ffi {

using havel::core::Value;
using havel::ffi::FFICall;
using havel::ffi::FFIMemory;
using havel::ffi::FFIType;
using havel::ffi::FFITypeKind;
using havel::ffi::FFITypeRegistry;
using havel::ffi::FFIDeclaration;

static std::shared_ptr<FFIType> resolveType(compiler::VMApi& api, const Value& v) {
    std::string name = api.toString(v);
    auto t = FFITypeRegistry::from_name(name);
    if (!t) {
        if (name == "void") t = FFITypeRegistry::void_type();
        else if (name == "bool") t = FFITypeRegistry::bool_type();
        else if (name == "int8_t" || name == "int8") t = FFITypeRegistry::int8_type();
        else if (name == "int16_t" || name == "int16") t = FFITypeRegistry::int16_type();
        else if (name == "int32_t" || name == "int32") t = FFITypeRegistry::int32_type();
        else if (name == "int64_t" || name == "int64") t = FFITypeRegistry::int64_type();
        else if (name == "uint8_t" || name == "uint8") t = FFITypeRegistry::uint8_type();
        else if (name == "uint16_t" || name == "uint16") t = FFITypeRegistry::uint16_type();
        else if (name == "uint32_t" || name == "uint32") t = FFITypeRegistry::uint32_type();
        else if (name == "uint64_t" || name == "uint64") t = FFITypeRegistry::uint64_type();
        else if (name == "float" || name == "f32") t = FFITypeRegistry::float32_type();
        else if (name == "double" || name == "f64") t = FFITypeRegistry::float64_type();
        else if (name == "char*" || name == "string") t = FFITypeRegistry::string_type();
        else if (name == "void*" || name == "pointer") t = FFITypeRegistry::pointer_type(nullptr);
    }
    return t;
}

static void* resolvePtr(const Value& v) {
    if (v.isPtr()) return v.asPtr();
    if (v.isInt()) return reinterpret_cast<void*>(static_cast<uintptr_t>(v.asInt64()));
    if (v.isDouble()) return reinterpret_cast<void*>(static_cast<uintptr_t>(v.asDouble()));
    return nullptr;
}

static std::vector<Value> stripReceiver(const std::vector<Value>& args) {
    if (!args.empty() && args[0].isObjectId()) {
        std::vector<Value> stripped(args.begin() + 1, args.end());
        return stripped;
    }
    return args;
}

// ============================================================================
// Library management
// ============================================================================

static Value ffiOpen(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    std::string path = api.toString(args[0]);
    void* handle = FFICall::load_library(path);
    return Value::makePtr(handle);
}

static Value ffiClose(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* handle = resolvePtr(args[0]);
    if (handle) {
        FFICall::unload_library(handle);
    }
    return Value::makeNull();
}

static Value ffiSym(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* handle = resolvePtr(args[0]);
    std::string name = api.toString(args[1]);
    void* sym = FFICall::get_symbol(handle, name);
    return Value::makePtr(sym);
}

// ============================================================================
// FFI call (existing)
// ============================================================================

static Value ffiCall(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 3) {
        return Value::makeNull();
    }

    void* fn_ptr = resolvePtr(args[0]);
    if (!fn_ptr) return Value::makeNull();

    std::shared_ptr<FFIType> ret_type = resolveType(api, args[1]);
    if (!ret_type) return Value::makeNull();

    // args[2] is an array of argument type names
    std::vector<std::shared_ptr<FFIType>> param_types;
    if (args[2].isArrayId()) {
        uint32_t len = api.length(args[2]);
        for (uint32_t i = 0; i < len; i++) {
            auto typeName = api.getAt(args[2], i);
            auto t = resolveType(api, typeName);
            if (!t) return Value::makeNull();
            param_types.push_back(t);
        }
    }

    // The remaining args are the actual arguments to pass to C
    size_t havel_arg_offset = 3;
    if (param_types.size() != args.size() - havel_arg_offset) {
        return Value::makeNull();
    }

    std::vector<void*> arg_ptrs;
    std::vector<std::unique_ptr<uint8_t[]>> arg_storage;
    std::vector<std::unique_ptr<char[]>> string_storage;
    arg_ptrs.reserve(param_types.size());

    for (size_t i = 0; i < param_types.size(); i++) {
        const Value& arg = args[havel_arg_offset + i];
        auto& pt = param_types[i];

        if (pt->kind == FFITypeKind::STRING) {
            // Resolve string value and allocate a persistent copy
            std::string s;
            if (arg.isPtr()) {
                s = static_cast<const char*>(arg.asPtr());
            } else if (arg.isStringId() || arg.isStringValId()) {
                s = api.toString(arg);
            }
            if (!s.empty()) {
                auto buf = std::make_unique<char[]>(s.size() + 1);
                std::memcpy(buf.get(), s.c_str(), s.size() + 1);
                arg_ptrs.push_back(buf.get());
                string_storage.push_back(std::move(buf));
            } else {
                arg_ptrs.push_back(nullptr);
            }
        } else if (pt->kind == FFITypeKind::POINTER) {
            void* p = resolvePtr(arg);
            auto buf = std::make_unique<uint8_t[]>(sizeof(void*));
            std::memcpy(buf.get(), &p, sizeof(void*));
            arg_ptrs.push_back(buf.get());
            arg_storage.push_back(std::move(buf));
        } else {
            void* native = FFIMemory::to_native(arg, pt);
            if (!native) {
                return Value::makeNull();
            }
            size_t sz = FFITypeRegistry::size_of(pt);
            auto buf = std::make_unique<uint8_t[]>(sz);
            std::memcpy(buf.get(), native, sz);
            arg_ptrs.push_back(buf.get());
            arg_storage.push_back(std::move(buf));
            FFIMemory::free(native);
        }
    }

    return FFICall::call_native(fn_ptr, arg_ptrs, ret_type, param_types);
}

// ============================================================================
// C definition parser
// ============================================================================

static Value ffiCdef(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    std::string cdef = api.toString(args[0]);

    void* lib_handle = nullptr;
    if (args.size() >= 2) {
        lib_handle = resolvePtr(args[1]);
    }

    auto decls = FFICall::parse_cdef(cdef);

    auto result = api.makeArray();
    for (auto& decl : decls) {
        auto obj = api.makeObject();
        std::string kindStr;
        switch (decl.kind) {
        case FFIDeclaration::Kind::FUNCTION: kindStr = "function"; break;
        case FFIDeclaration::Kind::CONSTANT: kindStr = "constant"; break;
        case FFIDeclaration::Kind::TYPEDEF: kindStr = "typedef"; break;
        case FFIDeclaration::Kind::STRUCT: kindStr = "struct"; break;
        case FFIDeclaration::Kind::UNION: kindStr = "union"; break;
        case FFIDeclaration::Kind::VARIABLE: kindStr = "variable"; break;
        default: kindStr = "unknown"; break;
        }
        api.setField(obj, "kind", api.makeString(kindStr));
        api.setField(obj, "name", api.makeString(decl.name));

        if (decl.kind == FFIDeclaration::Kind::CONSTANT) {
            api.setField(obj, "value", Value(static_cast<int64_t>(decl.constant_value)));
            api.setGlobal(decl.name, Value(static_cast<int64_t>(decl.constant_value)));
        }

        if (decl.kind == FFIDeclaration::Kind::VARIABLE && lib_handle) {
            void* sym = FFICall::get_symbol(lib_handle, decl.name);
            if (sym) {
                api.setField(obj, "address", Value::makePtr(sym));
                if (decl.type && decl.type->kind != FFITypeKind::POINTER) {
                    api.setGlobal(decl.name, FFIMemory::to_havel(sym, decl.type));
                } else {
                    api.setGlobal(decl.name, Value::makePtr(sym));
                }
            }
        }

        if (decl.kind == FFIDeclaration::Kind::FUNCTION && lib_handle) {
            void* fn = FFICall::get_symbol(lib_handle, decl.name);
            if (fn) {
                decl.function_ptr = fn;
                api.setField(obj, "address", Value::makePtr(fn));
            }
        }

        api.push(result, obj);
    }
    return result;
}

// ============================================================================
// Memory management
// ============================================================================

static Value ffiAlloc(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    // If arg is an integer, treat as byte count (like allocBytes)
    if (args[0].isInt()) {
        size_t size = static_cast<size_t>(args[0].asInt64());
        void* ptr = FFIMemory::alloc_bytes(size);
        return Value::makePtr(ptr);
    }
    auto t = resolveType(api, args[0]);
    if (!t) return Value::makeNull();
    void* ptr = FFIMemory::alloc(t);
    return Value::makePtr(ptr);
}

static Value ffiAllocBytes(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    size_t size = static_cast<size_t>(args[0].asInt64());
    void* ptr = FFIMemory::alloc_bytes(size);
    return Value::makePtr(ptr);
}

static Value ffiFree(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    FFIMemory::free(ptr);
    return Value::makeNull();
}

static Value ffiSizeof(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    auto t = resolveType(api, args[0]);
    if (!t) return Value::makeNull();
    return Value(static_cast<int64_t>(FFITypeRegistry::size_of(t)));
}

static Value ffiAlignof(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    auto t = resolveType(api, args[0]);
    if (!t) return Value::makeNull();
    return Value(static_cast<int64_t>(FFITypeRegistry::align_of(t)));
}

// ============================================================================
// Type conversion
// ============================================================================

static Value ffiString(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    const char* str = static_cast<const char*>(ptr);
    return api.makeString(std::string(str));
}

static Value ffiCstring(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    std::string s = api.toString(args[0]);
    char* buf = static_cast<char*>(FFIMemory::alloc_bytes(s.size() + 1));
    if (!buf) return Value::makeNull();
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return Value::makePtr(buf);
}

static Value ffiArray(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 3) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    auto t = resolveType(api, args[1]);
    if (!t) return Value::makeNull();
    int64_t len = args[2].asInt64();
    if (len <= 0) return Value::makeNull();

    size_t elemSize = FFITypeRegistry::size_of(t);
    auto arr = api.makeArray();
    for (int64_t i = 0; i < len; i++) {
        void* elemPtr = static_cast<char*>(ptr) + i * elemSize;
        api.push(arr, FFIMemory::to_havel(elemPtr, t));
    }
    return arr;
}

static Value ffiCast(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    auto t = resolveType(api, args[1]);
    if (!t) return Value::makeNull();
    void* result = FFIMemory::cast(ptr, t);
    return Value::makePtr(result);
}

// ============================================================================
// Struct operations
// ============================================================================

static Value ffiStruct(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    std::string name = api.toString(args[0]);
    auto st = FFITypeRegistry::struct_type(name);

    // args[1] is an array of [field_name, field_type] pairs
    if (args[1].isArrayId()) {
        uint32_t len = api.length(args[1]);
        for (uint32_t i = 0; i < len; i++) {
            auto pair = api.getAt(args[1], i);
            if (!pair.isArrayId()) continue;
            if (api.length(pair) < 2) continue;
            std::string fieldName = api.toString(api.getAt(pair, 0));
            auto fieldType = resolveType(api, api.getAt(pair, 1));
            if (fieldType) {
                FFITypeRegistry::add_struct_field(st, fieldName, fieldType);
            }
        }
    }
    FFITypeRegistry::compute_layout(st);
    return Value::makePtr(st.get());
}

static Value ffiField(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 3) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    auto t = resolveType(api, args[1]);
    if (!t || t->kind != FFITypeKind::STRUCT) return Value::makeNull();
    std::string fieldName = api.toString(args[2]);

    auto it = t->field_offsets.find(fieldName);
    if (it == t->field_offsets.end()) return Value::makeNull();

    auto fit = std::find_if(t->fields.begin(), t->fields.end(),
        [&fieldName](const auto& p) { return p.first == fieldName; });
    if (fit == t->fields.end()) return Value::makeNull();

    void* fieldPtr = static_cast<char*>(ptr) + it->second;
    return FFIMemory::to_havel(fieldPtr, fit->second);
}

static Value ffiSetField(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 4) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    auto t = resolveType(api, args[1]);
    if (!t || t->kind != FFITypeKind::STRUCT) return Value::makeNull();
    std::string fieldName = api.toString(args[2]);

    auto it = t->field_offsets.find(fieldName);
    if (it == t->field_offsets.end()) return Value::makeNull();

    auto fit = std::find_if(t->fields.begin(), t->fields.end(),
        [&fieldName](const auto& p) { return p.first == fieldName; });
    if (fit == t->fields.end()) return Value::makeNull();

    void* fieldPtr = static_cast<char*>(ptr) + it->second;
    void* native = FFIMemory::to_native(args[3], fit->second);
    if (native) {
        size_t sz = FFITypeRegistry::size_of(fit->second);
        std::memcpy(fieldPtr, native, sz);
        FFIMemory::free(native);
    }
    return Value::makeNull();
}

// ============================================================================
// Callbacks
// ============================================================================

static Value ffiCallback(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 3) return Value::makeNull();

    // args[0] is a Havel closure/function
    Value closure = args[0];

    auto retType = resolveType(api, args[1]);
    if (!retType) return Value::makeNull();

    std::vector<std::shared_ptr<FFIType>> paramTypes;
    if (args[2].isArrayId()) {
        uint32_t len = api.length(args[2]);
        for (uint32_t i = 0; i < len; i++) {
            auto t = resolveType(api, api.getAt(args[2], i));
            if (!t) return Value::makeNull();
            paramTypes.push_back(t);
        }
    }

    auto sig = FFITypeRegistry::function_type(retType, paramTypes);
    void* cb = FFICall::create_callback(
        [&api, closure, retType](const std::vector<Value>& cbArgs) -> Value {
            Value result = api.invoke(closure, cbArgs);
            return result;
        },
        sig);
    return Value::makePtr(cb);
}

static Value ffiClosure(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    // Attach closure context to callback - stub for now
    if (args.size() < 1) return Value::makeNull();
    void* cb = resolvePtr(args[0]);
    return Value::makePtr(cb);
}

// ============================================================================
// Global variables
// ============================================================================

static Value ffiVar(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* handle = resolvePtr(args[0]);
    std::string name = api.toString(args[1]);
    void* sym = FFICall::get_symbol(handle, name);
    return Value::makePtr(sym);
}

static Value ffiGet(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    auto t = resolveType(api, args[1]);
    if (!t) return Value::makeNull();
    return FFIMemory::to_havel(ptr, t);
}

static Value ffiSet(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 3) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    auto t = resolveType(api, args[1]);
    if (!t) return Value::makeNull();
    void* native = FFIMemory::to_native(args[2], t);
    if (native) {
        size_t sz = FFITypeRegistry::size_of(t);
        std::memcpy(ptr, native, sz);
        FFIMemory::free(native);
    }
    return Value::makeNull();
}

// ============================================================================
// Typed accessors
// ============================================================================

#define DEFINE_GETTER(name, ctype, accessor) \
static Value ffi##name(compiler::VMApi&, const std::vector<Value>& rawArgs) { \
    auto args = stripReceiver(rawArgs); \
    if (args.size() < 1) return Value::makeNull(); \
        void* ptr = resolvePtr(args[0]); \
        if (!ptr) return Value::makeNull(); \
        return accessor(ptr); \
    }

#define DEFINE_SETTER(name, ctype, accessor) \
static Value ffi##name(compiler::VMApi&, const std::vector<Value>& rawArgs) { \
    auto args = stripReceiver(rawArgs); \
    if (args.size() < 2) return Value::makeNull(); \
        void* ptr = resolvePtr(args[0]); \
        if (!ptr) return Value::makeNull(); \
        ctype v = static_cast<ctype>(args[1].asInt64()); \
        accessor(ptr, v); \
        return Value::makeNull(); \
    }

static Value ffiGetI8(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_int8(ptr)));
}
static Value ffiSetI8(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int8(ptr, static_cast<int8_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetI16(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_int16(ptr)));
}
static Value ffiSetI16(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int16(ptr, static_cast<int16_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetI32(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_int32(ptr)));
}
static Value ffiSetI32(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int32(ptr, static_cast<int32_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetI64(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(havel::ffi::get_int64(ptr));
}
static Value ffiSetI64(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int64(ptr, args[1].asInt64());
    return Value::makeNull();
}

static Value ffiGetU8(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint8(ptr)));
}
static Value ffiSetU8(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint8(ptr, static_cast<uint8_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetU16(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint16(ptr)));
}
static Value ffiSetU16(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint16(ptr, static_cast<uint16_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetU32(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint32(ptr)));
}
static Value ffiSetU32(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint32(ptr, static_cast<uint32_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetU64(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint64(ptr)));
}
static Value ffiSetU64(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint64(ptr, static_cast<uint64_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetF32(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value::makeDouble(static_cast<double>(havel::ffi::get_float32(ptr)));
}
static Value ffiSetF32(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_float32(ptr, static_cast<float>(args[1].asDouble()));
    return Value::makeNull();
}

static Value ffiGetF64(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value::makeDouble(havel::ffi::get_float64(ptr));
}
static Value ffiSetF64(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_float64(ptr, args[1].asDouble());
    return Value::makeNull();
}

static Value ffiGetPtr(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value::makePtr(havel::ffi::get_pointer(ptr));
}
static Value ffiSetPtr(compiler::VMApi&, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    void* v = resolvePtr(args[1]);
    havel::ffi::set_pointer(ptr, v);
    return Value::makeNull();
}

// ============================================================================
// Platform-specific
// ============================================================================

static Value ffiLastError(compiler::VMApi&, const std::vector<Value>&) {
    return Value(static_cast<int64_t>(errno));
}

static Value ffiClearError(compiler::VMApi&, const std::vector<Value>&) {
    errno = 0;
    return Value::makeNull();
}

// ============================================================================
// Module registration
// ============================================================================

void registerFFIModule(compiler::VMApi& api) {
    auto reg = [&api](const char* name, auto fn) {
        api.registerFunction(name, [&api, fn](const std::vector<Value>& args) {
            return fn(api, args);
        });
    };

    reg("ffi.open", ffiOpen);
    reg("ffi.close", ffiClose);
    reg("ffi.sym", ffiSym);
    reg("ffi.call", ffiCall);
    reg("ffi.cdef", ffiCdef);

    // Memory management
    reg("ffi.alloc", ffiAlloc);
    reg("ffi.allocBytes", ffiAllocBytes);
    reg("ffi.free", ffiFree);
    reg("ffi.sizeof", ffiSizeof);
    reg("ffi.alignof", ffiAlignof);

    // Type conversion
    reg("ffi.string", ffiString);
    reg("ffi.cstring", ffiCstring);
    reg("ffi.array", ffiArray);
    reg("ffi.cast", ffiCast);

    // Struct operations
    reg("ffi.newStruct", ffiStruct);
    reg("ffi.field", ffiField);
    reg("ffi.setField", ffiSetField);

    // Callbacks
    reg("ffi.callback", ffiCallback);
    reg("ffi.closure", ffiClosure);

    // Global variables
    reg("ffi.var", ffiVar);
    reg("ffi.get", ffiGet);
    reg("ffi.set", ffiSet);

    // Typed accessors
    reg("ffi.get_i8", ffiGetI8);
    reg("ffi.set_i8", ffiSetI8);
    reg("ffi.get_i16", ffiGetI16);
    reg("ffi.set_i16", ffiSetI16);
    reg("ffi.get_i32", ffiGetI32);
    reg("ffi.set_i32", ffiSetI32);
    reg("ffi.get_i64", ffiGetI64);
    reg("ffi.set_i64", ffiSetI64);
    reg("ffi.get_u8", ffiGetU8);
    reg("ffi.set_u8", ffiSetU8);
    reg("ffi.get_u16", ffiGetU16);
    reg("ffi.set_u16", ffiSetU16);
    reg("ffi.get_u32", ffiGetU32);
    reg("ffi.set_u32", ffiSetU32);
    reg("ffi.get_u64", ffiGetU64);
    reg("ffi.set_u64", ffiSetU64);
    reg("ffi.get_f32", ffiGetF32);
    reg("ffi.set_f32", ffiSetF32);
    reg("ffi.get_f64", ffiGetF64);
    reg("ffi.set_f64", ffiSetF64);
    reg("ffi.get_ptr", ffiGetPtr);
    reg("ffi.set_ptr", ffiSetPtr);

    // Platform
    reg("ffi.lastError", ffiLastError);
    reg("ffi.clearError", ffiClearError);

    // Build module object
    auto ffiObj = api.makeObject();
    api.setField(ffiObj, "open", api.makeFunctionRef("ffi.open"));
    api.setField(ffiObj, "close", api.makeFunctionRef("ffi.close"));
    api.setField(ffiObj, "sym", api.makeFunctionRef("ffi.sym"));
    api.setField(ffiObj, "call", api.makeFunctionRef("ffi.call"));
    api.setField(ffiObj, "cdef", api.makeFunctionRef("ffi.cdef"));
    api.setField(ffiObj, "alloc", api.makeFunctionRef("ffi.alloc"));
    api.setField(ffiObj, "allocBytes", api.makeFunctionRef("ffi.allocBytes"));
    api.setField(ffiObj, "free", api.makeFunctionRef("ffi.free"));
    api.setField(ffiObj, "sizeof", api.makeFunctionRef("ffi.sizeof"));
    api.setField(ffiObj, "alignof", api.makeFunctionRef("ffi.alignof"));
    api.setField(ffiObj, "string", api.makeFunctionRef("ffi.string"));
    api.setField(ffiObj, "cstring", api.makeFunctionRef("ffi.cstring"));
    api.setField(ffiObj, "array", api.makeFunctionRef("ffi.array"));
    api.setField(ffiObj, "cast", api.makeFunctionRef("ffi.cast"));
    api.setField(ffiObj, "newStruct", api.makeFunctionRef("ffi.newStruct"));
    api.setField(ffiObj, "field", api.makeFunctionRef("ffi.field"));
    api.setField(ffiObj, "setField", api.makeFunctionRef("ffi.setField"));
    api.setField(ffiObj, "callback", api.makeFunctionRef("ffi.callback"));
    api.setField(ffiObj, "closure", api.makeFunctionRef("ffi.closure"));
    api.setField(ffiObj, "var", api.makeFunctionRef("ffi.var"));
    api.setField(ffiObj, "get", api.makeFunctionRef("ffi.get"));
    api.setField(ffiObj, "set", api.makeFunctionRef("ffi.set"));
    api.setField(ffiObj, "get_i8", api.makeFunctionRef("ffi.get_i8"));
    api.setField(ffiObj, "set_i8", api.makeFunctionRef("ffi.set_i8"));
    api.setField(ffiObj, "get_i16", api.makeFunctionRef("ffi.get_i16"));
    api.setField(ffiObj, "set_i16", api.makeFunctionRef("ffi.set_i16"));
    api.setField(ffiObj, "get_i32", api.makeFunctionRef("ffi.get_i32"));
    api.setField(ffiObj, "set_i32", api.makeFunctionRef("ffi.set_i32"));
    api.setField(ffiObj, "get_i64", api.makeFunctionRef("ffi.get_i64"));
    api.setField(ffiObj, "set_i64", api.makeFunctionRef("ffi.set_i64"));
    api.setField(ffiObj, "get_u8", api.makeFunctionRef("ffi.get_u8"));
    api.setField(ffiObj, "set_u8", api.makeFunctionRef("ffi.set_u8"));
    api.setField(ffiObj, "get_u16", api.makeFunctionRef("ffi.get_u16"));
    api.setField(ffiObj, "set_u16", api.makeFunctionRef("ffi.set_u16"));
    api.setField(ffiObj, "get_u32", api.makeFunctionRef("ffi.get_u32"));
    api.setField(ffiObj, "set_u32", api.makeFunctionRef("ffi.set_u32"));
    api.setField(ffiObj, "get_u64", api.makeFunctionRef("ffi.get_u64"));
    api.setField(ffiObj, "set_u64", api.makeFunctionRef("ffi.set_u64"));
    api.setField(ffiObj, "get_f32", api.makeFunctionRef("ffi.get_f32"));
    api.setField(ffiObj, "set_f32", api.makeFunctionRef("ffi.set_f32"));
    api.setField(ffiObj, "get_f64", api.makeFunctionRef("ffi.get_f64"));
    api.setField(ffiObj, "set_f64", api.makeFunctionRef("ffi.set_f64"));
    api.setField(ffiObj, "get_ptr", api.makeFunctionRef("ffi.get_ptr"));
    api.setField(ffiObj, "set_ptr", api.makeFunctionRef("ffi.set_ptr"));
    api.setField(ffiObj, "lastError", api.makeFunctionRef("ffi.lastError"));
    api.setField(ffiObj, "clearError", api.makeFunctionRef("ffi.clearError"));

    api.setGlobal("ffi", ffiObj);
}

} // namespace havel::modules::ffi

namespace havel::modules {

REGISTER_MODULE("ffi", [](compiler::VMApi& api) {
    ffi::registerFFIModule(api);
});

} // namespace havel::modules

#else // !HAVE_LIBFFI

namespace havel::modules::ffi {

void registerFFIModule(compiler::VMApi&) {
    // FFI not available - no-op
}

} // namespace havel::modules::ffi

#endif // HAVE_LIBFFI
