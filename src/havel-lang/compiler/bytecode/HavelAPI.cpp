/*
 * HavelAPI.cpp - Implementation of C API function table for extensions
 *
 * This wires up the HavelAPI function table to connect extension functions
 * to VM execution. Extensions call register_function() which registers
 * their functions with the HostBridge, making them available to Havel scripts.
 */

#include "HavelCAPI.h"
#include "HavelValue.h"
#include "HostBridge.hpp"
#include "VM.hpp"

#include <cstring>
#include <cstdio>

namespace havel::compiler {

/* ==========================================================================
 * Static wrapper functions - convert C API to C++ implementation
 * ========================================================================== */

/* Value creation */
static HavelValue* api_new_null(void) { return havel_new_null(); }
static HavelValue* api_new_bool(int b) { return havel_new_bool(b); }
static HavelValue* api_new_int(int64_t i) { return havel_new_int(i); }
static HavelValue* api_new_float(double f) { return havel_new_float(f); }
static HavelValue* api_new_string(const char* s) { return havel_new_string(s); }
static HavelValue* api_new_handle(void* ptr, HavelHandleDestructor destructor) {
    return havel_new_handle(ptr, destructor);
}
static HavelValue* api_new_array(size_t initial_capacity) {
    return havel_new_array(initial_capacity);
}
static HavelValue* api_new_object(void) { return havel_new_object(); }

/* Value access */
static HavelValueType api_get_type(HavelValue* v) { return havel_get_type(v); }
static int api_get_bool(HavelValue* v) { return havel_get_bool(v); }
static int64_t api_get_int(HavelValue* v) { return havel_get_int(v); }
static double api_get_float(HavelValue* v) { return havel_get_float(v); }
static const char* api_get_string(HavelValue* v) { return havel_get_string(v); }
static void* api_get_handle(HavelValue* v) { return havel_get_handle(v); }

/* Array operations */
static size_t api_array_length(HavelValue* arr) { return havel_array_length(arr); }
static HavelValue* api_array_get(HavelValue* arr, size_t index) {
    return havel_array_get(arr, index);
}
static void api_array_push(HavelValue* arr, HavelValue* v) {
    havel_array_push(arr, v);
}

/* Object operations */
static void api_object_set(HavelValue* obj, const char* key, HavelValue* v) {
    havel_object_set(obj, key, v);
}
static HavelValue* api_object_get(HavelValue* obj, const char* key) {
    return havel_object_get(obj, key);
}

/* Memory management */
static void api_free_value(HavelValue* v) { havel_free_value(v); }
static void api_incref(HavelValue* v) { havel_incref(v); }
static void api_decref(HavelValue* v) { havel_decref(v); }

/* Host services - stub for now */
static void* api_get_host_service(const char* name) {
    (void)name;
    return nullptr;  /* TODO: Wire up to HostBridge services */
}

/* ==========================================================================
 * Extension function registration
 * ========================================================================== */

/**
 * Wrapper that converts C API call to C++ BytecodeValue call
 */
struct ExtensionFunctionWrapper {
    HavelNativeFn c_function;
    
    static BytecodeValue callWrapper(const std::vector<BytecodeValue>& args, 
                                      void* userData) {
        auto* wrapper = static_cast<ExtensionFunctionWrapper*>(userData);
        if (!wrapper || !wrapper->c_function) {
            return BytecodeValue(nullptr);
        }
        
        /* Convert BytecodeValue args to HavelValue args */
        std::vector<HavelValue*> c_args;
        c_args.reserve(args.size());
        
        for (const auto& arg : args) {
            HavelValue* havelVal = nullptr;
            
            /* Convert BytecodeValue to HavelValue */
            if (std::holds_alternative<std::nullptr_t>(arg)) {
                havelVal = havel_new_null();
            } else if (std::holds_alternative<bool>(arg)) {
                havelVal = havel_new_bool(std::get<bool>(arg) ? 1 : 0);
            } else if (std::holds_alternative<int64_t>(arg)) {
                havelVal = havel_new_int(std::get<int64_t>(arg));
            } else if (std::holds_alternative<double>(arg)) {
                havelVal = havel_new_float(std::get<double>(arg));
            } else if (std::holds_alternative<std::string>(arg)) {
                havelVal = havel_new_string(std::get<std::string>(arg).c_str());
            } else {
                havelVal = havel_new_null();  /* Unsupported type */
            }
            
            c_args.push_back(havelVal);
        }
        
        /* Call C extension function */
        HavelValue* result = wrapper->c_function(
            static_cast<int>(c_args.size()), 
            c_args.data()
        );
        
        /* Convert HavelValue result to BytecodeValue */
        BytecodeValue bytecodeResult;
        if (result) {
            switch (havel_get_type(result)) {
                case HAVEL_NULL:
                    bytecodeResult = BytecodeValue(nullptr);
                    break;
                case HAVEL_BOOL:
                    bytecodeResult = BytecodeValue(havel_get_bool(result) != 0);
                    break;
                case HAVEL_INT:
                    bytecodeResult = BytecodeValue(havel_get_int(result));
                    break;
                case HAVEL_FLOAT:
                    bytecodeResult = BytecodeValue(havel_get_float(result));
                    break;
                case HAVEL_STRING: {
                    const char* str = havel_get_string(result);
                    bytecodeResult = BytecodeValue(str ? str : "");
                    break;
                }
                case HAVEL_HANDLE: {
                    /* For now, handles return nullptr to VM */
                    bytecodeResult = BytecodeValue(nullptr);
                    break;
                }
                case HAVEL_ARRAY:
                case HAVEL_OBJECT:
                    /* Complex types not yet supported */
                    bytecodeResult = BytecodeValue(nullptr);
                    break;
            }
            
            /* Free the result - ownership transferred */
            havel_decref(result);
        } else {
            bytecodeResult = BytecodeValue(nullptr);
        }
        
        /* Free argument values */
        for (auto* val : c_args) {
            havel_decref(val);
        }
        
        return bytecodeResult;
    }
};

/**
 * Register extension function with HostBridge
 */
static void api_register_function(const char* module, const char* name, 
                                   HavelNativeFn fn) {
    if (!module || !name || !fn) {
        fprintf(stderr, "[HavelAPI] Invalid arguments to register_function\n");
        return;
    }
    
    /* Create wrapper that converts between C and C++ calling conventions */
    auto* wrapper = new ExtensionFunctionWrapper{fn};
    
    /* Create C++ function that calls the wrapper */
    BytecodeHostFunction cppFn = [wrapper](const std::vector<BytecodeValue>& args) {
        return ExtensionFunctionWrapper::callWrapper(args, wrapper);
    };
    
    /* Register with HostBridge - this is called during HostBridge::install() */
    /* The function name format is "module.function" */
    std::string fullName = std::string(module) + "." + name;
    
    /* Store in options for later registration */
    /* Note: This requires HostBridge to be accessible */
    /* For now, we store in a global registry */
    static std::unordered_map<std::string, BytecodeHostFunction> extensionFunctions;
    extensionFunctions[fullName] = std::move(cppFn);
    
    printf("[HavelAPI] Registered extension function: %s\n", fullName.c_str());
}

/* ==========================================================================
 * Global HavelAPI instance
 * ========================================================================== */

/**
 * Global HavelAPI function table
 * All extensions share the same API instance
 */
static HavelAPI g_havelAPI = {
    .version = 1,
    
    /* Module registration */
    .register_function = api_register_function,
    
    /* Value creation */
    .new_null = api_new_null,
    .new_bool = api_new_bool,
    .new_int = api_new_int,
    .new_float = api_new_float,
    .new_string = api_new_string,
    .new_handle = api_new_handle,
    .new_array = api_new_array,
    .new_object = api_new_object,
    
    /* Value access */
    .get_type = api_get_type,
    .get_bool = api_get_bool,
    .get_int = api_get_int,
    .get_float = api_get_float,
    .get_string = api_get_string,
    .get_handle = api_get_handle,
    
    /* Array operations */
    .array_length = api_array_length,
    .array_get = api_array_get,
    .array_push = api_array_push,
    
    /* Object operations */
    .object_set = api_object_set,
    .object_get = api_object_get,
    
    /* Memory management */
    .free_value = api_free_value,
    .incref = api_incref,
    .decref = api_decref,
    
    /* Host services */
    .get_host_service = api_get_host_service,
    
    /* Reserved */
    .reserved_1 = nullptr,
    .reserved_2 = nullptr,
    .reserved_3 = nullptr,
    .reserved_4 = nullptr,
    .reserved_5 = nullptr,
};

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * Get the global HavelAPI instance
 * Extensions call this to get the function table
 */
HavelAPI* getHavelAPI(void) {
    return &g_havelAPI;
}

/**
 * Get registered extension functions
 * HostBridge calls this during install() to register extension functions
 */
const std::unordered_map<std::string, BytecodeHostFunction>& getExtensionFunctions() {
    static std::unordered_map<std::string, BytecodeHostFunction> functions;
    return functions;
}

/**
 * Register extension function directly with HostBridge
 * Called by ExtensionLoader after loading extension
 */
void registerExtensionFunction(const std::string& fullName, BytecodeHostFunction fn) {
    auto& functions = const_cast<std::unordered_map<std::string, BytecodeHostFunction>&>(
        getExtensionFunctions());
    functions[fullName] = std::move(fn);
    printf("[HavelAPI] Registered: %s\n", fullName.c_str());
}

} /* namespace havel::compiler */
