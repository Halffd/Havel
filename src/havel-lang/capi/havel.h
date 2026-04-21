// havel.h - Public C API for Havel language embedding
// Compatible with C and other languages with C bindings

#ifndef HAVEL_H
#define HAVEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque types
// ============================================================================

typedef struct HavelState HavelState;
typedef struct HavelValue HavelValue;
typedef int (*HavelCFunction)(HavelState*);

// ============================================================================
// Type constants
// ============================================================================

#define HAVEL_TNIL       0
#define HAVEL_TBOOLEAN  1
#define HAVEL_TNUMBER  2
#define HAVEL_TSTRING  3
#define HAVEL_TTABLE  4
#define HAVEL_TFUNCTION 5
#define HAVEL_TTHREAD  6
#define HAVEL_TARRAY  7
#define HAVEL_TOBJECT 8

// ============================================================================
// Status codes
// ============================================================================

#define HAVEL_OK     0
#define HAVEL_YIELD 1
#define HAVEL_ERR   2
#define HAVEL_DEAD  3

// ============================================================================
// GC constants
// ============================================================================

#define HAVEL_GC_COLLECT 0
#define HAVEL_GC_STOP   1
#define HAVEL_GC_RESTART 2

// ============================================================================
// State management
// ============================================================================

HavelState* havel_newstate(void);
void       havel_close(HavelState* H);
HavelState* havel_newthread(HavelState* H);

// ============================================================================
// Stack operations
// ============================================================================

int  havel_gettop(HavelState* H);
void havel_settop(HavelState* H, int idx);
void havel_pushvalue(HavelState* H, int idx);
void havel_pop(HavelState* H, int n);
void havel_remove(HavelState* H, int idx);
void havel_insert(HavelState* H, int idx);

// ============================================================================
// Push functions
// ============================================================================

void havel_pushnil(HavelState* H);
void havel_pushboolean(HavelState* H, int b);
void havel_pushnumber(HavelState* H, double n);
void havel_pushinteger(HavelState* H, int64_t n);
void havel_pushstring(HavelState* H, const char* s);
void havel_pushcfunction(HavelState* H, HavelCFunction fn, const char* name);
void havel_pushobject(HavelState* H);
void havel_pusharray(HavelState* H);

// ============================================================================
// Type checking
// ============================================================================

int havel_type(HavelState* H, int idx);
int havel_isnil(HavelState* H, int idx);
int havel_isboolean(HavelState* H, int idx);
int havel_isnumber(HavelState* H, int idx);
int havel_isstring(HavelState* H, int idx);
int havel_isfunction(HavelState* H, int idx);
int havel_istable(HavelState* H, int idx);
int havel_isarray(HavelState* H, int idx);
int havel_isthread(HavelState* H, int idx);

// ============================================================================
// Get values (with conversions)
// ============================================================================

int         havel_toboolean(HavelState* H, int idx);
double      havel_tonumber(HavelState* H, int idx);
int64_t     havel_tointeger(HavelState* H, int idx);
const char* havel_tostring(HavelState* H, int idx);
size_t      havel_rawlen(HavelState* H, int idx);

// ============================================================================
// Globals
// ============================================================================

void havel_getglobal(HavelState* H, const char* name);
void havel_setglobal(HavelState* H, const char* name);
int  havel_hasglobal(HavelState* H, const char* name);

// ============================================================================
// Table operations
// ============================================================================

void havel_getfield(HavelState* H, int idx, const char* key);
void havel_setfield(HavelState* H, int idx, const char* key);
void havel_rawgeti(HavelState* H, int idx, int i);
void havel_rawseti(HavelState* H, int idx, int i);
int  havel_next(HavelState* H, int idx);

// ============================================================================
// Array operations
// ============================================================================

void havel_arrayappend(HavelState* H, int idx);
int  havel_arraylen(HavelState* H, int idx);

// ============================================================================
// Calling functions
// ============================================================================

void havel_call(HavelState* H, int nargs, int nresults);
int  havel_pcall(HavelState* H, int nargs, int nresults, int msgh);

// ============================================================================
// Coroutines
// ============================================================================

int  havel_resume(HavelState* H, HavelState* thread, int nargs);
int  havel_yield(HavelState* H, int nresults);
int  havel_status(HavelState* H, HavelState* thread);

// ============================================================================
// Error handling
// ============================================================================

void havel_error(HavelState* H, const char* fmt, ...);
const char* havel_errmsg(HavelState* H);

// ============================================================================
// GC
// ============================================================================

void havel_gc(HavelState* H, int what);

// ============================================================================
// Compilation
// ============================================================================

int havel_loadstring(HavelState* H, const char* s, const char* name);
int havel_loadfile(HavelState* H, const char* filename);

// ============================================================================
// Module loading
// ============================================================================

void havel_require(HavelState* H, const char* name);
void havel_register_cmodule(HavelState* H, const char* name,
                          int (*init)(HavelState*));

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

#endif // HAVEL_H