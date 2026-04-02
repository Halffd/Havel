/*
 * HavelValue.h - Opaque value representation for C ABI
 *
 * This is the core type that extensions use to communicate with the VM.
 * All values are passed by pointer (HavelValue*), never by value.
 *
 * Memory ownership:
 * - VM creates values, extensions consume them
 * - Extensions create return values, VM frees them
 * - String data is copied, not referenced
 */

#ifndef HAVEL_VALUE_H
#define HAVEL_VALUE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * Forward declarations
 */
typedef struct HavelValue HavelValue;
typedef struct HavelArray HavelArray;
typedef struct HavelObject HavelObject;

/**
 * Destructor function for handles
 */
typedef void (*HavelHandleDestructor)(void*);

/**
 * HavelValue - Opaque value handle
 *
 * Extensions should NOT access internals directly.
 * Use the API functions to create/access values.
 */
struct HavelValue {
    HavelValueType type;
    union {
        int boolean;
        int64_t integer;
        double floating;
        char* string;
        HavelArray* array;
        HavelObject* object;
        struct {
            void* ptr;
            HavelHandleDestructor destructor;
        } handle;
    } data;
    
    /* Reference counting for GC integration */
    int ref_count;
};

/**
 * HavelArray - Dynamic array of values
 */
struct HavelArray {
    HavelValue** values;
    size_t length;
    size_t capacity;
};

/**
 * HavelObject - Key-value object (like JS object / Python dict)
 */
struct HavelObject {
    char** keys;
    HavelValue** values;
    size_t count;
    size_t capacity;
};

/* ==========================================================================
 * Value creation (VM allocates, extension consumes)
 * ========================================================================== */

HavelValue* havel_new_null(void);
HavelValue* havel_new_bool(int b);
HavelValue* havel_new_int(int64_t i);
HavelValue* havel_new_float(double f);
HavelValue* havel_new_string(const char* s);
HavelValue* havel_new_handle(void* ptr, HavelHandleDestructor destructor);
HavelValue* havel_new_array(size_t initial_capacity);
HavelValue* havel_new_object(void);

/* ==========================================================================
 * Value access (read-only, no ownership transfer)
 * ========================================================================== */

HavelValueType havel_get_type(const HavelValue* v);
int havel_get_bool(const HavelValue* v);
int64_t havel_get_int(const HavelValue* v);
double havel_get_float(const HavelValue* v);
const char* havel_get_string(const HavelValue* v);
void* havel_get_handle(const HavelValue* v);

/* ==========================================================================
 * Array operations
 * ========================================================================== */

size_t havel_array_length(const HavelValue* arr);
HavelValue* havel_array_get(const HavelValue* arr, size_t index);
void havel_array_set(HavelValue* arr, size_t index, HavelValue* v);
void havel_array_push(HavelValue* arr, HavelValue* v);

/* ==========================================================================
 * Object operations
 * ========================================================================== */

void havel_object_set(HavelValue* obj, const char* key, HavelValue* v);
HavelValue* havel_object_get(const HavelValue* obj, const char* key);

/* ==========================================================================
 * Memory management
 * ========================================================================== */

void havel_free_value(HavelValue* v);
void havel_incref(HavelValue* v);
void havel_decref(HavelValue* v);

#ifdef __cplusplus
}
#endif

#endif /* HAVEL_VALUE_H */
