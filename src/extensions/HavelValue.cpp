/*
 * HavelValue.cpp - Opaque value implementation
 *
 * Memory management:
 * - All values are reference counted
 * - Strings are deep copied
 * - Handles have optional destructors
 */

#include "HavelValue.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

extern "C" {

/* ==========================================================================
 * Value creation
 * ========================================================================== */

HavelValue* havel_new_null(void) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_NULL;
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_bool(int b) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_BOOL;
    v->data.boolean = b ? 1 : 0;
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_int(int64_t i) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_INT;
    v->data.integer = i;
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_float(double f) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_FLOAT;
    v->data.floating = f;
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_string(const char* s) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_STRING;
    if (s) {
        v->data.string = strdup(s);
    } else {
        v->data.string = nullptr;
    }
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_handle(void* ptr, HavelHandleDestructor destructor) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_HANDLE;
    v->data.handle.ptr = ptr;
    v->data.handle.destructor = destructor;
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_array(size_t initial_capacity) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_ARRAY;
    
    HavelArray* arr = (HavelArray*)calloc(1, sizeof(HavelArray));
    if (!arr) {
        free(v);
        return nullptr;
    }
    
    arr->capacity = initial_capacity > 0 ? initial_capacity : 8;
    arr->values = (HavelValue**)calloc(arr->capacity, sizeof(HavelValue*));
    if (!arr->values) {
        free(arr);
        free(v);
        return nullptr;
    }
    
    v->data.array = arr;
    v->ref_count = 1;
    return v;
}

HavelValue* havel_new_object(void) {
    HavelValue* v = (HavelValue*)calloc(1, sizeof(HavelValue));
    if (!v) return nullptr;
    v->type = HAVEL_OBJECT;
    
    HavelObject* obj = (HavelObject*)calloc(1, sizeof(HavelObject));
    if (!obj) {
        free(v);
        return nullptr;
    }
    
    obj->capacity = 8;
    obj->keys = (char**)calloc(obj->capacity, sizeof(char*));
    obj->values = (HavelValue**)calloc(obj->capacity, sizeof(HavelValue*));
    if (!obj->keys || !obj->values) {
        free(obj->keys);
        free(obj->values);
        free(obj);
        free(v);
        return nullptr;
    }
    
    v->data.object = obj;
    v->ref_count = 1;
    return v;
}

/* ==========================================================================
 * Value access
 * ========================================================================== */

HavelValueType havel_get_type(const HavelValue* v) {
    return v ? v->type : HAVEL_NULL;
}

int havel_get_bool(const HavelValue* v) {
    if (!v || v->type != HAVEL_BOOL) return 0;
    return v->data.boolean;
}

int64_t havel_get_int(const HavelValue* v) {
    if (!v || v->type != HAVEL_INT) return 0;
    return v->data.integer;
}

double havel_get_float(const HavelValue* v) {
    if (!v || v->type != HAVEL_FLOAT) return 0.0;
    return v->data.floating;
}

const char* havel_get_string(const HavelValue* v) {
    if (!v || v->type != HAVEL_STRING) return nullptr;
    return v->data.string;
}

void* havel_get_handle(const HavelValue* v) {
    if (!v || v->type != HAVEL_HANDLE) return nullptr;
    return v->data.handle.ptr;
}

/* ==========================================================================
 * Array operations
 * ========================================================================== */

size_t havel_array_length(const HavelValue* arr) {
    if (!arr || arr->type != HAVEL_ARRAY) return 0;
    return arr->data.array->length;
}

HavelValue* havel_array_get(const HavelValue* arr, size_t index) {
    if (!arr || arr->type != HAVEL_ARRAY) return nullptr;
    HavelArray* a = arr->data.array;
    if (index >= a->length) return nullptr;
    return a->values[index];
}

void havel_array_push(HavelValue* arr, HavelValue* v) {
    if (!arr || arr->type != HAVEL_ARRAY || !v) return;
    HavelArray* a = arr->data.array;
    
    /* Grow if needed */
    if (a->length >= a->capacity) {
        size_t new_capacity = a->capacity * 2;
        HavelValue** new_values = (HavelValue**)realloc(a->values, 
            new_capacity * sizeof(HavelValue*));
        if (!new_values) return;
        a->values = new_values;
        a->capacity = new_capacity;
    }
    
    a->values[a->length++] = v;
    havel_incref(v);
}

/* ==========================================================================
 * Object operations
 * ========================================================================== */

void havel_object_set(HavelValue* obj, const char* key, HavelValue* v) {
    if (!obj || obj->type != HAVEL_OBJECT || !key || !v) return;
    HavelObject* o = obj->data.object;
    
    /* Check if key exists */
    for (size_t i = 0; i < o->count; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            havel_decref(o->values[i]);
            o->values[i] = v;
            havel_incref(v);
            return;
        }
    }
    
    /* Grow if needed */
    if (o->count >= o->capacity) {
        size_t new_capacity = o->capacity * 2;
        char** new_keys = (char**)realloc(o->keys, new_capacity * sizeof(char*));
        HavelValue** new_values = (HavelValue**)realloc(o->values, 
            new_capacity * sizeof(HavelValue*));
        if (!new_keys || !new_values) return;
        o->keys = new_keys;
        o->values = new_values;
        o->capacity = new_capacity;
    }
    
    o->keys[o->count] = strdup(key);
    o->values[o->count] = v;
    o->count++;
    havel_incref(v);
}

HavelValue* havel_object_get(const HavelValue* obj, const char* key) {
    if (!obj || obj->type != HAVEL_OBJECT || !key) return nullptr;
    HavelObject* o = obj->data.object;
    
    for (size_t i = 0; i < o->count; i++) {
        if (strcmp(o->keys[i], key) == 0) {
            return o->values[i];
        }
    }
    return nullptr;
}

/* ==========================================================================
 * Memory management
 * ========================================================================== */

static void havel_array_free(HavelArray* arr) {
    if (!arr) return;
    for (size_t i = 0; i < arr->length; i++) {
        havel_decref(arr->values[i]);
    }
    free(arr->values);
    free(arr);
}

static void havel_object_free(HavelObject* obj) {
    if (!obj) return;
    for (size_t i = 0; i < obj->count; i++) {
        free(obj->keys[i]);
        havel_decref(obj->values[i]);
    }
    free(obj->keys);
    free(obj->values);
    free(obj);
}

void havel_free_value(HavelValue* v) {
    if (!v) return;
    
    switch (v->type) {
        case HAVEL_STRING:
            free(v->data.string);
            break;
        case HAVEL_ARRAY:
            havel_array_free(v->data.array);
            break;
        case HAVEL_OBJECT:
            havel_object_free(v->data.object);
            break;
        case HAVEL_HANDLE:
            if (v->data.handle.destructor) {
                v->data.handle.destructor(v->data.handle.ptr);
            }
            break;
        default:
            break;
    }
    
    free(v);
}

void havel_incref(HavelValue* v) {
    if (!v) return;
    v->ref_count++;
}

void havel_decref(HavelValue* v) {
    if (!v) return;
    v->ref_count--;
    if (v->ref_count <= 0) {
        havel_free_value(v);
    }
}

} /* extern "C" */
