/*
 * HavelAPI.cpp - Implementation of C API function table for extensions
 *
 * This wires up the HavelAPI function table to connect extension functions
 * to VM execution. Extensions call register_function() which registers
 * their functions with the HostBridge, making them available to Havel scripts.
 */

#include "../../../extensions/HavelCAPI.h"
#include "../../../extensions/HavelValue.h"
#include "../vm/VM.hpp"
#include "HavelAPI.hpp"
#include "utils/Logger.hpp"

#include <cstring>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>

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
 * Registry of functions registered through the C API (api_register_function)
 * plus the VM recorded at drain time for value conversion.
 *
 * This used to be a function-local static inside api_register_function with
 * no reader at all: every extension function registered through the C ABI
 * was silently discarded, which is how the qt.* / gtk.* namespaces went dark
 * after their extensions moved into the toolkit plugins. Modules::install
 * drains it via takeRegisteredExtensionFunctions().
 */
static std::unordered_map<std::string, BytecodeHostFunction> g_extensionFunctions;
static std::mutex g_extensionFunctionsMutex;
static VM* g_extension_vm = nullptr;

/* ==========================================================================
 * Bidirectional value conversion (declared in HavelAPI.hpp)
 * ==========================================================================
 *
 * Handles round-trip through g_handle_registry: a HAVEL_HANDLE result is
 * wrapped in a VM host object carrying the "__capi_handle" id field, and
 * converting such an object back returns the registered HavelValue with
 * pointer identity. That is the contract C-ABI widget APIs (qt.*, gtk.*)
 * rely on: widgetNew(...) hands the script an opaque object the script
 * can only pass back to other qt.* calls.
 *
 * Known limit: VM objects are garbage-collected without a finalizer into
 * this registry, so the small registry entry (and the +1 reference it
 * pins) lives until process exit. The C values wrap pointers owned by
 * the toolkit, so the leak is a few dozen bytes per handle.
 */

static std::unordered_map<int64_t, HavelValue*> g_handle_registry;
static std::mutex g_handle_registry_mutex;
static std::atomic<int64_t> g_next_handle_id{1};

static constexpr int kCapiMaxDepth = 16;

static const char* kCapiHandleMarker = "__capi_handle";

HavelValue* valueToHavelValue(VM* vm, const Value& v, int depth) {
    if (depth > kCapiMaxDepth) return havel_new_null();

    if (v.isNull()) {
        return havel_new_null();
    }
    if (v.isBool()) {
        return havel_new_bool(v.asBool() ? 1 : 0);
    }
    if (v.isInt()) {
        return havel_new_int(v.asInt());
    }
    if (v.isDouble()) {
        return havel_new_float(v.asDouble());
    }
    if ((v.isStringValId() || v.isStringId()) && vm) {
        std::string s = vm->resolveStringKey(v);
        return havel_new_string(s.c_str());
    }
    if (v.isObjectId() && vm) {
        // Marker object: hand back the registered handle with identity.
        auto ref = havel::compiler::ObjectRef{v.asObjectId(), true};
        Value marker = vm->getHostObjectField(ref, kCapiHandleMarker);
        if (marker.isInt()) {
            std::lock_guard<std::mutex> lk(g_handle_registry_mutex);
            auto it = g_handle_registry.find(marker.asInt());
            if (it != g_handle_registry.end()) {
                havel_incref(it->second);
                return it->second;
            }
        }
        // Plain object: convert fields to a C object value.
        HavelValue* obj = havel_new_object();
        for (const auto& key : vm->getHostObjectKeys(ref)) {
            Value field = vm->getHostObjectField(ref, key);
            HavelValue* field_c = valueToHavelValue(vm, field, depth + 1);
            havel_object_set(obj, key.c_str(), field_c);
            havel_decref(field_c); // object_set takes its own reference
        }
        return obj;
    }
    if (v.isArrayId() && vm) {
        auto ref = havel::compiler::ArrayRef{v.asArrayId()};
        size_t len = vm->getHostArrayLength(ref);
        HavelValue* arr = havel_new_array(len ? len : 4);
        for (size_t i = 0; i < len; ++i) {
            HavelValue* elem = valueToHavelValue(
                vm, vm->getHostArrayValue(ref, i), depth + 1);
            havel_array_push(arr, elem);
            havel_decref(elem); // push takes its own reference
        }
        return arr;
    }
    /* Unsupported VM types (enums, closures, coroutines, ...) */
    return havel_new_null();
}

Value havelValueToValue(VM* vm, HavelValue* hv, int depth) {
    if (!hv) return Value::makeNull();
    if (depth > kCapiMaxDepth) return Value::makeNull();

    switch (havel_get_type(hv)) {
        case HAVEL_NULL:
            return Value::makeNull();
        case HAVEL_BOOL:
            return Value::makeBool(havel_get_bool(hv) != 0);
        case HAVEL_INT:
            return Value::makeInt(havel_get_int(hv));
        case HAVEL_FLOAT:
            return Value::makeDouble(havel_get_float(hv));
        case HAVEL_STRING: {
            const char* s = havel_get_string(hv);
            if (vm && s) {
                return Value::makeStringId(
                    vm->createRuntimeString(std::string(s)).id);
            }
            return Value::makeNull();
        }
        case HAVEL_HANDLE: {
            // Register and wrap: the VM object carries the registry id.
            if (!vm) return Value::makeNull();
            havel_incref(hv);
            int64_t id = g_next_handle_id.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(g_handle_registry_mutex);
                g_handle_registry[id] = hv;
            }
            auto obj = vm->createHostObject();
            vm->setHostObjectField(obj, kCapiHandleMarker,
                                   Value::makeInt(id));
            return Value::makeObjectId(obj.id);
        }
        case HAVEL_ARRAY: {
            if (!vm) return Value::makeNull();
            size_t len = havel_array_length(hv);
            auto arr = vm->createHostArray();
            for (size_t i = 0; i < len; ++i) {
                HavelValue* elem = havel_array_get(hv, i);
                if (!elem) {
                    vm->pushHostArrayValue(arr, Value::makeNull());
                    continue;
                }
                vm->pushHostArrayValue(
                    arr, havelValueToValue(vm, elem, depth + 1));
            }
            return Value::makeArrayId(arr.id);
        }
        case HAVEL_OBJECT: {
            if (!vm) return Value::makeNull();
            auto obj = vm->createHostObject();
            size_t count = havel_object_count(hv);
            for (size_t i = 0; i < count; ++i) {
                const char* key = havel_object_key(hv, i);
                HavelValue* field = key ? havel_object_get(hv, key) : nullptr;
                if (!key) continue;
                vm->setHostObjectField(
                    obj, key,
                    havelValueToValue(vm, field, depth + 1));
            }
            return Value::makeObjectId(obj.id);
        }
    }
    return Value::makeNull();
}

/**
 * Wrapper that converts C API call to C++ Value call
 */
struct ExtensionFunctionWrapper {
    HavelNativeFn c_function;

    static Value callWrapper(const std::vector<Value>& args,
                              void* userData) {
        auto* wrapper = static_cast<ExtensionFunctionWrapper*>(userData);
        if (!wrapper || !wrapper->c_function) {
            return Value::makeNull();
        }

        /* Convert Value args to HavelValue args (strings, handles and
         * collections included — not just primitives) */
        std::vector<HavelValue*> c_args;
        c_args.reserve(args.size());
        for (const auto& arg : args) {
            c_args.push_back(valueToHavelValue(g_extension_vm, arg));
        }

        /* Call C extension function */
        HavelValue* result = wrapper->c_function(
            static_cast<int>(c_args.size()),
            c_args.data()
        );

        /* Convert HavelValue result to Value (borrows; registry pins
         * handle results itself) */
        Value bytecodeResult = havelValueToValue(g_extension_vm, result);

        if (result) {
            /* Free the result - ownership transferred */
            havel_decref(result);
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
        return;
    }

    /* Create wrapper that converts between C and C++ calling conventions.
     * Owned by the host-function lambda via shared_ptr: the registry map may
     * overwrite entries (repeat registrations) and BytecodeHostFunction is
     * copied around, so raw new here leaked every wrapper (ASan flagged 87
     * leaks per qt registration pass). */
    auto wrapper = std::make_shared<ExtensionFunctionWrapper>();
    wrapper->c_function = fn;

    /* Create C++ function that calls the wrapper */
    BytecodeHostFunction cppFn = [wrapper](const std::vector<Value>& args) {
        return ExtensionFunctionWrapper::callWrapper(args, wrapper.get());
    };

    /* The function name format is "module.function" */
    std::string fullName = std::string(module) + "." + name;

    {
        std::lock_guard<std::mutex> lk(g_extensionFunctionsMutex);
        g_extensionFunctions[fullName] = std::move(cppFn);
    }
    ::havel::debug("[HavelAPI] Registered extension function: {}", fullName);
}

std::unordered_map<std::string, BytecodeHostFunction>
takeRegisteredExtensionFunctions(VM* vm) {
    std::lock_guard<std::mutex> lk(g_extensionFunctionsMutex);
    g_extension_vm = vm;
    auto out = std::move(g_extensionFunctions);
    g_extensionFunctions.clear();
    return out;
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

/* Declared in HavelCAPI.h; toolkit-plugin hosts (UIManager) use it to hand
 * extensions the real table at backend-creation time. */
extern "C" void* havel_get_global_c_api(void) {
    return &g_havelAPI;
}

} /* namespace havel::compiler */
