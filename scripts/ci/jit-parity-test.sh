#!/usr/bin/env bash
# jit-parity-test.sh - the only accepted verification for tiering/JIT changes.
#
# "Compiles clean" is not verification. This gate proves, per script:
#   1. the interpreted run passes (reference),
#   2. the tiered run passes (--tiering, low tier1 threshold),
#   3. the JIT actually compiled something ([tiering] tier1=N, N>0) -
#      a "tiered" run that never tiers is a FAIL, not a pass,
#   4. the tiered output matches the interpreted output (parity),
#      after filtering runtime log lines (timestamps + [INFO]/[WARN]/...).
# Every run is under a timeout: a hung JIT test fails in TMO seconds,
# it can never hang the gate indefinitely.
#
# Usage:
#   scripts/ci/jit-parity-test.sh
#   HAVEL_BIN=./build-release/havel scripts/ci/jit-parity-test.sh
#   REQUIRE_TIER2=1 scripts/ci/jit-parity-test.sh   # LLVM builds: assert
#                                                   # tier2_compiled>0 too
#   TMO=120 scripts/ci/jit-parity-test.sh    # per-run timeout seconds
#
# NOTE ON BINARIES: the default dev build (build-debug) has NO JIT
# backend (ENABLE_CRANELIFT is off by default). Pointing this gate at
# a backend-less binary fails every script with "JIT never tiered" -
# which is correct: you cannot verify a JIT change on a binary without
# a JIT. Default picks the first JIT-capable build that exists.
set -u
cd "$(dirname "$0")/../.."

if [ -n "${HAVEL_BIN:-}" ]; then
  :
elif [ -x ./build-crane-nollvm-hl/havel ]; then
  HAVEL_BIN=./build-crane-nollvm-hl/havel
elif [ -x ./build-release/havel ]; then
  HAVEL_BIN=./build-release/havel
else
  HAVEL_BIN=./build-debug/havel
fi
TMO="${TMO:-120}"
TIER1_THRESHOLD="${TIER1_THRESHOLD:-3}"
TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

if [ ! -x "$HAVEL_BIN" ]; then
  echo "FAIL: no havel binary at $HAVEL_BIN (build first, or set HAVEL_BIN)" >&2
  exit 1
fi

# Loop- and closure-bearing scripts so the hotness signals (backedge,
# binop feedback, invocation counts) actually cross the tier-1
# threshold, plus the tier-1 lowerings (div/mod/negate/bitwise) inside
# loop bodies. Straight-line scripts never tier and cannot be JIT
# tests.
CANDIDATES="for_loop_range for_loop_array for_loop_break loop_with_break \
do_while closure_loop_capture chunk_navigator"

# Known failures: the gate reports these LOUDLY as skips with the bug
# reference. They are never silently dropped. Remove the entry when the
# bug is fixed and the gate will hold it from then on.
# - chunk_navigator (2026-10-15, this gate's first catch): tier-1
#   compile of chunkAt/chunkLeft refuses with "join depth conflict
#   (1 vs 0)" (cranelift-backend/src/lib.rs ~911) and silently falls
#   back to the interpreter. The default-arg prologue
#   (DUP; JUMP_IF_NULL skip; POP; LOAD_GLOBAL; STORE_VAR) genuinely
#   joins its two arms at different operand depths - benign under the
#   interpreter's truncate-at-RETURN semantics, fatal to the model's
#   depths-only-converge invariant. See benchmarks README known issues.
KNOWN_FAILURES="chunk_navigator"

# Strip ANSI color codes and runtime log lines (timestamp + [LEVEL]).
# Script-owned output (print/FAIL/PASS lines) has no timestamp prefix.
normalize() {
  sed -e 's/\x1b\[[0-9;]*m//g' "$1" \
    | grep -avE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:.]+ +\[(INFO|WARNING|WARN|ERROR|DEBUG|TRACE|CRITICAL)\]' \
    || true
}

pass=0
fail=0

for s in $CANDIDATES; do
  f="scripts/smoke/$s.hv"
  if [ ! -f "$f" ]; then
    echo "FAIL $s: missing scripts/smoke/$s.hv"
    fail=$((fail + 1))
    continue
  fi
  case " $KNOWN_FAILURES " in
    *" $s "*)
      echo "SKIP $s: KNOWN FAILURE (tier-1 compile refusal + silent interpreter fallback; see the KNOWN_FAILURES comment in this script)"
      continue
      ;;
  esac

  # 1. reference interpreted run
  timeout "$TMO" env HAVEL_HEADLESS=1 "$HAVEL_BIN" run "$f" \
    >"$TMPD/$s.ref" 2>&1
  ref_rc=$?
  if [ "$ref_rc" -eq 124 ]; then
    echo "FAIL $s: interpreted run hung (> ${TMO}s)"
    fail=$((fail + 1)); continue
  fi
  if [ "$ref_rc" -ne 0 ]; then
    echo "FAIL $s: interpreted run rc=$ref_rc (reference must pass first)"
    fail=$((fail + 1)); continue
  fi

  # 2. tiered run
  timeout "$TMO" env HAVEL_HEADLESS=1 "$HAVEL_BIN" run \
    --tiering --tier1-threshold "$TIER1_THRESHOLD" "$f" \
    >"$TMPD/$s.tier" 2>&1
  tier_rc=$?
  if [ "$tier_rc" -eq 124 ]; then
    echo "FAIL $s: tiered run hung (> ${TMO}s) - miscompiled code can hang"
    fail=$((fail + 1)); continue
  fi
  if [ "$tier_rc" -ne 0 ]; then
    echo "FAIL $s: tiered run rc=$tier_rc (self-assertion failed under JIT)"
    fail=$((fail + 1)); continue
  fi

  # 3. the JIT must have actually compiled something
  tier1="$(grep -aoE 'tier1=[0-9]+' "$TMPD/$s.tier" | head -1 | cut -d= -f2)"
  if [ -z "$tier1" ] || [ "$tier1" -eq 0 ]; then
    echo "FAIL $s: JIT never tiered (tier1=${tier1:-line absent}) - a tiered run that stays interpreted is not a JIT test"
    fail=$((fail + 1)); continue
  fi
  # 3b. tier-1 compile failures are gate failures: a function whose
  # compile failed falls back to the interpreter silently - the run
  # "passes" while the very code under test never ran compiled.
  if grep -aqE '\[hclb\] .* failed|\[tiering\] .* compile .* fail' "$TMPD/$s.tier"; then
    echo "FAIL $s: tier-1 compile failure(s) with silent interpreter fallback:"
    grep -aE '\[hclb\] .* failed|\[tiering\] .* compile .* fail' "$TMPD/$s.tier" | head -4
    fail=$((fail + 1)); continue
  fi
  if [ "${REQUIRE_TIER2:-0}" = "1" ]; then
    tier2="$(grep -aoE 'tier2_compiled=[0-9]+' "$TMPD/$s.tier" | head -1 | cut -d= -f2)"
    if [ -z "$tier2" ] || [ "$tier2" -eq 0 ]; then
      echo "FAIL $s: tier2 never compiled (REQUIRE_TIER2=1, tier2_compiled=${tier2:-absent})"
      fail=$((fail + 1)); continue
    fi
  fi

  # 4. parity: script-owned output must match the interpreter's
  if ! diff -q <(normalize "$TMPD/$s.ref") <(normalize "$TMPD/$s.tier") >/dev/null; then
    echo "FAIL $s: output divergence between interpreter and tier-1:"
    diff <(normalize "$TMPD/$s.ref") <(normalize "$TMPD/$s.tier") | head -10
    fail=$((fail + 1)); continue
  fi

  echo "PASS $s (tier1=$tier1)"
  pass=$((pass + 1))
done

echo "jit-parity: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
