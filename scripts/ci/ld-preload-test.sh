#!/usr/bin/env bash
# ld-preload-test.sh - prove LD_PRELOAD exports reach Havel through mod.sym("").
#
# The mod manager resolves module symbols through each generation's dlopen
# handle first and RTLD_DEFAULT last. RTLD_DEFAULT is where a library injected
# with LD_PRELOAD lives, so a preloaded export must be resolvable and callable
# from a script with no mod.load() and no ffi.open() of that library.
#
# Runs the same script twice: with the probe preloaded (must PASS) and with
# LD_PRELOAD removed (must FAIL at the symbol lookup). Both runs are bounded.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMPBASE="${TMPDIR:-/tmp/opencode}"
mkdir -p "$TMPBASE"
WORK="$(mktemp -d "$TMPBASE/ld-preload-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

CC="${CC:-clang}"
HAVEL_BIN="${HAVEL_BIN:-$ROOT/build-debug/havel}"
HAVEL_STDLIB="${HAVEL_STDLIB:-$ROOT/modules/std}"
TMO="${TMO:-90}"
SCRIPT="$ROOT/scripts/tests/ld-preload/test_ld_preload.hv"

fail() {
    echo "FAIL: $1" >&2
    [ -n "${2:-}" ] && [ -f "$2" ] && tail -60 "$2" >&2
    exit 1
}

[ -x "$HAVEL_BIN" ] || fail "havel binary is not executable: $HAVEL_BIN"
[ -f "$SCRIPT" ] || fail "missing test script: $SCRIPT"

echo "== preload probe =="
if ! $CC -shared -fPIC -O2 -g "$ROOT/scripts/tests/ld-preload/preload_probe.c" \
       -o "$WORK/libhavel_preload_probe.so" >"$WORK/build.log" 2>&1; then
    fail "preload probe failed to compile" "$WORK/build.log"
fi

echo "== with LD_PRELOAD =="
if ! LD_PRELOAD="$WORK/libhavel_preload_probe.so" \
     HAVEL_STDLIB="$HAVEL_STDLIB" HAVEL_BINARY="$HAVEL_BIN" \
     timeout "$TMO" "$HAVEL_BIN" run "$SCRIPT" >"$WORK/with.out" 2>&1; then
    fail "havel failed with the probe preloaded" "$WORK/with.out"
fi
grep -q '^PASS ld preload probe resolves and calls$' "$WORK/with.out" || \
    fail "preloaded probe was not resolved" "$WORK/with.out"
grep '^PASS ld preload' "$WORK/with.out"

echo "== without LD_PRELOAD =="
if env -u LD_PRELOAD HAVEL_STDLIB="$HAVEL_STDLIB" HAVEL_BINARY="$HAVEL_BIN" \
     timeout "$TMO" "$HAVEL_BIN" run "$SCRIPT" >"$WORK/without.out" 2>&1; then
    fail "test unexpectedly passed without the preload" "$WORK/without.out"
fi
grep -q '^FAIL ld preload probe symbol not found$' "$WORK/without.out" || \
    fail "wrong failure without the preload" "$WORK/without.out"
grep '^FAIL ld preload' "$WORK/without.out"

echo "ld-preload-test: PASS ($HAVEL_BIN)"
