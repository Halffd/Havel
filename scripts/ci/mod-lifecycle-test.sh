#!/usr/bin/env bash
# mod-lifecycle-test.sh - build the demo mod and run the modding lifecycle test.
#
# Hot reload is the part that cannot be eyeballed: state must survive a
# generation switch that genuinely unmaps the old code, hooks a mod installed
# into its own text must be gone before dlclose, and dlopen must never hand
# back a cached instance of stale bytes. Each of those is asserted here with
# real dlopen/dlclose/mmap calls in a fresh process.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d /tmp/opencode/mod-lifecycle-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

CC="${CC:-clang}"
CXX="${CXX:-clang++}"
SOCFLAGS="${SOCFLAGS:--shared -fPIC -O2 -g}"
CFLAGS="-std=c++23 -Wall -Wextra -O0 -g -I$ROOT/include -I$ROOT/src"

fail() {
    echo "FAIL: $1" >&2
    [ -n "${2:-}" ] && [ -f "$2" ] && tail -80 "$2" >&2
    exit 1
}

echo "== demo mod =="
if ! $CC $SOCFLAGS -I"$ROOT/include" "$ROOT/examples/mods/demo_mod.c" \
       -o "$WORK/libdemo_mod.so" >"$WORK/build_mod.log" 2>&1; then
    fail "demo mod failed to compile" "$WORK/build_mod.log"
fi

echo "== lifecycle harness =="
# A hook target that lives in the demo .so needs the .so within +-2GB of its
# own trampoline, which the engine's alloc_near() arranges at runtime. The
# harness links the tree's sources directly; standards are per-language
# because a single -std flag cannot serve C and C++ translation units.
INCS="-I$ROOT/include -I$ROOT/src -I$ROOT/src/include"
if ! $CC -std=c++23 -Wall -Wextra -O0 -g $INCS -x c++ \
       "$ROOT/scripts/tests/mods/test_mod_lifecycle.c" -c -o "$WORK/main.o" >"$WORK/build_test.log" 2>&1; then
    fail "lifecycle harness (C++) failed to compile" "$WORK/build_test.log"
fi
if ! $CC -std=c++23 -Wall -Wextra -O0 -g $INCS -x c++ \
       "$ROOT/src/modding/ModManager.cpp" -c -o "$WORK/ModManager.o" >>"$WORK/build_test.log" 2>&1; then
    fail "ModManager.cpp failed to compile" "$WORK/build_test.log"
fi
if ! $CC -std=c17 -Wall -Wextra -O2 -g $INCS -x c \
       "$ROOT/src/modding/hook.c" -c -o "$WORK/hook.o" >>"$WORK/build_test.log" 2>&1; then
    fail "hook.c failed to compile" "$WORK/build_test.log"
fi
# clock_gettime/strdup/localtime_r are POSIX.1-2008, and a strict -std=c17
# compilation hides them, so the feature macro is stated explicitly.
if ! $CC -std=c17 -Wall -Wextra -O0 -g -D_POSIX_C_SOURCE=200809L $INCS -x c \
       "$ROOT/src/c/LoggerC.c" -c -o "$WORK/LoggerC.o" >>"$WORK/build_test.log" 2>&1; then
    fail "LoggerC.c failed to compile" "$WORK/build_test.log"
fi
if ! $CXX "$WORK/main.o" "$WORK/ModManager.o" "$WORK/hook.o" "$WORK/LoggerC.o" \
       -ldl -lpthread -o "$WORK/test_mod_lifecycle" >>"$WORK/build_test.log" 2>&1; then
    fail "lifecycle harness failed to link" "$WORK/build_test.log"
fi
if ! "$WORK/test_mod_lifecycle" "$WORK/libdemo_mod.so" >"$WORK/run.out" 2>&1; then
    fail "lifecycle test failed" "$WORK/run.out"
fi
grep -E '^(PASS|FAIL|==)' "$WORK/run.out" | sed 's/^/  /'
grep -q '^PASS: mod lifecycle' "$WORK/run.out" || fail "no final PASS line" "$WORK/run.out"

echo "mod-lifecycle-test: PASS"