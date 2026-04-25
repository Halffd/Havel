#include "FFICall.hpp"
#include "../../utils/Logger.hpp"
#include "FFITypes.hpp"
#include "FFIMemory.hpp"
#include "FFIAccessors.hpp"
#include "../core/Value.hpp"
#include <regex>
#include <dlfcn.h>

#ifdef HAVE_LIBFFI
#include <ffi.h>

namespace havel::ffi {

std::unordered_map<void*, std::string> FFICall::libraries_;
std::unordered_map<void*, std::function<Value(const std::vector<Value>&)>> FFICall::callbacks_;

static ffi_type* to_ffi_type_internal(FFITypeKind kind) {
    switch (kind) {
    case FFITypeKind::VOID: return &ffi_type_void;
    case FFITypeKind::BOOL: return &ffi_type_uint8;
    case FFITypeKind::INT8: return &ffi_type_sint8;
    case FFITypeKind::INT16: return &ffi_type_sint16;
    case FFITypeKind::INT32: return &ffi_type_sint32;
    case FFITypeKind::INT64: return &ffi_type_sint64;
    case FFITypeKind::UINT8: return &ffi_type_uint8;
    case FFITypeKind::UINT16: return &ffi_type_uint16;
    case FFITypeKind::UINT32: return &ffi_type_uint32;
    case FFITypeKind::UINT64: return &ffi_type_uint64;
    case FFITypeKind::FLOAT32: return &ffi_type_float;
    case FFITypeKind::FLOAT64: return &ffi_type_double;
    case FFITypeKind::POINTER: return &ffi_type_pointer;
    case FFITypeKind::STRING: return &ffi_type_pointer;
    default: return &ffi_type_pointer;
    }
}

ffi_type* FFICall::to_ffi_type(std::shared_ptr<FFIType> type) {
    if (!type) return &ffi_type_pointer;
    return to_ffi_type_internal(type->kind);
}

void* FFICall::load_library(const std::string& path) {
    int flags = RTLD_NOW;
#ifdef __APPLE__
    flags |= RTLD_FIRST;
#endif

    void* handle = dlopen(path.c_str(), flags);
    if (!handle) {
        ::havel::error("FFICall: failed to load {}: {}", path, dlerror());
    }
    return handle;
}

void FFICall::unload_library(void* handle) {
    if (handle) {
        dlclose(handle);
        libraries_.erase(handle);
    }
}

void* FFICall::get_symbol(void* handle, const std::string& name) {
    if (!handle) return nullptr;

    void* sym = dlsym(handle, name.c_str());
    char* error = dlerror();
    if (error) {
        ::havel::error("FFICall: failed to find symbol {}: {}", name, error);
        return nullptr;
    }
    return sym;
}

Value FFICall::call_function(void* fn_ptr,
    const std::vector<Value>& args,
    std::shared_ptr<FFIType> return_type,
    const std::vector<std::shared_ptr<FFIType>>& param_types,
    bool variadic,
    const std::vector<std::shared_ptr<FFIType>>& variadic_types) {
    if (!fn_ptr) return Value::makeNull();

    size_t fixed_count = param_types.size();
    size_t total_count = variadic ? (fixed_count + variadic_types.size()) : param_types.size();
    if (!variadic && fixed_count != args.size()) return Value::makeNull();
    if (variadic && total_count != args.size()) return Value::makeNull();

    std::vector<ffi_type*> ffi_param_types(total_count);
    for (size_t i = 0; i < fixed_count; i++) {
        ffi_param_types[i] = to_ffi_type(param_types[i]);
    }
    if (variadic) {
        for (size_t i = 0; i < variadic_types.size(); i++) {
            ffi_param_types[fixed_count + i] = to_ffi_type(variadic_types[i]);
        }
    }

    ffi_cif cif;
    ffi_type* ffi_ret = to_ffi_type(return_type);
    ffi_status status;

    if (variadic) {
        status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI,
            static_cast<unsigned int>(fixed_count),
            static_cast<unsigned int>(total_count),
            ffi_ret, ffi_param_types.data());
    } else {
        status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
            static_cast<unsigned int>(total_count),
            ffi_ret, ffi_param_types.data());
    }

    if (status != FFI_OK) {
        ::havel::error("FFICall: failed to prep cif: {}", static_cast<int>(status));
        return Value::makeNull();
    }

    std::vector<void*> ffi_args(total_count);
    std::vector<int64_t> int_storage(total_count);
    for (size_t i = 0; i < args.size(); i++) {
        int_storage[i] = args[i].asInt64();
        ffi_args[i] = &int_storage[i];
    }

    void* ffi_ret_ptr = alloca(FFITypeRegistry::size_of(return_type));

    ffi_call(&cif, reinterpret_cast<void(*)()>(fn_ptr), ffi_ret_ptr, ffi_args.data());

    return FFIMemory::to_havel(ffi_ret_ptr, return_type);
}

static void closure_callback(ffi_cif* cif, void* ret, void** args, void* user_data) {
    auto* fn = static_cast<std::function<Value(const std::vector<Value>&)>*>(user_data);
    std::vector<Value> hav_args;
    for (unsigned int i = 0; i < cif->nargs; i++) {
        hav_args.push_back(Value::makePtr(args[i]));
    }
    Value result = (*fn)(hav_args);
    *(void**)ret = result.asPtr();
}

void* FFICall::create_callback(std::function<Value(const std::vector<Value>&)> fn,
    std::shared_ptr<FFIType> signature) {
    if (signature->param_types.empty()) {
        return nullptr;
    }

    auto* param_types = new std::vector<ffi_type*>;
    for (auto& p : signature->param_types) {
        param_types->push_back(to_ffi_type(p));
    }

    auto* cif = new ffi_cif;
    ffi_type* ret = to_ffi_type(signature->return_type);

    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI,
        static_cast<unsigned int>(param_types->size()),
        ret, param_types->data());
    if (status != FFI_OK) {
        delete param_types;
        delete cif;
        return nullptr;
    }

    auto* closure = new std::function<Value(const std::vector<Value>&)>(fn);
    void* code = nullptr;

    ffi_closure* cov = (ffi_closure*)ffi_closure_alloc(sizeof(ffi_closure), &code);
    if (!cov) {
        delete closure;
        delete param_types;
        delete cif;
        return nullptr;
    }

    ffi_prep_closure_loc(cov, cif, closure_callback, closure, code);

    callbacks_[code] = fn;
    return code;
}

Value FFICall::call_native(void* fn_ptr,
    const std::vector<void*>& arg_ptrs,
    std::shared_ptr<FFIType> return_type,
    const std::vector<std::shared_ptr<FFIType>>& param_types,
    bool variadic,
    const std::vector<std::shared_ptr<FFIType>>& variadic_types) {
    if (!fn_ptr) return Value::makeNull();

    size_t fixed_count = param_types.size();
    size_t total_count = variadic ? (fixed_count + variadic_types.size()) : param_types.size();

    std::vector<ffi_type*> ffi_param_types(total_count);
    for (size_t i = 0; i < fixed_count; i++) {
        ffi_param_types[i] = to_ffi_type(param_types[i]);
    }
    if (variadic) {
        for (size_t i = 0; i < variadic_types.size(); i++) {
            ffi_param_types[fixed_count + i] = to_ffi_type(variadic_types[i]);
        }
    }

    ffi_cif cif;
    ffi_type* ffi_ret = to_ffi_type(return_type);
    ffi_status status;

    if (variadic) {
        status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI,
            static_cast<unsigned int>(fixed_count),
            static_cast<unsigned int>(total_count),
            ffi_ret, ffi_param_types.data());
    } else {
        status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
            static_cast<unsigned int>(total_count),
            ffi_ret, ffi_param_types.data());
    }

    if (status != FFI_OK) {
        ::havel::error("FFICall: failed to prep cif: {}", static_cast<int>(status));
        return Value::makeNull();
    }

    void* ffi_ret_ptr = alloca(FFITypeRegistry::size_of(return_type));
    std::vector<void*> ffi_args(arg_ptrs.begin(), arg_ptrs.end());

    ffi_call(&cif, reinterpret_cast<void(*)()>(fn_ptr), ffi_ret_ptr, ffi_args.data());

    return FFIMemory::to_havel(ffi_ret_ptr, return_type);
}

void FFICall::destroy_callback(void* callback_ptr) {
    if (callback_ptr) {
        auto it = callbacks_.find(callback_ptr);
        if (it != callbacks_.end()) {
            callbacks_.erase(it);
        }
    }
}

std::vector<FFIDeclaration> FFICall::parse_cdef(const std::string& cdef) {
    std::vector<FFIDeclaration> decls;

    std::regex func_regex(R"((\w+)\s+(\w+)\s*\(([^)]*)\)\s*;)");
    std::regex const_regex(R"(#define\s+(\w+)\s+(\w+))");
    std::regex struct_regex(R"(struct\s+(\w+)\s*\{)");
    std::regex type_regex(R"(typedef\s+(\w+)\s+(\w+);)");

    std::sregex_iterator func_iter(cdef.begin(), cdef.end(), func_regex);
    std::sregex_iterator const_iter(cdef.begin(), cdef.end(), const_regex);
    std::sregex_iterator struct_iter(cdef.begin(), cdef.end(), struct_regex);
    std::sregex_iterator type_iter(cdef.begin(), cdef.end(), type_regex);
    std::sregex_iterator end;

    for (auto it = func_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::FUNCTION;
        decl.name = match[2].str();
        decls.push_back(decl);
    }

    for (auto it = const_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::CONSTANT;
        decl.name = match[1].str();
        decls.push_back(decl);
    }

    return decls;
}

bool FFICall::register_declaration(const FFIDeclaration& decl) {
    (void)decl;
    return true;
}

} // namespace havel::ffi

#endif // HAVE_LIBFFI
