#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
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

    static Value call_function(void* fn_ptr,
                               const std::vector<Value>& args,
                               std::shared_ptr<FFIType> return_type,
                               const std::vector<std::shared_ptr<FFIType>>& param_types,
                               bool variadic = false,
                               const std::vector<std::shared_ptr<FFIType>>& variadic_types = {});

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

#ifdef HAVE_LIBFFI
    static ffi_type* to_ffi_type(std::shared_ptr<FFIType> type);
#endif

private:
    static std::unordered_map<void*, std::string> libraries_;
    static std::unordered_map<void*, std::function<Value(const std::vector<Value>&)>> callbacks_;
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