// havel.h - Public C API for Havel language embedding
#ifndef HAVEL_H
#define HAVEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HavelState HavelState;
typedef struct HavelValue HavelValue;
typedef int (*HavelCFunction)(HavelState*);

// VM Runtime types (matches Value.hpp)
#define HAVEL_TNIL       0   // isNull()
#define HAVEL_TBOOLEAN    1   // isBool()
#define HAVEL_TINT        2   // isInt() - 48-bit signed
#define HAVEL_TNUMBER     3   // isInt() || isDouble()
#define HAVEL_TFLOAT     4   // isDouble()
#define HAVEL_TSTRING     5   // isStringId() || isStringValId()
#define HAVEL_TPOINTER   6   // isPtr()
#define HAVEL_TARRAY     7   // isArrayId()
#define HAVEL_TOBJECT    8   // isObjectId() - keyed object/map
#define HAVEL_TFUNCTION  9   // isClosureId() || isHostFuncId()
#define HAVEL_TCOROUTINE 10   // isCoroutineId()
#define HAVEL_TTHREAD    11   // isThreadId()
#define HAVEL_TCHANNEL  12   // isChannelId()
#define HAVEL_TRANGE    13   // isRangeId()
#define HAVEL_TSET      14   // isSetId()
#define HAVEL_TENUM     15   // isEnumId()
#define HAVEL_TITERATOR  16   // isIteratorId()

// Type predicates for numbers
#define HAVEL_TISINT     20  // isInt() - exact match
#define HAVEL_TISDOUBLE  21  // isDouble() - exact match
#define HAVEL_TISNUMBER  22  // isNumber() - int or double

#define HAVEL_OK     0
#define HAVEL_YIELD 1
#define HAVEL_ERR   2
#define HAVEL_DEAD  3

#define HAVEL_GC_COLLECT  0
#define HAVEL_GC_STOP    1
#define HAVEL_GC_RESTART 2

HavelState* havel_newstate(void);
void       havel_close(HavelState* H);
HavelState* havel_newthread(HavelState* H);

int  havel_gettop(HavelState* H);
void havel_settop(HavelState* H, int idx);
void havel_pushvalue(HavelState* H, int idx);
void havel_pop(HavelState* H, int n);
void havel_remove(HavelState* H, int idx);
void havel_insert(HavelState* H, int idx);

void havel_pushnil(HavelState* H);
void havel_pushboolean(HavelState* H, int b);
void havel_pushnumber(HavelState* H, double n);
void havel_pushinteger(HavelState* H, int64_t n);
void havel_pushstring(HavelState* H, const char* s);
void havel_pushcfunction(HavelState* H, HavelCFunction fn, const char* name);
void havel_pushobject(HavelState* H);
void havel_pusharray(HavelState* H);

int havel_type(HavelState* H, int idx);
int havel_isnil(HavelState* H, int idx);
int havel_isboolean(HavelState* H, int idx);
int havel_isnumber(HavelState* H, int idx);
int havel_isstring(HavelState* H, int idx);
int havel_isfunction(HavelState* H, int idx);
int havel_isobject(HavelState* H, int idx);
int havel_isarray(HavelState* H, int idx);
int havel_iscoroutine(HavelState* H, int idx);
int havel_isthread(HavelState* H, int idx);

int         havel_toboolean(HavelState* H, int idx);
double      havel_tonumber(HavelState* H, int idx);
int64_t     havel_tointeger(HavelState* H, int idx);
const char* havel_tostring(HavelState* H, int idx);
size_t      havel_rawlen(HavelState* H, int idx);

void havel_getglobal(HavelState* H, const char* name);
void havel_setglobal(HavelState* H, const char* name);
int  havel_hasglobal(HavelState* H, const char* name);

void havel_getfield(HavelState* H, int idx, const char* key);
void havel_setfield(HavelState* H, int idx, const char* key);
void havel_getindex(HavelState* H, int idx, int i);
void havel_setindex(HavelState* H, int idx, int i);
int  havel_iternext(HavelState* H, int idx);

void havel_arrayappend(HavelState* H, int idx);
int  havel_arraylen(HavelState* H, int idx);

void havel_call(HavelState* H, int nargs, int nresults);
int  havel_pcall(HavelState* H, int nargs, int nresults, int msgh);

int  havel_resume(HavelState* H, HavelState* thread, int nargs);
int  havel_yield(HavelState* H, int nresults);
int  havel_status(HavelState* H, HavelState* thread);

void havel_error(HavelState* H, const char* fmt, ...);
const char* havel_errmsg(HavelState* H);

void havel_gc(HavelState* H, int what);

int havel_loadstring(HavelState* H, const char* s, const char* name);
int havel_loadfile(HavelState* H, const char* filename);

void havel_require(HavelState* H, const char* name);
void havel_register_cmodule(HavelState* H, const char* name,
                          int (*init)(HavelState*));
void havel_add_search_path(HavelState* H, const char* path);
void havel_clear_module_cache(HavelState* H);

const char* havel_version(void);
int havel_version_major(void);
int havel_version_minor(void);
int havel_version_patch(void);

#ifdef __cplusplus
}
#endif
#endif