#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <dlfcn.h>
#include "../core/Value.hpp"

#ifdef HAVE_LIBFFI
#include <ffi.h>
#endif

namespace havel::ffi {

struct FFIType;
using Value = ::havel::core::Value;

class FFICall {
public:
    static void* load_library(const std::string& path);
    static void unload_library(void* handle);
    static void* get_symbol(void* handle, const std::string& name);

    static Value call_native(void* fn_ptr,
                             const std::vector<void*>& arg_ptrs,
                             std::shared_ptr<FFIType> return_type,
                             const std::vector<std::shared_ptr<FFIType>>& param_types,
                             bool variadic = false,
                             const std::vector<std::shared_ptr<FFIType>>& variadic_types = {});

    static void* create_callback(std::function<Value(const std::vector<Value>&)> fn,
                                 std::shared_ptr<FFIType> signature);
    static void destroy_callback(void* callback_ptr);

    static std::vector<struct FFIDeclaration> parse_cdef(const std::string& cdef);
    static bool register_declaration(const struct FFIDeclaration& decl);

    static void clear_cif_cache();

#ifdef HAVE_LIBFFI
    static ffi_type* to_ffi_type(std::shared_ptr<FFIType> type);
#endif

    struct CallbackData {
        std::function<Value(const std::vector<Value>&)>* fn = nullptr;
        std::shared_ptr<FFIType> return_type;
        std::vector<std::shared_ptr<FFIType>> param_types;
        ffi_cif* cif = nullptr;
        std::vector<ffi_type*>* param_types_ffi = nullptr;
        ffi_closure* closure = nullptr;
    };

    struct CifKey {
        void* fn_ptr = nullptr;
        uint8_t return_kind = 0;
        std::vector<uint8_t> param_kinds;
        bool variadic = false;
        std::vector<uint8_t> variadic_kinds;

        bool operator==(const CifKey& o) const {
            return fn_ptr == o.fn_ptr && return_kind == o.return_kind &&
                   param_kinds == o.param_kinds && variadic == o.variadic &&
                   variadic_kinds == o.variadic_kinds;
        }
    };

    struct CifKeyHash {
        size_t operator()(const CifKey& k) const {
            size_t h = std::hash<void*>()(k.fn_ptr);
            h ^= std::hash<uint8_t>()(k.return_kind) << 1;
            for (auto pk : k.param_kinds) h ^= std::hash<uint8_t>()(pk) << 1;
            h ^= std::hash<bool>()(k.variadic) << 1;
            for (auto vk : k.variadic_kinds) h ^= std::hash<uint8_t>()(vk) << 2;
            return h;
        }
    };

private:
    static std::unordered_map<void*, std::string> libraries_;
    static std::mutex callback_mutex_;
    static std::unordered_map<void*, std::unique_ptr<CallbackData>> callbacks_;
    static std::mutex cif_mutex_;
    static std::unordered_map<CifKey, std::shared_ptr<ffi_cif>, CifKeyHash> cif_cache_;

    static CifKey make_cif_key(void* fn_ptr,
                               const std::vector<std::shared_ptr<FFIType>>& param_types,
                               std::shared_ptr<FFIType> return_type,
                               bool variadic,
                               const std::vector<std::shared_ptr<FFIType>>& variadic_types);

    static std::shared_ptr<ffi_cif> get_or_create_cif(void* fn_ptr,
                                                       const std::vector<std::shared_ptr<FFIType>>& param_types,
                                                       std::shared_ptr<FFIType> return_type,
                                                       bool variadic,
                                                       const std::vector<std::shared_ptr<FFIType>>& variadic_types);
};

struct FFIDeclaration {
    enum class Kind { TYPEDEF, STRUCT, UNION, FUNCTION, VARIABLE, CONSTANT };
    Kind kind;
    std::string name;
    std::shared_ptr<FFIType> type;
    std::vector<std::pair<std::string, std::shared_ptr<FFIType>>> fields;
    std::vector<std::string> param_names;
    bool is_variadic = false;
    void* function_ptr = nullptr;
    uint64_t constant_value = 0;
};

} 
