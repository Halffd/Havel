#pragma once

/**
 * HavelCAPI.h - Stable C ABI for native extensions
 *
 * This is the ONLY stable interface extensions should use.
 * C++ wrappers can be built on top, but the core ABI is C.
 *
 * Why C ABI:
 * - Compiler-independent (GCC, Clang, MSVC all agree on C ABI)
 * - Version-stable (struct layout doesn't change)
 * - Language-agnostic (can write extensions in C, C++, Rust, Zig)
 * - dlopen/dlsym compatible
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * HavelValue - Opaque value handle
 *
 * Extensions don't see VM internals.
 * Values are passed by handle, not by value.
 */
typedef struct HavelValue HavelValue;

/**
 * Value type tags
 */
typedef enum HavelValueType {
    HAVEL_NULL = 0,
    HAVEL_BOOL = 1,
    HAVEL_INT = 2,
    HAVEL_FLOAT = 3,
    HAVEL_STRING = 4,
    HAVEL_ARRAY = 5,
    HAVEL_OBJECT = 6,
    HAVEL_HANDLE = 7  /* Opaque handle for resources */
} HavelValueType;

/**
 * Function signature for extension exports
 * 
 * @param argc Number of arguments
 * @param argv Array of argument values
 * @return Result value (must be freed by VM)
 */
typedef HavelValue* (*HavelNativeFn)(int argc, HavelValue** argv);

/**
 * Extension API function table
 *
 * This is the ONLY thing extensions see.
 * Adding new functions to this struct is ABI-compatible
 * as long as we only append to the end.
 */
typedef struct HavelAPI {
    /* Version - bump when ABI changes */
    int version;
    
    /* Module registration */
    void (*register_function)(const char* module, const char* name, HavelNativeFn fn);
    
    /* Value creation */
    HavelValue* (*new_null)(void);
    HavelValue* (*new_bool)(int b);
    HavelValue* (*new_int)(int64_t i);
    HavelValue* (*new_float)(double f);
    HavelValue* (*new_string)(const char* s);
    HavelValue* (*new_handle)(void* ptr, void (*destructor)(void*));
    
    /* Value access */
    HavelValueType (*get_type)(HavelValue* v);
    int (*get_bool)(HavelValue* v);
    int64_t (*get_int)(HavelValue* v);
    double (*get_float)(HavelValue* v);
    const char* (*get_string)(HavelValue* v);
    void* (*get_handle)(HavelValue* v);
    
    /* Array operations */
    HavelValue* (*new_array)(size_t initial_capacity);
    size_t (*array_length)(HavelValue* arr);
    HavelValue* (*array_get)(HavelValue* arr, size_t index);
    void (*array_push)(HavelValue* arr, HavelValue* v);
    
    /* Object operations */
    HavelValue* (*new_object)(void);
    void (*object_set)(HavelValue* obj, const char* key, HavelValue* v);
    HavelValue* (*object_get)(HavelValue* obj, const char* key);
    
    /* Memory management */
    void (*free_value)(HavelValue* v);
    
    /* Host services (sandboxed access) */
    void* (*get_host_service)(const char* name);  /* Returns service interface or NULL */
    
    /* Reserved for future expansion - DO NOT USE */
    void (*reserved_1)(void);
    void (*reserved_2)(void);
    void (*reserved_3)(void);
    void (*reserved_4)(void);
    void (*reserved_5)(void);
} HavelAPI;

/**
 * Extension entry point
 *
 * Every extension must export this symbol:
 * 
 * extern "C" void havel_extension_init(HavelAPI* api);
 */
typedef void (*HavelExtensionInit)(HavelAPI* api);

/**
 * Extension metadata (optional but recommended)
 */
typedef struct HavelExtensionInfo {
    const char* name;
    const char* version;
    const char* description;
    int api_version;  /* Minimum required API version */
} HavelExtensionInfo;

/* Current API version - increment when breaking changes are made */
#define HAVEL_API_VERSION 1

/**
 * Helper macro for defining extension entry point
 *
 * Usage:
 *   HAVEL_EXTENSION(my_extension) {
 *     // registration code
 *   }
 */
#define HAVEL_EXTENSION(name) \
    static void name##_init(HavelAPI* api); \
    extern "C" void havel_extension_init(HavelAPI* api) { \
        name##_init(api); \
    } \
    static void name##_init(HavelAPI* api)

#ifdef __cplusplus
}
#endif
