#include "FFICall.hpp"
#include "../../utils/Logger.hpp"
#include "FFITypes.hpp"
#include "FFIMemory.hpp"
#include "FFIAccessors.hpp"
#include "../core/Value.hpp"
#include <iostream>
#include <regex>
#include <set>
#include <dlfcn.h>

#ifdef HAVE_LIBFFI
#include <ffi.h>

namespace havel::ffi {

std::unordered_map<void*, std::string> FFICall::libraries_;
std::unordered_map<void*, std::function<Value(const std::vector<Value>&)>> FFICall::callbacks_;

static ffi_type* to_ffi_type_internal(FFITypeKind kind) {
    switch (kind) {
        case FFITypeKind::VOID:    return &ffi_type_void;
        case FFITypeKind::BOOL:    return &ffi_type_uint8;
        case FFITypeKind::INT8:   return &ffi_type_sint8;
        case FFITypeKind::INT16:  return &ffi_type_sint16;
        case FFITypeKind::INT32:  return &ffi_type_sint32;
        case FFITypeKind::INT64:  return &ffi_type_sint64;
        case FFITypeKind::UINT8:  return &ffi_type_uint8;
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
        havel::error("FFICall: failed to load {}: {}", path, dlerror());
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
        havel::error("FFICall: failed to find symbol {}: {}", name, error);
        return nullptr;
    }
    return sym;
}

Value FFICall::call_function(void* fn_ptr,
                          const std::vector<Value>& args,
                          std::shared_ptr<FFIType> return_type,
                          const std::vector<std::shared_ptr<FFIType>>& param_types) {
    if (!fn_ptr || param_types.size() != args.size()) {
        return Value::makeNull();
    }

    std::vector<void*> arg_ptrs;
    std::vector<std::unique_ptr<uint8_t[]>> arg_storage;
    arg_ptrs.reserve(args.size());
    arg_storage.reserve(args.size());

    for (size_t i = 0; i < args.size(); i++) {
        void* ptr = FFIMemory::to_native(args[i], param_types[i]);
        if (!ptr) {
            return Value::makeNull();
        }
        size_t sz = FFITypeRegistry::size_of(param_types[i]);
        auto buf = std::make_unique<uint8_t[]>(sz);
        std::memcpy(buf.get(), ptr, sz);
        arg_ptrs.push_back(buf.get());
        arg_storage.push_back(std::move(buf));
    }

    return call_native(fn_ptr, arg_ptrs, return_type, param_types);
}

Value FFICall::call_native(void* fn_ptr,
                          const std::vector<void*>& arg_ptrs,
                          std::shared_ptr<FFIType> return_type,
                          const std::vector<std::shared_ptr<FFIType>>& param_types) {
    if (!fn_ptr || param_types.size() != arg_ptrs.size()) {
        return Value::makeNull();
    }

    ffi_cif cif;
    std::vector<ffi_type*> ffi_param_types(param_types.size());
    for (size_t i = 0; i < param_types.size(); i++) {
        ffi_param_types[i] = to_ffi_type(param_types[i]);
    }

    ffi_type* ffi_ret = to_ffi_type(return_type);

    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
        static_cast<unsigned int>(param_types.size()),
        ffi_ret, ffi_param_types.data());
    if (status != FFI_OK) {
        havel::error("FFICall: failed to prep cif: {}", static_cast<int>(status));
        return Value::makeNull();
    }

    size_t ret_size = FFITypeRegistry::size_of(return_type);
    void* ffi_ret_ptr = ret_size > 0 ? alloca(ret_size) : nullptr;

    ffi_call(&cif, reinterpret_cast<void (*)(void)>(fn_ptr), ffi_ret_ptr, const_cast<void**>(arg_ptrs.data()));

    if (!ffi_ret_ptr || return_type->kind == FFITypeKind::VOID) {
        return Value::makeNull();
    }
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
    static std::vector<ffi_type*> param_types;
    static ffi_cif cif;
    
    if (signature->param_types.empty()) {
        return nullptr;
    }
    
    param_types.clear();
    for (auto& p : signature->param_types) {
        param_types.push_back(FFICall::to_ffi_type(p));
    }
    
    ffi_type* ret = FFICall::to_ffi_type(signature->return_type);
    
    ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
                                  static_cast<unsigned int>(param_types.size()),
                                  ret, param_types.data());
    if (status != FFI_OK) {
        return nullptr;
    }
    
    auto* closure = new std::function<Value(const std::vector<Value>&)>(fn);
    void* code = nullptr;
    
    ffi_closure* cov = (ffi_closure*)ffi_closure_alloc(sizeof(ffi_closure), &code);
    if (!cov) {
        delete closure;
        return nullptr;
    }
    
    ffi_prep_closure_loc(cov, &cif, closure_callback, closure, code);
    
    callbacks_[code] = fn;
    return code;
}

void FFICall::destroy_callback(void* callback_ptr) {
    if (callback_ptr) {
        auto it = callbacks_.find(callback_ptr);
        if (it != callbacks_.end()) {
            callbacks_.erase(it);
        }
    }
}

static std::shared_ptr<FFIType> simple_type_from_name(const std::string& name) {
    if (name.find('*') != std::string::npos)
        return FFITypeRegistry::pointer_type(nullptr);
    if (name == "int" || name == "int32_t")
        return FFITypeRegistry::int32_type();
    if (name == "long" || name == "int64_t")
        return FFITypeRegistry::int64_type();
    if (name == "short" || name == "int16_t")
        return FFITypeRegistry::int16_type();
    if (name == "char" || name == "int8_t")
        return FFITypeRegistry::int8_type();
    if (name == "unsigned int" || name == "uint32_t")
        return FFITypeRegistry::uint32_type();
    if (name == "unsigned long" || name == "uint64_t")
        return FFITypeRegistry::uint64_type();
    if (name == "unsigned short" || name == "uint16_t")
        return FFITypeRegistry::uint16_type();
    if (name == "unsigned char" || name == "uint8_t")
        return FFITypeRegistry::uint8_type();
    if (name == "float")
        return FFITypeRegistry::float32_type();
    if (name == "double")
        return FFITypeRegistry::float64_type();
    if (name == "void")
        return FFITypeRegistry::void_type();
    if (name == "bool" || name == "_Bool")
        return FFITypeRegistry::bool_type();
    auto t = FFITypeRegistry::from_name(name);
    return t ? t : FFITypeRegistry::pointer_type(nullptr);
}

static uint64_t parse_const_value(const std::string& s) {
    std::string v = s;
    std::regex suffix_re(R"([uUlL]+$)");
    v = std::regex_replace(v, suffix_re, "");
    if (v.size() >= 2 && (v.substr(0, 2) == "0x" || v.substr(0, 2) == "0X"))
        return std::stoull(v, nullptr, 16);
    if (v.size() >= 2 && v.substr(0, 2) == "0b")
        return std::stoull(v.substr(2), nullptr, 2);
    if (!v.empty() && v[0] == '0' && v.size() > 1 && isdigit(v[1]))
        return std::stoull(v, nullptr, 8);
    return std::stoull(v);
}

static std::shared_ptr<FFIType> parse_c_type(const std::string& raw) {
    std::string t = raw;
    t = std::regex_replace(t, std::regex(R"(\s+)"), " ");
    t = std::regex_replace(t, std::regex(R"(^\s+|\s+$)"), "");
    return simple_type_from_name(t);
}

std::vector<FFIDeclaration> FFICall::parse_cdef(const std::string& cdef) {
    std::vector<FFIDeclaration> decls;

    std::regex func_regex(R"((\w+)\s+(\w+)\s*\(([^)]*)\)\s*;)");
    std::regex const_paren_regex(R"(#define\s+(\w+)\s+\(([^)]+)\))");
    std::regex const_plain_regex(
        R"(#define\s+(\w+)\s+(0[xX][0-9a-fA-F]+[uUlL]*|-?[0-9]+[uUlL]*))");
    std::regex extern_regex(R"(extern\s+([\w\s\*]+?)\s+(\w+)\s*;)");
    std::regex struct_regex(R"(struct\s+(\w+)\s*\{)");
    std::regex type_regex(R"(typedef\s+(\w+)\s+(\w+);)");

    std::sregex_iterator end;

    std::sregex_iterator func_it(cdef.begin(), cdef.end(), func_regex);
    for (; func_it != end; ++func_it) {
        auto& match = *func_it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::FUNCTION;
        decl.name = match[2].str();
        std::string ret_str = match[1].str();
        decl.type = parse_c_type(ret_str);
        std::string params = match[3].str();
        std::regex param_re(R"(\s*(\w[\w\s\*]*)\s+(\w+)\s*)");
        auto p_it = std::sregex_iterator(params.begin(), params.end(), param_re);
        for (; p_it != end; ++p_it) {
            decl.param_names.push_back((*p_it)[2].str());
            decl.fields.push_back({(*p_it)[2].str(), parse_c_type((*p_it)[1].str())});
        }
        decls.push_back(decl);
    }

    std::set<std::string> seen_consts;
    std::sregex_iterator cp_it(cdef.begin(), cdef.end(), const_paren_regex);
    for (; cp_it != end; ++cp_it) {
        auto& match = *cp_it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::CONSTANT;
        decl.name = match[1].str();
        if (seen_consts.count(decl.name)) continue;
        seen_consts.insert(decl.name);
        try { decl.constant_value = parse_const_value(match[2].str()); }
        catch (...) { decl.constant_value = 0; }
        decls.push_back(decl);
    }

    std::sregex_iterator cn_it(cdef.begin(), cdef.end(), const_plain_regex);
    for (; cn_it != end; ++cn_it) {
        auto& match = *cn_it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::CONSTANT;
        decl.name = match[1].str();
        if (seen_consts.count(decl.name)) continue;
        seen_consts.insert(decl.name);
        try { decl.constant_value = parse_const_value(match[2].str()); }
        catch (...) { decl.constant_value = 0; }
        decls.push_back(decl);
    }

    std::sregex_iterator ext_it(cdef.begin(), cdef.end(), extern_regex);
    for (; ext_it != end; ++ext_it) {
        auto& match = *ext_it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::VARIABLE;
        decl.name = match[2].str();
        decl.type = parse_c_type(match[1].str());
        decls.push_back(decl);
    }

    std::sregex_iterator st_it(cdef.begin(), cdef.end(), struct_regex);
    for (; st_it != end; ++st_it) {
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::STRUCT;
        decl.name = (*st_it)[1].str();
        decls.push_back(decl);
    }

    std::sregex_iterator td_it(cdef.begin(), cdef.end(), type_regex);
    for (; td_it != end; ++td_it) {
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::TYPEDEF;
        decl.name = (*td_it)[2].str();
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
