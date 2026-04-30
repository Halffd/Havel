#include "FFICall.hpp"
#include "../../utils/Logger.hpp"
#include "FFITypes.hpp"
#include "FFIMemory.hpp"
#include "FFIAccessors.hpp"
#include "../core/Value.hpp"
#include <regex>
#include <dlfcn.h>
#include <cstring>
#include <sstream>

#ifdef HAVE_LIBFFI
#include <ffi.h>

namespace havel::ffi {

std::unordered_map<void*, std::string> FFICall::libraries_;
std::mutex FFICall::callback_mutex_;
std::unordered_map<void*, std::unique_ptr<FFICall::CallbackData>> FFICall::callbacks_;
std::mutex FFICall::cif_mutex_;
std::unordered_map<FFICall::CifKey, std::shared_ptr<ffi_cif>, FFICall::CifKeyHash> FFICall::cif_cache_;

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
    } else {
        libraries_[handle] = path;
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

static void closure_callback(ffi_cif* cif, void* ret, void** args, void* user_data) {
    auto* cbd = static_cast<FFICall::CallbackData*>(user_data);
    if (!cbd || !cbd->fn) {
        if (cbd && cbd->return_type) {
            std::memset(ret, 0, FFITypeRegistry::size_of(cbd->return_type));
        }
        return;
    }

    std::vector<Value> hav_args;
    for (unsigned int i = 0; i < cif->nargs; i++) {
        auto param_type = (i < cbd->param_types.size()) ? cbd->param_types[i] : FFITypeRegistry::pointer_type(nullptr);
        Value v = FFIMemory::to_havel(args[i], param_type);
        hav_args.push_back(v);
    }

    Value result = (*cbd->fn)(hav_args);

    auto& ret_type = cbd->return_type;
    if (!ret_type || ret_type->kind == FFITypeKind::VOID) {
        return;
    }

    switch (ret_type->kind) {
    case FFITypeKind::INT8:
        *reinterpret_cast<int8_t*>(ret) = result.isInt() ? static_cast<int8_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::INT16:
        *reinterpret_cast<int16_t*>(ret) = result.isInt() ? static_cast<int16_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::INT32:
        *reinterpret_cast<int32_t*>(ret) = result.isInt() ? static_cast<int32_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::INT64:
        *reinterpret_cast<int64_t*>(ret) = result.isInt() ? result.asInt64() : 0;
        break;
    case FFITypeKind::UINT8:
        *reinterpret_cast<uint8_t*>(ret) = result.isInt() ? static_cast<uint8_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::UINT16:
        *reinterpret_cast<uint16_t*>(ret) = result.isInt() ? static_cast<uint16_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::UINT32:
        *reinterpret_cast<uint32_t*>(ret) = result.isInt() ? static_cast<uint32_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::UINT64:
        *reinterpret_cast<uint64_t*>(ret) = result.isInt() ? static_cast<uint64_t>(result.asInt64()) : 0;
        break;
    case FFITypeKind::FLOAT32:
        *reinterpret_cast<float*>(ret) = result.isDouble() ? static_cast<float>(result.asDouble()) : (result.isInt() ? static_cast<float>(result.asInt64()) : 0.0f);
        break;
    case FFITypeKind::FLOAT64:
        *reinterpret_cast<double*>(ret) = result.isDouble() ? result.asDouble() : (result.isInt() ? static_cast<double>(result.asInt64()) : 0.0);
        break;
    case FFITypeKind::BOOL:
        *reinterpret_cast<uint8_t*>(ret) = result.isInt() ? (result.asInt64() != 0 ? 1 : 0) : 0;
        break;
    case FFITypeKind::POINTER:
    case FFITypeKind::STRING:
        if (result.isPtr()) {
            *reinterpret_cast<void**>(ret) = result.asPtr();
        } else if (result.isInt()) {
            *reinterpret_cast<void**>(ret) = reinterpret_cast<void*>(static_cast<uintptr_t>(result.asInt64()));
        } else {
            *reinterpret_cast<void**>(ret) = nullptr;
        }
        break;
    default:
        *reinterpret_cast<void**>(ret) = result.isPtr() ? result.asPtr() : nullptr;
        break;
    }
}

void* FFICall::create_callback(std::function<Value(const std::vector<Value>&)> fn,
                                std::shared_ptr<FFIType> signature) {
    auto* param_types_ffi = new std::vector<ffi_type*>;
    for (auto& p : signature->param_types) {
        param_types_ffi->push_back(to_ffi_type(p));
    }

    auto* cif = new ffi_cif;
    ffi_type* ret = to_ffi_type(signature->return_type);

    ffi_status status;
    if (param_types_ffi->empty()) {
        status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, 0, ret, nullptr);
    } else {
        status = ffi_prep_cif(cif, FFI_DEFAULT_ABI,
                              static_cast<unsigned int>(param_types_ffi->size()),
                              ret, param_types_ffi->data());
    }

    if (status != FFI_OK) {
        delete param_types_ffi;
        delete cif;
        ::havel::error("FFICall: create_callback: failed to prep cif: {}", static_cast<int>(status));
        return nullptr;
    }

    auto cbd = std::make_unique<CallbackData>();
    cbd->fn = new std::function<Value(const std::vector<Value>&)>(std::move(fn));
    cbd->return_type = signature->return_type;
    cbd->param_types = signature->param_types;
    cbd->cif = cif;
    cbd->param_types_ffi = param_types_ffi;

    void* code = nullptr;
    ffi_closure* closure = static_cast<ffi_closure*>(ffi_closure_alloc(sizeof(ffi_closure), &code));
    if (!closure) {
        delete cbd->fn;
        delete param_types_ffi;
        delete cif;
        ::havel::error("FFICall: create_callback: ffi_closure_alloc failed");
        return nullptr;
    }

    cbd->closure = closure;
    ffi_prep_closure_loc(closure, cif, closure_callback, cbd.get(), code);

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callbacks_[code] = std::move(cbd);
    }

    return code;
}

void FFICall::destroy_callback(void* callback_ptr) {
    if (!callback_ptr) return;

    std::lock_guard<std::mutex> lock(callback_mutex_);
    auto it = callbacks_.find(callback_ptr);
    if (it == callbacks_.end()) return;

    auto& cbd = it->second;
    if (cbd->closure) {
        ffi_closure_free(cbd->closure);
    }
    delete cbd->cif;
    delete cbd->param_types_ffi;
    delete cbd->fn;

    callbacks_.erase(it);
}

FFICall::CifKey FFICall::make_cif_key(void* fn_ptr,
                                       const std::vector<std::shared_ptr<FFIType>>& param_types,
                                       std::shared_ptr<FFIType> return_type,
                                       bool variadic,
                                       const std::vector<std::shared_ptr<FFIType>>& variadic_types) {
    CifKey key;
    key.fn_ptr = fn_ptr;
    key.return_kind = return_type ? static_cast<uint8_t>(return_type->kind) : 0;
    for (auto& pt : param_types) {
        key.param_kinds.push_back(pt ? static_cast<uint8_t>(pt->kind) : static_cast<uint8_t>(FFITypeKind::POINTER));
    }
    if (variadic) {
        key.variadic = true;
        for (auto& vt : variadic_types) {
            key.variadic_kinds.push_back(vt ? static_cast<uint8_t>(vt->kind) : static_cast<uint8_t>(FFITypeKind::POINTER));
        }
    }
    return key;
}

std::shared_ptr<ffi_cif> FFICall::get_or_create_cif(void* fn_ptr,
                                                      const std::vector<std::shared_ptr<FFIType>>& param_types,
                                                      std::shared_ptr<FFIType> return_type,
                                                      bool variadic,
                                                      const std::vector<std::shared_ptr<FFIType>>& variadic_types) {
    auto key = make_cif_key(fn_ptr, param_types, return_type, variadic, variadic_types);

    {
        std::lock_guard<std::mutex> lock(cif_mutex_);
        auto it = cif_cache_.find(key);
        if (it != cif_cache_.end()) {
            return it->second;
        }
    }

    size_t fixed_count = param_types.size();
    size_t total_count = variadic ? (fixed_count + variadic_types.size()) : param_types.size();

    auto cif = std::make_shared<ffi_cif>();

    std::vector<ffi_type*> ffi_param_types(total_count);
    for (size_t i = 0; i < fixed_count; i++) {
        ffi_param_types[i] = to_ffi_type(param_types[i]);
    }
    if (variadic) {
        for (size_t i = 0; i < variadic_types.size(); i++) {
            ffi_param_types[fixed_count + i] = to_ffi_type(variadic_types[i]);
        }
    }

    ffi_type* ffi_ret = to_ffi_type(return_type);
    ffi_status status;

    if (variadic) {
        status = ffi_prep_cif_var(cif.get(), FFI_DEFAULT_ABI,
                                  static_cast<unsigned int>(fixed_count),
                                  static_cast<unsigned int>(total_count),
                                  ffi_ret, ffi_param_types.data());
    } else {
        status = ffi_prep_cif(cif.get(), FFI_DEFAULT_ABI,
                              static_cast<unsigned int>(total_count),
                              ffi_ret, ffi_param_types.data());
    }

    if (status != FFI_OK) {
        ::havel::error("FFICall: failed to prep cif: {}", static_cast<int>(status));
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(cif_mutex_);
    cif_cache_[key] = cif;
    return cif;
}

Value FFICall::call_native(void* fn_ptr,
                            const std::vector<void*>& arg_ptrs,
                            std::shared_ptr<FFIType> return_type,
                            const std::vector<std::shared_ptr<FFIType>>& param_types,
                            bool variadic,
                            const std::vector<std::shared_ptr<FFIType>>& variadic_types) {
    if (!fn_ptr) {
        ::havel::error("FFICall: call_native: null function pointer");
        return Value::makeNull();
    }

    auto cif = get_or_create_cif(fn_ptr, param_types, return_type, variadic, variadic_types);
    if (!cif) return Value::makeNull();

    size_t ret_size = FFITypeRegistry::size_of(return_type);
    void* ffi_ret_ptr = alloca(ret_size > 0 ? ret_size : 1);
    std::memset(ffi_ret_ptr, 0, ret_size > 0 ? ret_size : 1);
    std::vector<void*> ffi_args(arg_ptrs.begin(), arg_ptrs.end());

    ffi_call(cif.get(), reinterpret_cast<void(*)()>(fn_ptr), ffi_ret_ptr, ffi_args.data());

    return FFIMemory::to_havel(ffi_ret_ptr, return_type);
}

static std::string trim_string(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<FFIDeclaration> FFICall::parse_cdef(const std::string& cdef) {
    std::vector<FFIDeclaration> decls;

    static const std::regex func_regex(R"((\w+)\s+(\w+)\s*\(([^)]*)\)\s*;)");
    static const std::regex const_regex(R"(#define\s+(\w+)\s+((?:0[xX][0-9a-fA-F]+|\d+)(?:[uUlL]{0,2})|(?:\w+)))");
    static const std::regex struct_regex(R"(struct\s+(\w+)\s*\{([^}]*)\})");
    static const std::regex type_regex(R"(typedef\s+([\w\s\*]+?)\s+(\w+)\s*;)");
    static const std::regex var_regex(R"(extern\s+([\w\s\*]+?)\s+(\w+)\s*;)");

    std::sregex_iterator end;

    std::sregex_iterator func_iter(cdef.begin(), cdef.end(), func_regex);
    for (auto it = func_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::FUNCTION;
        decl.name = match[2].str();

        std::string ret_type_str = trim_string(match[1].str());
        decl.type = FFITypeRegistry::from_name(ret_type_str);

        std::string params_str = match[3].str();
        if (!params_str.empty() && trim_string(params_str) != "void") {
            std::istringstream ss(params_str);
            std::string param;
            while (std::getline(ss, param, ',')) {
                param = trim_string(param);
                if (param.empty()) continue;

                size_t last_space = param.rfind(' ');
                if (last_space != std::string::npos && last_space + 1 < param.size()) {
                    std::string type_str = trim_string(param.substr(0, last_space));
                    std::string pname = trim_string(param.substr(last_space + 1));
                    while (!pname.empty() && pname.back() == '*') {
                        type_str += "*";
                        pname.pop_back();
                    }
                    type_str = trim_string(type_str);
                    decl.param_names.push_back(pname);
                    auto pt = FFITypeRegistry::from_name(type_str);
                    if (pt) decl.fields.emplace_back(pname, pt);
                } else {
                    auto pt = FFITypeRegistry::from_name(param);
                    if (pt) {
                        decl.param_names.push_back(param);
                        decl.fields.emplace_back(param, pt);
                    }
                }
            }
        }

        decls.push_back(decl);
    }

    std::sregex_iterator const_iter(cdef.begin(), cdef.end(), const_regex);
    for (auto it = const_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::CONSTANT;
        decl.name = match[1].str();
        std::string val_str = trim_string(match[2].str());

        std::string clean_val = val_str;
        while (!clean_val.empty() && (clean_val.back() == 'u' || clean_val.back() == 'U' ||
                                       clean_val.back() == 'l' || clean_val.back() == 'L')) {
            clean_val.pop_back();
        }

        try {
            if (clean_val.find("0x") == 0 || clean_val.find("0X") == 0) {
                decl.constant_value = std::stoull(clean_val, nullptr, 16);
            } else if (!clean_val.empty() && std::isdigit(static_cast<unsigned char>(clean_val[0]))) {
                decl.constant_value = std::stoull(clean_val);
            } else {
                decl.constant_value = 0;
            }
        } catch (...) {
            decl.constant_value = 0;
        }

        decls.push_back(decl);
    }

    std::sregex_iterator struct_iter(cdef.begin(), cdef.end(), struct_regex);
    for (auto it = struct_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::STRUCT;
        decl.name = match[1].str();

        auto st = FFITypeRegistry::struct_type(decl.name);
        std::string body = match[2].str();
        std::istringstream bss(body);
        std::string line;
        while (std::getline(bss, line)) {
            size_t semi = line.find(';');
            if (semi == std::string::npos) continue;
            line = line.substr(0, semi);
            line = trim_string(line);
            if (line.empty()) continue;

            size_t last_space = line.rfind(' ');
            if (last_space != std::string::npos && last_space + 1 < line.size()) {
                std::string type_str = trim_string(line.substr(0, last_space));
                std::string fname = trim_string(line.substr(last_space + 1));
                while (!fname.empty() && fname.back() == '*') {
                    type_str += "*";
                    fname.pop_back();
                }
                type_str = trim_string(type_str);
                auto ft = FFITypeRegistry::from_name(type_str);
                if (ft) {
                    FFITypeRegistry::add_struct_field(st, fname, ft);
                    decl.fields.emplace_back(fname, ft);
                }
            }
        }
        FFITypeRegistry::compute_layout(st);
        decl.type = st;

        decls.push_back(decl);
    }

    std::sregex_iterator type_iter(cdef.begin(), cdef.end(), type_regex);

    for (auto it = type_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::TYPEDEF;
        std::string underlying = trim_string(match[1].str());
        decl.name = match[2].str();
        auto base = FFITypeRegistry::from_name(underlying);
        if (base) {
            decl.type = base;
            FFITypeRegistry::register_typedef(decl.name, base);
        }
        decls.push_back(decl);
    }

    std::sregex_iterator var_iter(cdef.begin(), cdef.end(), var_regex);
    for (auto it = var_iter; it != end; ++it) {
        std::smatch match = *it;
        FFIDeclaration decl;
        decl.kind = FFIDeclaration::Kind::VARIABLE;
        std::string type_str = trim_string(match[1].str());
        decl.name = match[2].str();
        decl.type = FFITypeRegistry::from_name(type_str);
        decls.push_back(decl);
    }

    return decls;
}

bool FFICall::register_declaration(const FFIDeclaration& decl) {
    (void)decl;
    return true;
}

void FFICall::clear_cif_cache() {
    std::lock_guard<std::mutex> lock(cif_mutex_);
    cif_cache_.clear();
}

} // namespace havel::ffi

#endif // HAVE_LIBFFI
