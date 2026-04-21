// test_capi.cpp - Test the Havel C API
#include "havel.h"
#include <cstdio>
#include <cstring>

int main() {
    printf("=== Havel C API Test ===\n\n");

    // Create VM state
    printf("Test 1: havel_newstate()\n");
    HavelState* H = havel_newstate();
    if (!H) {
        printf("  FAILED: could not create state\n");
        return 1;
    }
    printf("  OK: Created state\n");

    // Test stack operations
    printf("\nTest 2: Stack operations\n");
    havel_pushnil(H);
    printf("  pushnil: top=%d (expected 1)\n", havel_gettop(H));
    havel_pushboolean(H, 1);
    printf("  pushboolean: top=%d (expected 2)\n", havel_gettop(H));
    havel_pushnumber(H, 3.14);
    printf("  pushnumber: top=%d (expected 3)\n", havel_gettop(H));
    havel_pushinteger(H, 42);
    printf("  pushinteger: top=%d (expected 4)\n", havel_gettop(H));
    havel_pushstring(H, "hello");
    printf("  pushstring: top=%d (expected 5)\n", havel_gettop(H));

    // Test type checking
    printf("\nTest 3: Type checking\n");
    printf("  type(1) = %d (NIL=0, BOOL=1, NUM=2, STR=3)\n", havel_type(H, 1));
    printf("  type(2) = %d (expected 1 BOOL)\n", havel_type(H, 2));
    printf("  type(3) = %d (expected 2 NUMBER)\n", havel_type(H, 3));
    printf("  type(4) = %d (expected 2 NUMBER)\n", havel_type(H, 4));
    printf("  type(5) = %d (expected 3 STRING)\n", havel_type(H, 5));

    // Test conversions
    printf("\nTest 4: Conversions\n");
    havel_pop(H, 3);  // Pop NIL, BOOL, NUMBER
    bool b = havel_toboolean(H, 1);
    printf("  toboolean: %s\n", b ? "true" : "false");
    double d = havel_tonumber(H, 1);
    printf("  tonumber: %g\n", d);
    int64_t i = havel_tointeger(H, 1);
    printf("  tointeger: %ld\n", (long)i);
    const char* s = havel_tostring(H, 1);
    printf("  tostring: %s\n", s);

    // Test arrays
    printf("\nTest 5: Arrays\n");
    havel_pop(H, havel_gettop(H));  // Clear stack
    havel_pusharray(H);
    printf("  pusharray: top=%d, type=%d (expected 7 ARRAY)\n", 
           havel_gettop(H), havel_type(H, -1));
    havel_pushnil(H);
    havel_pushnumber(H, 100);
    havel_arrayappend(H, -2);  // Append to array at -2
    int len = havel_arraylen(H, -1);
    printf("  array append: len=%d\n", len);

    // Test objects/tables
    printf("\nTest 6: Objects/Tables\n");
    havel_pop(H, havel_gettop(H));  // Clear
    havel_pushobject(H);
    printf("  pushobject: type=%d (expected 8 OBJECT)\n", havel_type(H, -1));
    havel_pushnil(H);
    havel_pushstring(H, "value");
    havel_setfield(H, -3, "key");
    havel_getfield(H, -1, "key");
    printf("  setfield/getfield: value='%s'\n", havel_tostring(H, -1));

    // Test loadstring (if implemented)
    printf("\nTest 7: Compilation\n");
    int result = havel_loadstring(H, "let x = 10", "test");
    printf("  loadstring: result=%d (0=OK, 2=ERR)\n", result);

    // Test coroutines
    printf("\nTest 8: Coroutines\n");
    HavelState* main = havel_newstate();
    HavelState* coro = havel_newthread(main);
    printf("  newthread: created\n");
    int status = havel_status(main, coro);
    printf("  status (new): %d (3=DEAD)\n", status);
    (void)coro;  // Silence warning

    // Test loadstring on thread
    result = havel_loadstring(coro, "yield 1; yield 2; return 3", "coro");
    printf("  loadstring on thread: %d\n", result);

    // Resume the thread
    status = havel_resume(main, coro, 0);
    printf("  resume: status=%d (1=YIELD expected)\n", status);
    printf("  stack top: %d\n", havel_gettop(coro));
    if (havel_gettop(coro) > 0) {
        printf("  yielded value: %s\n", havel_tostring(coro, -1));
    }

    // Second resume
    status = havel_resume(main, coro, 0);
    printf("  resume 2: status=%d\n", status);
    if (havel_gettop(coro) > 0) {
        printf("  yielded value: %s\n", havel_tostring(coro, -1));
    }

    status = havel_status(main, coro);
    printf("  final status: %d\n", status);

    havel_close(coro);
    havel_close(main);
    havel_close(H);

    printf("\n=== Test Complete ===\n");
    return 0;
}