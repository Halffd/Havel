// test_capi_simple.c - Minimal test of the Havel C API header
#include <stdio.h>
#include <string.h>
#include "havel.h"

int main() {
    printf("=== Havel C API Header Test ===\n\n");
    
    printf("Type constants:\n");
    printf("  HAVEL_TNIL=%d\n", HAVEL_TNIL);
    printf("  HAVEL_TBOOLEAN=%d\n", HAVEL_TBOOLEAN);
    printf("  HAVEL_TNUMBER=%d\n", HAVEL_TNUMBER);
    printf("  HAVEL_TSTRING=%d\n", HAVEL_TSTRING);
    printf("  HAVEL_TTABLE=%d\n", HAVEL_TTABLE);
    printf("  HAVEL_TFUNCTION=%d\n", HAVEL_TFUNCTION);
    printf("  HAVEL_TTHREAD=%d\n", HAVEL_TTHREAD);
    printf("  HAVEL_TARRAY=%d\n", HAVEL_TARRAY);
    printf("  HAVEL_TOBJECT=%d\n", HAVEL_TOBJECT);
    printf("  HAVEL_TSTRUCT=%d\n", HAVEL_TSTRUCT);
    printf("  HAVEL_TUNION=%d\n", HAVEL_TUNION);
    printf("  HAVEL_TPOINTER=%d\n", HAVEL_TPOINTER);
    printf("  HAVEL_TRANGE=%d\n", HAVEL_TRANGE);
    printf("  HAVEL_TSET=%d\n", HAVEL_TSET);
    printf("  HAVEL_TCLOSURE=%d\n", HAVEL_TCLOSURE);
    printf("  HAVEL_TFOREIGN=%d\n", HAVEL_TFOREIGN);
    printf("  HAVEL_THANDLE=%d\n", HAVEL_THANDLE);
    
    printf("\nFFI types:\n");
    printf("  HAVEL_FFI_VOID=%d\n", HAVEL_FFI_VOID);
    printf("  HAVEL_FFI_INT=%d\n", HAVEL_FFI_INT);
    printf("  HAVEL_FFI_LONG=%d\n", HAVEL_FFI_LONG);
    printf("  HAVEL_FFI_FLOAT=%d\n", HAVEL_FFI_FLOAT);
    printf("  HAVEL_FFI_DOUBLE=%d\n", HAVEL_FFI_DOUBLE);
    printf("  HAVEL_FFI_POINTER=%d\n", HAVEL_FFI_POINTER);
    printf("  HAVEL_FFI_STRING=%d\n", HAVEL_FFI_STRING);
    
    printf("\nStatus codes:\n");
    printf("  HAVEL_OK=%d\n", HAVEL_OK);
    printf("  HAVEL_YIELD=%d\n", HAVEL_YIELD);
    printf("  HAVEL_ERR=%d\n", HAVEL_ERR);
    printf("  HAVEL_DEAD=%d\n", HAVEL_DEAD);
    
    printf("\nGC codes:\n");
    printf("  HAVEL_GC_COLLECT=%d\n", HAVEL_GC_COLLECT);
    printf("  HAVEL_GC_STOP=%d\n", HAVEL_GC_STOP);
    printf("  HAVEL_GC_RESTART=%d\n", HAVEL_GC_RESTART);
    
    printf("\nAPI functions present:\n");
    
    // Verify all functions are declared
    void* funcs[] = {
        (void*)havel_newstate,
        (void*)havel_close,
        (void*)havel_newthread,
        (void*)havel_gettop,
        (void*)havel_settop,
        (void*)havel_pop,
        (void*)havel_remove,
        (void*)havel_insert,
        (void*)havel_pushnil,
        (void*)havel_pushboolean,
        (void*)havel_pushnumber,
        (void*)havel_pushinteger,
        (void*)havel_pushstring,
        (void*)havel_pushcfunction,
        (void*)havel_pushobject,
        (void*)havel_pusharray,
        (void*)havel_type,
        (void*)havel_isnil,
        (void*)havel_isboolean,
        (void*)havel_isnumber,
        (void*)havel_isstring,
        (void*)havel_isfunction,
        (void*)havel_istable,
        (void*)havel_isarray,
        (void*)havel_isthread,
        (void*)havel_toboolean,
        (void*)havel_tonumber,
        (void*)havel_tointeger,
        (void*)havel_tostring,
        (void*)havel_rawlen,
        (void*)havel_getglobal,
        (void*)havel_setglobal,
        (void*)havel_hasglobal,
        (void*)havel_getfield,
        (void*)havel_setfield,
        (void*)havel_rawgeti,
        (void*)havel_rawseti,
        (void*)havel_next,
        (void*)havel_arrayappend,
        (void*)havel_arraylen,
        (void*)havel_call,
        (void*)havel_pcall,
        (void*)havel_resume,
        (void*)havel_yield,
        (void*)havel_status,
        (void*)havel_error,
        (void*)havel_errmsg,
        (void*)havel_gc,
        (void*)havel_loadstring,
        (void*)havel_loadfile,
        (void*)havel_require,
        (void*)havel_register_cmodule,
        (void*)havel_version,
        (void*)havel_version_major,
        (void*)havel_version_minor,
        (void*)havel_version_patch,
    };
    
    int count = sizeof(funcs) / sizeof(funcs[0]);
    int passed = 0;
    for (int i = 0; i < count; i++) {
        if (funcs[i]) passed++;
    }
    printf("\n  %d/%d functions declared\n", passed, count);
    
    printf("\nVersion: %s\n", havel_version());
    printf("Version: %d.%d.%d\n", 
           havel_version_major(), 
           havel_version_minor(), 
           havel_version_patch());
    
    printf("\n=== Header Test Complete ===\n");
    return 0;
}