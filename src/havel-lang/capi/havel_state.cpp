// havel_state.cpp - C API state management
#include "havel.h"
#include "../compiler/vm/VM.hpp"
#include "../core/Value.hpp"
#ifdef HAVE_LIBFFI
#include "../ffi/FFITypes.hpp"
#include "../ffi/FFIMemory.hpp"
#include "../ffi/FFIAccessors.hpp"
#include "../ffi/FFICall.hpp"
#endif
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

enum class CoroutineState { DEAD, RUNNING, SUSPENDED, ERROR };

struct HavelState {
    std::unique_ptr<havel::compiler::VM> vm;
    std::vector<havel::core::Value> stack;
    std::unordered_map<std::string, havel::core::Value> globals;
    std::unordered_map<uint32_t, std::unordered_map<std::string, havel::core::Value>> tables;
    std::unordered_map<uint32_t, std::vector<havel::core::Value>> arrays;
    std::unordered_map<uint32_t, std::string> strings;
    std::vector<std::shared_ptr<void>> compiled_chunks;
    std::string last_error;
    HavelState* parent = nullptr;
    bool is_thread = false;
    CoroutineState coroutine_state = CoroutineState::DEAD;
    std::optional<havel::core::Value> yield_value;
    uint32_t next_array_id = 1;
    uint32_t next_table_id = 1;
    uint32_t next_string_id = 1;
};

static const char* HAVEL_VERSION = "0.1.0";

HavelState* havel_newstate() {
    auto* H = new HavelState();
    H->vm = std::make_unique<havel::compiler::VM>();
    return H;
}

void havel_close(HavelState* H) {
    if (H) delete H;
}

HavelState* havel_newthread(HavelState* parent) {
    auto* H = new HavelState();
    H->vm = std::make_unique<havel::compiler::VM>();
    H->parent = parent;
    H->is_thread = true;
    return H;
}

int havel_gettop(HavelState* H) {
    return static_cast<int>(H->stack.size());
}

void havel_settop(HavelState* H, int idx) {
    if (idx < 0) {
        size_t needed = H->stack.size() + static_cast<size_t>(-idx);
        if (needed > H->stack.size()) {
            H->last_error = "stack underflow";
        }
    } else {
        H->stack.resize(static_cast<size_t>(idx));
    }
}

void havel_pushvalue(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i >= 0 && i < t) {
        H->stack.push_back(H->stack[i]);
    } else {
        H->stack.push_back(havel::core::Value::makeNull());
    }
}

void havel_pop(HavelState* H, int n) {
    while (n-- > 0 && !H->stack.empty()) {
        H->stack.pop_back();
    }
}

void havel_remove(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i >= 0 && i < t) {
        H->stack.erase(H->stack.begin() + i);
    }
}

void havel_insert(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i >= 0 && i < t && !H->stack.empty()) {
        auto val = H->stack.back();
        H->stack.pop_back();
        H->stack.insert(H->stack.begin() + i, val);
    }
}

void havel_pushnil(HavelState* H) {
    H->stack.push_back(havel::core::Value::makeNull());
}

void havel_pushboolean(HavelState* H, int b) {
    H->stack.push_back(havel::core::Value::makeBool(b != 0));
}

void havel_pushnumber(HavelState* H, double n) {
    H->stack.push_back(havel::core::Value::makeDouble(n));
}

void havel_pushinteger(HavelState* H, int64_t n) {
    H->stack.push_back(havel::core::Value::makeInt(n));
}

void havel_pushstring(HavelState* H, const char* s) {
    if (s) {
        auto strId = H->next_string_id++;
        H->strings[strId] = s;
        H->stack.push_back(havel::core::Value::makeStringValId(strId));
    } else {
        H->stack.push_back(havel::core::Value::makeNull());
    }
}

void havel_pushcfunction(HavelState* H, HavelCFunction fn, const char* name) {
    (void)H;
    (void)fn;
    (void)name;
}

void havel_pushobject(HavelState* H) {
    uint32_t id = H->next_table_id++;
    H->tables[id] = std::unordered_map<std::string, havel::core::Value>{};
    H->stack.push_back(havel::core::Value::makeObjectId(id));
}

void havel_pusharray(HavelState* H) {
    uint32_t id = H->next_array_id++;
    H->arrays[id] = std::vector<havel::core::Value>{};
    H->stack.push_back(havel::core::Value::makeArrayId(id));
}

int havel_type(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return HAVEL_TNIL;
    
    const auto& v = H->stack[i];
    if (v.isNull()) return HAVEL_TNIL;
    if (v.isBool()) return HAVEL_TBOOLEAN;
    if (v.isInt()) return HAVEL_TINT;
    if (v.isDouble()) return HAVEL_TFLOAT;
    if (v.isNumber()) return HAVEL_TNUMBER;
    if (v.isStringValId() || v.isStringId()) return HAVEL_TSTRING;
    if (v.isPtr()) return HAVEL_TPOINTER;
    if (v.isArrayId()) return HAVEL_TARRAY;
    if (v.isObjectId()) return HAVEL_TOBJECT;
    if (v.isClosureId() || v.isHostFuncId()) return HAVEL_TFUNCTION;
    if (v.isCoroutineId()) return HAVEL_TCOROUTINE;
    if (v.isThreadId()) return HAVEL_TTHREAD;
    if (v.isChannelId()) return HAVEL_TCHANNEL;
    if (v.isRangeId()) return HAVEL_TRANGE;
    if (v.isSetId()) return HAVEL_TSET;
    if (v.isEnumId()) return HAVEL_TENUM;
    if (v.isIteratorId()) return HAVEL_TITERATOR;
    return HAVEL_TOBJECT;
}

int havel_isnil(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TNIL; }
int havel_isboolean(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TBOOLEAN; }
int havel_isnumber(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TNUMBER; }
int havel_isstring(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TSTRING; }
int havel_isfunction(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TFUNCTION; }
int havel_isobject(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TOBJECT; }
int havel_isarray(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TARRAY; }
int havel_iscoroutine(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TCOROUTINE; }
int havel_isthread(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TTHREAD; }

int havel_toboolean(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return 0;
    const auto& v = H->stack[i];
    return v.isBool() ? (v.asBool() ? 1 : 0) : 0;
}

double havel_tonumber(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return 0;
    const auto& v = H->stack[i];
    if (v.isDouble()) return v.asDouble();
    if (v.isInt()) return static_cast<double>(v.asInt());
    return 0.0;
}

int64_t havel_tointeger(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return 0;
    const auto& v = H->stack[i];
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return static_cast<int64_t>(v.asDouble());
    return 0;
}

const char* havel_tostring(HavelState* H, int idx) {
    static std::string buf;
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return "";
    const auto& v = H->stack[i];
    if (v.isStringValId()) {
        auto it = H->strings.find(v.asStringValId());
        if (it != H->strings.end()) return it->second.c_str();
    }
    buf = "<value>";
    return buf.c_str();
}

size_t havel_rawlen(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return 0;
    const auto& v = H->stack[i];
    if (v.isArrayId()) {
        auto it = H->arrays.find(v.asArrayId());
        return it != H->arrays.end() ? static_cast<int>(it->second.size()) : 0;
    }
    if (v.isObjectId()) {
        auto it = H->tables.find(v.asObjectId());
        return it != H->tables.end() ? static_cast<int>(it->second.size()) : 0;
    }
    return 0;
}

void havel_getglobal(HavelState* H, const char* name) {
    auto it = H->globals.find(name);
    if (it != H->globals.end()) {
        H->stack.push_back(it->second);
    } else {
        H->stack.push_back(havel::core::Value::makeNull());
    }
}

void havel_setglobal(HavelState* H, const char* name) {
    if (!H->stack.empty()) {
        H->globals[name] = H->stack.back();
        H->stack.pop_back();
    }
}

int havel_hasglobal(HavelState* H, const char* name) {
    return H->globals.find(name) != H->globals.end() ? 1 : 0;
}

void havel_getfield(HavelState* H, int idx, const char* key) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) {
        H->stack.push_back(havel::core::Value::makeNull());
        return;
    }
    const auto& v = H->stack[i];
    if (v.isObjectId()) {
        auto it = H->tables.find(v.asObjectId());
        if (it != H->tables.end()) {
            auto fieldIt = it->second.find(key);
            if (fieldIt != it->second.end()) {
                H->stack.push_back(fieldIt->second);
                return;
            }
        }
    }
    H->stack.push_back(havel::core::Value::makeNull());
}

void havel_setfield(HavelState* H, int idx, const char* key) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t || H->stack.empty()) return;
    const auto& v = H->stack[i];
    auto val = H->stack.back();
    H->stack.pop_back();
    if (v.isObjectId()) {
        auto it = H->tables.find(v.asObjectId());
        if (it != H->tables.end()) {
            it->second[key] = val;
        }
    }
}

void havel_getindex(HavelState* H, int idx, int i) {
    int t = havel_gettop(H);
    int arr_idx = idx >= 0 ? idx : t + idx;
    if (arr_idx < 0 || arr_idx >= t) {
        H->stack.push_back(havel::core::Value::makeNull());
        return;
    }
    const auto& v = H->stack[arr_idx];
    if (v.isArrayId()) {
        auto arrId = v.asArrayId();
        auto it = H->arrays.find(arrId);
        if (it != H->arrays.end() && i >= 0 && i < static_cast<int>(it->second.size())) {
            H->stack.push_back(it->second[static_cast<size_t>(i)]);
            return;
        }
    }
    H->stack.push_back(havel::core::Value::makeNull());
}

void havel_setindex(HavelState* H, int idx, int i) {
    int t = havel_gettop(H);
    int arr_idx = idx >= 0 ? idx : t + idx;
    if (arr_idx < 0 || arr_idx >= t || H->stack.empty()) return;
    const auto& v = H->stack[arr_idx];
    auto val = H->stack.back();
    H->stack.pop_back();
    if (v.isArrayId()) {
        auto it = H->arrays.find(v.asArrayId());
        if (it != H->arrays.end() && i >= 0) {
            while (it->second.size() < static_cast<size_t>(i)) {
                it->second.push_back(havel::core::Value::makeNull());
            }
            it->second[static_cast<size_t>(i)] = val;
        }
    }
}

int havel_iternext(HavelState* H, int idx) {
    (void)H;
    (void)idx;
    return 0;
}

void havel_arrayappend(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t || H->stack.empty()) return;
    const auto& v = H->stack[i];
    auto val = H->stack.back();
    H->stack.pop_back();
    if (v.isArrayId()) {
        auto arrId = v.asArrayId();
        if (arrId > 0 && arrId < H->arrays.size()) {
            H->arrays[arrId].push_back(val);
        }
    }
}

int havel_arraylen(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return 0;
    const auto& v = H->stack[i];
    if (v.isArrayId()) {
        return static_cast<int>(H->arrays[v.asArrayId()].size());
    }
    return 0;
}

void havel_call(HavelState* H, int nargs, int nresults) {
    (void)H;
    (void)nargs;
    (void)nresults;
}

int havel_pcall(HavelState* H, int nargs, int nresults, int msgh) {
    (void)H;
    (void)nargs;
    (void)nresults;
    (void)msgh;
    return HAVEL_ERR;
}

int havel_resume(HavelState* main, HavelState* thread, int nargs) {
    if (!main || !thread) return HAVEL_ERR;
    if (thread->is_thread && thread->parent != main) return HAVEL_ERR;
    if (nargs < 0 || nargs > havel_gettop(thread)) return HAVEL_ERR;
    thread->coroutine_state = CoroutineState::RUNNING;
    thread->parent = main;
    if (thread->yield_value) {
        thread->stack.push_back(*thread->yield_value);
        thread->yield_value.reset();
    }
    thread->coroutine_state = CoroutineState::SUSPENDED;
    return HAVEL_YIELD;
}

int havel_yield(HavelState* H, int nresults) {
    if (!H) return HAVEL_ERR;
    if (nresults < 0 || nresults > havel_gettop(H)) return HAVEL_ERR;
    H->coroutine_state = CoroutineState::SUSPENDED;
    while (nresults-- > 0 && !H->stack.empty()) {
        if (H->parent) H->parent->stack.push_back(H->stack.back());
        H->stack.pop_back();
    }
    return HAVEL_YIELD;
}

int havel_status(HavelState* H, HavelState* thread) {
    if (!H || !thread) return HAVEL_ERR;
    switch (thread->coroutine_state) {
        case CoroutineState::DEAD: return HAVEL_DEAD;
        case CoroutineState::ERROR: return HAVEL_ERR;
        case CoroutineState::SUSPENDED: return HAVEL_YIELD;
        default: return HAVEL_OK;
    }
}

void havel_error(HavelState* H, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    H->last_error = buf;
}

const char* havel_errmsg(HavelState* H) {
    return H->last_error.c_str();
}

void havel_gc(HavelState* H, int what) {
    (void)H;
    (void)what;
}

int havel_loadstring(HavelState* H, const char* s, const char* name) {
    if (!H || !s) return HAVEL_ERR;
    H->last_error = "loadstring not fully implemented - needs full parser integration";
    return HAVEL_ERR;
}

int havel_loadfile(HavelState* H, const char* filename) {
    (void)H;
    (void)filename;
    return HAVEL_ERR;
}

void havel_require(HavelState* H, const char* name) {
    if (!H || !name || !H->vm) return;
    try {
        auto exports = H->vm->loadModule(name);
        H->stack.push_back(exports);
    } catch (const std::exception& e) {
        H->last_error = e.what();
        H->stack.push_back(havel::core::Value::makeNull());
    }
}

void havel_register_cmodule(HavelState* H, const char* name, int (*init)(HavelState*)) {
    (void)H;
    (void)name;
    (void)init;
}

void havel_add_search_path(HavelState* H, const char* path) {
    if (H && H->vm && path) {
        H->vm->addModuleSearchPath(path);
    }
}

void havel_clear_module_cache(HavelState* H) {
    if (H && H->vm) {
        H->vm->moduleLoader().clearCache();
    }
}

const char* havel_version(void) { return HAVEL_VERSION; }
int havel_version_major(void) { return 0; }
int havel_version_minor(void) { return 1; }
int havel_version_patch(void) { return 0; }

// ========================================================================
// FFI Implementations (stub when HAVE_LIBFFI not available)
// ========================================================================

#ifdef HAVE_LIBFFI

int havel_ffi_typeid(HavelState* H, const char* ctype) {
    (void)H;
    auto type = havel::ffi::FFITypeRegistry::from_name(ctype);
    if (!type) return 0;
    return havel::ffi::FFITypeRegistry::type_id(type);
}

size_t havel_ffi_sizeof(HavelState* H, const char* ctype) {
    (void)H;
    auto type = havel::ffi::FFITypeRegistry::from_name(ctype);
    if (!type) return 0;
    return havel::ffi::FFITypeRegistry::size_of(type);
}

size_t havel_ffi_alignof(HavelState* H, const char* ctype) {
    (void)H;
    auto type = havel::ffi::FFITypeRegistry::from_name(ctype);
    if (!type) return 1;
    return havel::ffi::FFITypeRegistry::align_of(type);
}

void* havel_ffi_alloc(HavelState* H, const char* ctype, size_t count) {
    (void)H;
    auto type = havel::ffi::FFITypeRegistry::from_name(ctype);
    if (!type) return nullptr;
    if (count > 1 && type->kind == havel::ffi::FFITypeKind::ARRAY) {
        type = havel::ffi::FFITypeRegistry::array_type(type->element_type, count);
    }
    return havel::ffi::FFIMemory::alloc(type);
}

void havel_ffi_free(HavelState* H, void* ptr) {
    (void)H;
    havel::ffi::FFIMemory::free(ptr);
}

void* havel_ffi_cast(HavelState* H, const char* ctype, void* ptr) {
    (void)H;
    (void)ctype;
    return ptr;
}

int8_t havel_ffi_get_int8(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_int8(ptr);
}

int16_t havel_ffi_get_int16(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_int16(ptr);
}

int32_t havel_ffi_get_int32(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_int32(ptr);
}

int64_t havel_ffi_get_int64(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_int64(ptr);
}

uint8_t havel_ffi_get_uint8(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_uint8(ptr);
}

uint16_t havel_ffi_get_uint16(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_uint16(ptr);
}

uint32_t havel_ffi_get_uint32(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_uint32(ptr);
}

uint64_t havel_ffi_get_uint64(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_uint64(ptr);
}

float havel_ffi_get_float(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_float32(ptr);
}

double havel_ffi_get_double(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_float64(ptr);
}

void* havel_ffi_get_pointer(HavelState* H, void* ptr) {
    (void)H;
    return havel::ffi::get_pointer(ptr);
}

void havel_ffi_set_int8(HavelState* H, void* ptr, int8_t val) {
    (void)H;
    havel::ffi::set_int8(ptr, val);
}

void havel_ffi_set_int16(HavelState* H, void* ptr, int16_t val) {
    (void)H;
    havel::ffi::set_int16(ptr, val);
}

void havel_ffi_set_int32(HavelState* H, void* ptr, int32_t val) {
    (void)H;
    havel::ffi::set_int32(ptr, val);
}

void havel_ffi_set_int64(HavelState* H, void* ptr, int64_t val) {
    (void)H;
    havel::ffi::set_int64(ptr, val);
}

void havel_ffi_set_uint8(HavelState* H, void* ptr, uint8_t val) {
    (void)H;
    havel::ffi::set_uint8(ptr, val);
}

void havel_ffi_set_uint16(HavelState* H, void* ptr, uint16_t val) {
    (void)H;
    havel::ffi::set_uint16(ptr, val);
}

void havel_ffi_set_uint32(HavelState* H, void* ptr, uint32_t val) {
    (void)H;
    havel::ffi::set_uint32(ptr, val);
}

void havel_ffi_set_uint64(HavelState* H, void* ptr, uint64_t val) {
    (void)H;
    havel::ffi::set_uint64(ptr, val);
}

void havel_ffi_set_float(HavelState* H, void* ptr, float val) {
    (void)H;
    havel::ffi::set_float32(ptr, val);
}

void havel_ffi_set_double(HavelState* H, void* ptr, double val) {
    (void)H;
    havel::ffi::set_float64(ptr, val);
}

void havel_ffi_set_pointer(HavelState* H, void* ptr, void* val) {
    (void)H;
    havel::ffi::set_pointer(ptr, val);
}

void havel_ffi_cdef(HavelState* H, const char* cdecl) {
    (void)H;
    (void)cdecl;
}

void* havel_ffi_load(HavelState* H, const char* path) {
    (void)H;
    return havel::ffi::FFICall::load_library(path);
}

void havel_ffi_unload(HavelState* H, void* handle) {
    (void)H;
    havel::ffi::FFICall::unload_library(handle);
}

void* havel_ffi_sym(HavelState* H, void* handle, const char* name) {
    (void)H;
    return havel::ffi::FFICall::get_symbol(handle, name);
}

#else // HAVE_LIBFFI

int havel_ffi_typeid(HavelState* H, const char* ctype) {
    (void)H; (void)ctype;
    return 0;
}

size_t havel_ffi_sizeof(HavelState* H, const char* ctype) {
    (void)H; (void)ctype;
    return 0;
}

size_t havel_ffi_alignof(HavelState* H, const char* ctype) {
    (void)H; (void)ctype;
    return 1;
}

void* havel_ffi_alloc(HavelState* H, const char* ctype, size_t count) {
    (void)H; (void)ctype; (void)count;
    return nullptr;
}

void havel_ffi_free(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
}

void* havel_ffi_cast(HavelState* H, const char* ctype, void* ptr) {
    (void)H; (void)ctype;
    return ptr;
}

int8_t havel_ffi_get_int8(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

int16_t havel_ffi_get_int16(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

int32_t havel_ffi_get_int32(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

int64_t havel_ffi_get_int64(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

uint8_t havel_ffi_get_uint8(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

uint16_t havel_ffi_get_uint16(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

uint32_t havel_ffi_get_uint32(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

uint64_t havel_ffi_get_uint64(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0;
}

float havel_ffi_get_float(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0.0f;
}

double havel_ffi_get_double(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return 0.0;
}

void* havel_ffi_get_pointer(HavelState* H, void* ptr) {
    (void)H; (void)ptr;
    return nullptr;
}

void havel_ffi_set_int8(HavelState* H, void* ptr, int8_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_int16(HavelState* H, void* ptr, int16_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_int32(HavelState* H, void* ptr, int32_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_int64(HavelState* H, void* ptr, int64_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_uint8(HavelState* H, void* ptr, uint8_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_uint16(HavelState* H, void* ptr, uint16_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_uint32(HavelState* H, void* ptr, uint32_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_uint64(HavelState* H, void* ptr, uint64_t val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_float(HavelState* H, void* ptr, float val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_double(HavelState* H, void* ptr, double val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_set_pointer(HavelState* H, void* ptr, void* val) {
    (void)H; (void)ptr; (void)val;
}

void havel_ffi_cdef(HavelState* H, const char* cdecl) {
    (void)H; (void)cdecl;
}

void* havel_ffi_load(HavelState* H, const char* path) {
    (void)H; (void)path;
    return nullptr;
}

void havel_ffi_unload(HavelState* H, void* handle) {
    (void)H; (void)handle;
}

void* havel_ffi_sym(HavelState* H, void* handle, const char* name) {
    (void)H; (void)handle; (void)name;
    return nullptr;
}

#endif // HAVE_LIBFFI