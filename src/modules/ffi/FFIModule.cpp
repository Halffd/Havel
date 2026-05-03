#include "FFIModule.hpp"
#include "havel-lang/ffi/FFICall.hpp"
#include "havel-lang/ffi/FFIMemory.hpp"
#include "havel-lang/ffi/FFITypes.hpp"
#include "havel-lang/ffi/FFIAccessors.hpp"
#include "havel-lang/core/Value.hpp"
#include "modules/ModuleRegistry.hpp"
#include "../../utils/Logger.hpp"
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
        ::havel::error("FFI: unknown type: {}", name);
    }
    return t;
}

static void* resolvePtr(const Value& v) {
    if (v.isPtr()) return v.asPtr();
    if (v.isInt()) return reinterpret_cast<void*>(static_cast<uintptr_t>(v.asInt64()));
    if (v.isDouble()) return reinterpret_cast<void*>(static_cast<uintptr_t>(v.asDouble()));
    return nullptr;
}

// FFI module object marker - used to identify when receiver is the FFI module
static const char* FFI_MODULE_MARKER = "__ffi_module";

static bool isFFIModuleObject(compiler::VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    // Check if the object has the FFI module marker field
    auto marker = api.getField(val, FFI_MODULE_MARKER);
    return marker && marker->isBool() && marker->asBool();
}

static std::vector<Value> stripReceiver(compiler::VMApi& api, const std::vector<Value>& args) {
    // Only strip if args[0] is specifically the FFI module object (method call via CALL_METHOD).
    // Direct calls like ffi.open(path) should NOT strip - the first arg is real user data.
    if (!args.empty() && isFFIModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

// ============================================================================
// Library management
// ============================================================================

static Value ffiOpen(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    std::string path = api.toString(args[0]);
    void* handle = FFICall::load_library(path);
    return Value::makePtr(handle);
}

static Value ffiClose(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* handle = resolvePtr(args[0]);
    if (handle) {
        FFICall::unload_library(handle);
    }
    return Value::makeNull();
}

static Value ffiSym(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 3) {
        ::havel::error("ffi.call: need at least 3 args (fn_ptr, ret_type, param_types)");
        return Value::makeNull();
    }

    void* fn_ptr = resolvePtr(args[0]);
    if (!fn_ptr) {
        ::havel::error("ffi.call: null function pointer");
        return Value::makeNull();
    }

    std::shared_ptr<FFIType> ret_type = resolveType(api, args[1]);
    if (!ret_type) return Value::makeNull();

    std::vector<std::shared_ptr<FFIType>> param_types;
    if (args[2].isArrayId()) {
        uint32_t len = api.length(args[2]);
        for (uint32_t i = 0; i < len; i++) {
            auto typeName = api.getAt(args[2], i);
            auto t = resolveType(api, typeName);
            if (!t) {
                ::havel::error("ffi.call: unknown param type at index {}", i);
                return Value::makeNull();
            }
            param_types.push_back(t);
        }
    }

    size_t havel_arg_offset = 3;
    if (param_types.size() != args.size() - havel_arg_offset) {
        ::havel::error("ffi.call: expected {} args, got {}", param_types.size(), args.size() - havel_arg_offset);
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
            std::string s;
            if (arg.isPtr()) {
                s = static_cast<const char*>(arg.asPtr());
            } else if (arg.isStringId() || arg.isStringValId()) {
                s = api.toString(arg);
            }
            auto buf = std::make_unique<char[]>(s.size() + 1);
            std::memcpy(buf.get(), s.c_str(), s.size() + 1);
            arg_ptrs.push_back(buf.get());
            string_storage.push_back(std::move(buf));
        } else if (pt->kind == FFITypeKind::POINTER) {
            void* p = resolvePtr(arg);
            auto buf = std::make_unique<uint8_t[]>(sizeof(void*));
            std::memcpy(buf.get(), &p, sizeof(void*));
            arg_ptrs.push_back(buf.get());
            arg_storage.push_back(std::move(buf));
        } else {
            void* native = FFIMemory::to_native(arg, pt);
            if (!native) {
                ::havel::error("ffi.call: failed to marshal arg {} of type {}", i, pt->name);
                return Value::makeNull();
            }
            size_t sz = FFITypeRegistry::size_of(pt);
            auto buf = std::make_unique<uint8_t[]>(sz);
            std::memcpy(buf.get(), native, sz);
            arg_ptrs.push_back(buf.get());
            arg_storage.push_back(std::move(buf));
            std::free(native);
        }
    }

    return FFICall::call_native(fn_ptr, arg_ptrs, ret_type, param_types);
}

// ============================================================================
// C definition parser
// ============================================================================

static Value ffiCdef(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
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

        if (decl.kind == FFIDeclaration::Kind::STRUCT && decl.type) {
            api.setField(obj, "size", Value(static_cast<int64_t>(FFITypeRegistry::size_of(decl.type))));
            api.setField(obj, "alignment", Value(static_cast<int64_t>(FFITypeRegistry::align_of(decl.type))));
            auto fields_arr = api.makeArray();
            for (auto& [fname, ftype] : decl.fields) {
                auto fobj = api.makeObject();
                api.setField(fobj, "name", api.makeString(fname));
                api.setField(fobj, "type", api.makeString(ftype->name));
                auto it = decl.type->field_offsets.find(fname);
                if (it != decl.type->field_offsets.end()) {
                    api.setField(fobj, "offset", Value(static_cast<int64_t>(it->second)));
                }
                api.push(fields_arr, fobj);
            }
            api.setField(obj, "fields", fields_arr);
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
            if (!decl.param_names.empty()) {
                auto params_arr = api.makeArray();
                for (auto& pname : decl.param_names) {
                    api.push(params_arr, api.makeString(pname));
                }
                api.setField(obj, "params", params_arr);
            }
        }

        if (decl.kind == FFIDeclaration::Kind::TYPEDEF && decl.type) {
            api.setField(obj, "underlying", api.makeString(decl.type->name));
        }

        api.push(result, obj);
    }
    return result;
}

// ============================================================================
// Memory management
// ============================================================================

static Value ffiAlloc(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    size_t size = static_cast<size_t>(args[0].asInt64());
    void* ptr = FFIMemory::alloc_bytes(size);
    return Value::makePtr(ptr);
}

static Value ffiFree(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    FFIMemory::free(ptr);
    return Value::makeNull();
}

static Value ffiSizeof(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    auto t = resolveType(api, args[0]);
    if (!t) return Value::makeNull();
    return Value(static_cast<int64_t>(FFITypeRegistry::size_of(t)));
}

static Value ffiAlignof(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    auto t = resolveType(api, args[0]);
    if (!t) return Value::makeNull();
    return Value(static_cast<int64_t>(FFITypeRegistry::align_of(t)));
}

// ============================================================================
// Type conversion
// ============================================================================

static Value ffiString(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    const char* str = static_cast<const char*>(ptr);
    return api.makeString(std::string(str));
}

static Value ffiCstring(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    std::string s = api.toString(args[0]);
    char* buf = static_cast<char*>(FFIMemory::alloc_bytes(s.size() + 1));
    if (!buf) return Value::makeNull();
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return Value::makePtr(buf);
}

static Value ffiArray(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
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
    auto args = stripReceiver(api, rawArgs);
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

static Value ffiClosure(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* cb = resolvePtr(args[0]);
    FFICall::destroy_callback(cb);
    return Value::makeNull();
}

// ============================================================================
// Bulk memory operations
// ============================================================================

static Value ffiMemcpy(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 3) return Value::makeNull();
    void* dst = resolvePtr(args[0]);
    void* src = resolvePtr(args[1]);
    if (!dst || !src) return Value::makeNull();
    size_t size = static_cast<size_t>(args[2].asInt64());
    std::memcpy(dst, src, size);
    return Value::makePtr(dst);
}

static Value ffiMemset(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 3) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    int val = static_cast<int>(args[1].asInt64());
    size_t size = static_cast<size_t>(args[2].asInt64());
    std::memset(ptr, val, size);
    return Value::makePtr(ptr);
}

// ============================================================================
// Global variables
// ============================================================================

static Value ffiVar(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* handle = resolvePtr(args[0]);
    std::string name = api.toString(args[1]);
    void* sym = FFICall::get_symbol(handle, name);
    return Value::makePtr(sym);
}

static Value ffiGet(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    auto t = resolveType(api, args[1]);
    if (!t) return Value::makeNull();
    return FFIMemory::to_havel(ptr, t);
}

static Value ffiSet(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
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
static Value ffi##name(compiler::VMApi& api, const std::vector<Value>& rawArgs) { \
    auto args = stripReceiver(api, rawArgs); \
    if (args.size() < 1) return Value::makeNull(); \
        void* ptr = resolvePtr(args[0]); \
        if (!ptr) return Value::makeNull(); \
        return accessor(ptr); \
    }

#define DEFINE_SETTER(name, ctype, accessor) \
static Value ffi##name(compiler::VMApi& api, const std::vector<Value>& rawArgs) { \
    auto args = stripReceiver(api, rawArgs); \
    if (args.size() < 2) return Value::makeNull(); \
        void* ptr = resolvePtr(args[0]); \
        if (!ptr) return Value::makeNull(); \
        ctype v = static_cast<ctype>(args[1].asInt64()); \
        accessor(ptr, v); \
        return Value::makeNull(); \
    }

static Value ffiGetI8(compiler::VMApi&std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_int8(ptr)));
}
static Value ffiSetI8(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int8(ptr, static_cast<int8_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetI16(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_int16(ptr)));
}
static Value ffiSetI16(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int16(ptr, static_cast<int16_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetI32(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_int32(ptr)));
}
static Value ffiSetI32(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int32(ptr, static_cast<int32_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetI64(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(havel::ffi::get_int64(ptr));
}
static Value ffiSetI64(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_int64(ptr, args[1].asInt64());
    return Value::makeNull();
}

static Value ffiGetU8(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint8(ptr)));
}
static Value ffiSetU8(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint8(ptr, static_cast<uint8_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetU16(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint16(ptr)));
}
static Value ffiSetU16(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint16(ptr, static_cast<uint16_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetU32(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint32(ptr)));
}
static Value ffiSetU32(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint32(ptr, static_cast<uint32_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetU64(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value(static_cast<int64_t>(havel::ffi::get_uint64(ptr)));
}
static Value ffiSetU64(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_uint64(ptr, static_cast<uint64_t>(args[1].asInt64()));
    return Value::makeNull();
}

static Value ffiGetF32(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value::makeDouble(static_cast<double>(havel::ffi::get_float32(ptr)));
}
static Value ffiSetF32(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_float32(ptr, static_cast<float>(args[1].asDouble()));
    return Value::makeNull();
}

static Value ffiGetF64(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value::makeDouble(havel::ffi::get_float64(ptr));
}
static Value ffiSetF64(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    havel::ffi::set_float64(ptr, args[1].asDouble());
    return Value::makeNull();
}

static Value ffiGetPtr(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 1) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    return Value::makePtr(havel::ffi::get_pointer(ptr));
}
static Value ffiSetPtr(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    if (args.size() < 2) return Value::makeNull();
    void* ptr = resolvePtr(args[0]);
    if (!ptr) return Value::makeNull();
    void* v = resolvePtr(args[1]);
    havel::ffi::set_pointer(ptr, v);
    return Value::makeNull();
}

static Value ffiPtrAdd(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
  auto args = stripReceiver(api, rawArgs);
  if (args.size() < 2) return Value::makeNull();
  void* ptr = resolvePtr(args[0]);
  if (!ptr) return Value::makeNull();
  int64_t offset = args[1].isInt() ? args[1].asInt64() :
                   args[1].isDouble() ? static_cast<int64_t>(args[1].asDouble()) : 0;
  return Value::makePtr(static_cast<char*>(ptr) + offset);
}

static Value ffiPtrSub(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
  auto args = stripReceiver(api, rawArgs);
  if (args.size() < 2) return Value::makeNull();
  void* ptr = resolvePtr(args[0]);
  if (!ptr) return Value::makeNull();
  int64_t offset = args[1].isInt() ? args[1].asInt64() :
                   args[1].isDouble() ? static_cast<int64_t>(args[1].asDouble()) : 0;
  return Value::makePtr(static_cast<char*>(ptr) - offset);
}

static Value ffiPtrToUint(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
  auto args = stripReceiver(api, rawArgs);
  if (args.empty()) return Value::makeNull();
  void* ptr = resolvePtr(args[0]);
  return Value(static_cast<int64_t>(reinterpret_cast<uintptr_t>(ptr)));
}

// ============================================================================
// Platform-specific
// ============================================================================

static Value ffiLastError(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
    return Value(static_cast<int64_t>(errno));
}

static Value ffiClearError(compiler::VMApi& api, const std::vector<Value>& rawArgs) {
    auto args = stripReceiver(api, rawArgs);
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

    // Bulk memory operations
    reg("ffi.memcpy", ffiMemcpy);
    reg("ffi.memset", ffiMemset);

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

    // Pointer arithmetic
  reg("ffi.ptr_add", ffiPtrAdd);
  reg("ffi.ptr_sub", ffiPtrSub);
  reg("ffi.ptr_to_uint", ffiPtrToUint);

  // Platform
    reg("ffi.lastError", ffiLastError);
    reg("ffi.clearError", ffiClearError);

    // Build module object
    auto ffiObj = api.makeObject();
    // Mark this object as the FFI module so stripReceiver can identify it
    api.setField(ffiObj, FFI_MODULE_MARKER, Value::makeBool(true));
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
    api.setField(ffiObj, "memcpy", api.makeFunctionRef("ffi.memcpy"));
    api.setField(ffiObj, "memset", api.makeFunctionRef("ffi.memset"));
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
  api.setField(ffiObj, "ptr_add", api.makeFunctionRef("ffi.ptr_add"));
  api.setField(ffiObj, "ptr_sub", api.makeFunctionRef("ffi.ptr_sub"));
  api.setField(ffiObj, "ptr_to_uint", api.makeFunctionRef("ffi.ptr_to_uint"));
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
