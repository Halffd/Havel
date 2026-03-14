// Havel-cAPI.h
// C API for Havel language embedding
// Use this for embedding in C or other languages with C bindings

#ifndef HAVEL_CAPI_H
#define HAVEL_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque types
// ============================================================================

typedef struct HavelVM HavelVM;
typedef struct HavelValue HavelValue;
typedef struct HavelResult HavelResult;

// ============================================================================
// Value types
// ============================================================================

typedef enum {
    HAVEL_NIL,
    HAVEL_BOOL,
    HAVEL_NUMBER,
    HAVEL_STRING,
    HAVEL_ARRAY,
    HAVEL_OBJECT,
    HAVEL_FUNCTION,
    HAVEL_STRUCT,
    HAVEL_ERROR
} HavelType;

// ============================================================================
// VM functions
// ============================================================================

// Create/destroy VM
HavelVM* havel_vm_create(void);
void havel_vm_destroy(HavelVM* vm);

// Load and execute code
HavelResult* havel_vm_load(HavelVM* vm, const char* code, const char* sourceName);
HavelResult* havel_vm_loadFile(HavelVM* vm, const char* path);

// Call functions
HavelResult* havel_vm_call(HavelVM* vm, const char* funcName, 
                           HavelValue** args, int argCount);

// Globals
HavelValue* havel_vm_getGlobal(HavelVM* vm, const char* name);
void havel_vm_setGlobal(HavelVM* vm, const char* name, HavelValue* value);

// Native functions
typedef HavelValue* (*HavelNativeFn)(HavelVM* vm, HavelValue** args, int argCount);
void havel_vm_registerFn(HavelVM* vm, const char* name, HavelNativeFn fn);

// Host context
void havel_vm_setHostContext(HavelVM* vm, void* ctx);
void* havel_vm_getHostContext(HavelVM* vm);

// Error handling
const char* havel_result_error(HavelResult* result);
int havel_result_ok(HavelResult* result);
HavelValue* havel_result_value(HavelResult* result);
void havel_result_destroy(HavelResult* result);

// ============================================================================
// Value functions
// ============================================================================

// Create values
HavelValue* havel_value_nil(void);
HavelValue* havel_value_bool(int b);
HavelValue* havel_value_number(double n);
HavelValue* havel_value_string(const char* s);

// Type checks
HavelType havel_value_type(HavelValue* val);
int havel_value_isNil(HavelValue* val);
int havel_value_isBool(HavelValue* val);
int havel_value_isNumber(HavelValue* val);
int havel_value_isString(HavelValue* val);
int havel_value_isArray(HavelValue* val);
int havel_value_isObject(HavelValue* val);
int havel_value_isFunction(HavelValue* val);
int havel_value_isStruct(HavelValue* val);

// Conversions
int havel_value_asBool(HavelValue* val);
double havel_value_asNumber(HavelValue* val);
const char* havel_value_asString(HavelValue* val);
char* havel_value_toString(HavelValue* val);  // Caller must free

// Destroy value
void havel_value_destroy(HavelValue* val);

// ============================================================================
// Array functions
// ============================================================================

int havel_array_size(HavelValue* arr);
HavelValue* havel_array_get(HavelValue* arr, int index);
void havel_array_set(HavelValue* arr, int index, HavelValue* val);
void havel_array_push(HavelValue* arr, HavelValue* val);
HavelValue* havel_array_pop(HavelValue* arr);

// ============================================================================
// Object functions
// ============================================================================

HavelValue* havel_object_get(HavelValue* obj, const char* key);
void havel_object_set(HavelValue* obj, const char* key, HavelValue* val);
int havel_object_has(HavelValue* obj, const char* key);

// ============================================================================
// Version info
// ============================================================================

const char* havel_version(void);
int havel_version_major(void);
int havel_version_minor(void);
int havel_version_patch(void);

#ifdef __cplusplus
}
#endif

#endif // HAVEL_CAPI_H
