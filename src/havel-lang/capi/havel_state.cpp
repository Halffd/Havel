// havel_state.cpp - C API state management
#include "havel.h"
#include "../compiler/vm/VM.hpp"
#include "../core/Value.hpp"
#include <cstring>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

struct HavelState {
    std::unique_ptr<havel::compiler::VM> vm;
    std::vector<havel::core::Value> stack;
    std::unordered_map<std::string, havel::core::Value> globals;
    std::string last_error;
    HavelState* parent = nullptr;
    bool is_thread = false;
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
        uint32_t id = H->vm->heap().allocateString(s).id;
        H->stack.push_back(havel::core::Value::makeStringValId(id));
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
    uint32_t id = H->vm->heap().allocateObject().id;
    H->stack.push_back(havel::core::Value::makeObjectId(id));
}

void havel_pusharray(HavelState* H) {
    uint32_t id = H->vm->heap().allocateArray().id;
    H->stack.push_back(havel::core::Value::makeArrayId(id));
}

int havel_type(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return HAVEL_TNIL;
    
    const auto& v = H->stack[i];
    if (v.isNull()) return HAVEL_TNIL;
    if (v.isBool()) return HAVEL_TBOOLEAN;
    if (v.isInt() || v.isDouble()) return HAVEL_TNUMBER;
    if (v.isStringValId() || v.isStringId()) return HAVEL_TSTRING;
    if (v.isArrayId()) return HAVEL_TARRAY;
    if (v.isObjectId()) return HAVEL_TTABLE;
    if (v.isClosureId() || v.isHostFuncId()) return HAVEL_TFUNCTION;
    if (v.isCoroutineId()) return HAVEL_TTHREAD;
    return HAVEL_TOBJECT;
}

int havel_isnil(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TNIL; }
int havel_isboolean(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TBOOLEAN; }
int havel_isnumber(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TNUMBER; }
int havel_isstring(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TSTRING; }
int havel_isfunction(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TFUNCTION; }
int havel_istable(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TTABLE; }
int havel_isarray(HavelState* H, int idx) { return havel_type(H, idx) == HAVEL_TARRAY; }
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
        auto* str = H->vm->heap().string(v.asStringValId());
        if (str) return str->c_str();
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
        auto* arr = H->vm->heap().array(v.asArrayId());
        return arr ? arr->size() : 0;
    }
    if (v.isObjectId()) {
        auto* obj = H->vm->heap().object(v.asObjectId());
        return obj ? obj->size() : 0;
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
        auto* obj = H->vm->heap().object(v.asObjectId());
        if (obj) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                H->stack.push_back(it->second);
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
        auto* obj = H->vm->heap().object(v.asObjectId());
        if (obj) {
            (*obj)[key] = val;
        }
    }
}

void havel_rawgeti(HavelState* H, int idx, int i) {
    int t = havel_gettop(H);
    int arr_idx = idx >= 0 ? idx : t + idx;
    if (arr_idx < 0 || arr_idx >= t) {
        H->stack.push_back(havel::core::Value::makeNull());
        return;
    }
    const auto& v = H->stack[arr_idx];
    if (v.isArrayId()) {
        auto* arr = H->vm->heap().array(v.asArrayId());
        if (arr && i >= 0 && i < static_cast<int>(arr->size())) {
            H->stack.push_back((*arr)[static_cast<size_t>(i)]);
            return;
        }
    }
    H->stack.push_back(havel::core::Value::makeNull());
}

void havel_rawseti(HavelState* H, int idx, int i) {
    int t = havel_gettop(H);
    int arr_idx = idx >= 0 ? idx : t + idx;
    if (arr_idx < 0 || arr_idx >= t || H->stack.empty()) return;
    const auto& v = H->stack[arr_idx];
    auto val = H->stack.back();
    H->stack.pop_back();
    if (v.isArrayId()) {
        auto* arr = H->vm->heap().array(v.asArrayId());
        if (arr && i >= 0) {
            while (arr->size() < static_cast<size_t>(i)) {
                arr->push_back(havel::core::Value::makeNull());
            }
            (*arr)[static_cast<size_t>(i)] = val;
        }
    }
}

int havel_next(HavelState* H, int idx) {
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
        auto* arr = H->vm->heap().array(v.asArrayId());
        if (arr) {
            arr->push_back(val);
        }
    }
}

int havel_arraylen(HavelState* H, int idx) {
    int t = havel_gettop(H);
    int i = idx >= 0 ? idx : t + idx;
    if (i < 0 || i >= t) return 0;
    const auto& v = H->stack[i];
    if (v.isArrayId()) {
        auto* arr = H->vm->heap().array(v.asArrayId());
        return arr ? static_cast<int>(arr->size()) : 0;
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

int havel_resume(HavelState* H, HavelState* thread, int nargs) {
    (void)H;
    (void)thread;
    (void)nargs;
    return HAVEL_DEAD;
}

int havel_yield(HavelState* H, int nresults) {
    (void)H;
    (void)nresults;
    return HAVEL_YIELD;
}

int havel_status(HavelState* H, HavelState* thread) {
    (void)H;
    (void)thread;
    return HAVEL_DEAD;
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
    (void)H;
    (void)s;
    (void)name;
    return HAVEL_ERR;
}

int havel_loadfile(HavelState* H, const char* filename) {
    (void)H;
    (void)filename;
    return HAVEL_ERR;
}

void havel_require(HavelState* H, const char* name) {
    (void)H;
    (void)name;
}

void havel_register_cmodule(HavelState* H, const char* name, int (*init)(HavelState*)) {
    (void)H;
    (void)name;
    (void)init;
}

const char* havel_version(void) { return HAVEL_VERSION; }
int havel_version_major(void) { return 0; }
int havel_version_minor(void) { return 1; }
int havel_version_patch(void) { return 0; }