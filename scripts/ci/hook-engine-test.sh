#!/usr/bin/env bash
# hook-engine-test.sh - build and run the x86-64 detour engine regression tests.
#
# The modding module's inline hook engine patches executable memory, so its
# correctness cannot be assumed from a successful build: a wrong prologue
# length silently corrupts callers and a wrong rel32 jumps anywhere. Both
# defects in this area so far were caught only by a real run of these tests:
#   1. the 5-byte patch's rel32 was computed from the destination stack
#      buffer instead of the jump's runtime address -> random jump target;
#   2. mprotect(PROT_WRITE) on file-backed exec text SIGSEGVs on hardened
#      hosts, so /proc/self/mem is the primary write path.
#
# Runs two binaries: a decoder unit test over the exact instruction shapes
# clang/gcc emit at -O2 function entries, and an end-to-end detour test that
# installs, calls through, and removes a real hook in a fresh process.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d /tmp/opencode/hook-engine-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

CC="${CC:-clang}"
# The decoder test compiles anywhere; the detour test is compiled at -O2 so it
# runs against the prologue shape real optimized builds actually emit
# (endbr64 / push rbp / mov rbp,rsp / sub rsp,N / mov [rbp-N],edi), and so the
# 4-byte `tiny` body is short enough for the engine to correctly refuse.
CFLAGS="-std=c17 -Wall -Wextra -Wpedantic -I$ROOT"
DECODER_CFLAGS="$CFLAGS -O0 -g"
DETOUR_CFLAGS="$CFLAGS -O2 -g"

fail() {
    echo "FAIL: $1" >&2
    [ -n "${2:-}" ] && [ -f "$2" ] && tail -60 "$2" >&2
    exit 1
}

echo "== decoder unit test =="
if ! $CC $DECODER_CFLAGS "$ROOT/scripts/tests/hook-engine/test_decoder.c" -o "$WORK/test_decoder" >"$WORK/build_decoder.log" 2>&1; then
    fail "decoder test failed to compile" "$WORK/build_decoder.log"
fi
if ! "$WORK/test_decoder" >"$WORK/decoder.out" 2>&1; then
    fail "decoder test failed" "$WORK/decoder.out"
fi
tail -1 "$WORK/decoder.out"
grep -q '^PASS:' "$WORK/decoder.out" || fail "decoder test did not report PASS" "$WORK/decoder.out"

echo "== end-to-end detour test =="
# A linker script is not used: the trampoline page must land within +-2GB of
# the target, which the engine's alloc_near() arranges at runtime.
if ! $CC $DETOUR_CFLAGS "$ROOT/scripts/tests/hook-engine/test_detour.c" \
       "$ROOT/src/modding/hook.c" -o "$WORK/test_detour" >"$WORK/build_detour.log" 2>&1; then
    fail "detour test failed to compile" "$WORK/build_detour.log"
fi
if ! "$WORK/test_detour" >"$WORK/detour.out" 2>&1; then
    fail "detour test failed" "$WORK/detour.out"
fi
tail -3 "$WORK/detour.out"
grep -q '^PASS:' "$WORK/detour.out" || fail "detour test did not report PASS" "$WORK/detour.out"

echo "hook-engine-test: PASS"