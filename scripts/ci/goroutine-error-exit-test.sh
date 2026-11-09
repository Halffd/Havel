#!/usr/bin/env bash
# goroutine-error-exit-test.sh - the boundary contract for uncaught script
# errors raised inside `go` fibers.
#
# Rule (docs/contributing/reactive-hotkeys.md): an uncaught script error in
# a goroutine must surface as a normal script error - reported, exit 1 -
# never as std::terminate/SIGABRT. ScriptError is a plain struct, not
# std::exception, so it only lands if the engine entry points
# (HavelEngine::execute / tickGoroutines via convertUncaughtScriptError)
# keep their conversion catches.
#
# Usage: scripts/ci/goroutine-error-exit-test.sh [HAVEL_BIN]
#   default binary: ./build-debug/havel (override with HAVEL_BIN=)
set -u
cd "$(dirname "$0")/../.."

HAVEL_BIN="${1:-${HAVEL_BIN:-./build-debug/havel}}"
if [ ! -x "$HAVEL_BIN" ]; then
  echo "FAIL: no havel binary at $HAVEL_BIN (build first, or set HAVEL_BIN)" >&2
  exit 1
fi

TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

cat > "$TMPD/goroutine_throw.hv" <<'HV'
go fn() { x = nil; y = x.foo }
sleep(500)
exit(0)
HV

# Generous: this box runs other sessions' builds; a timeout must mean a
# hung boundary, not a slow box (observed 120s false-failures under load).
timeout 300 "$HAVEL_BIN" "$TMPD/goroutine_throw.hv" >"$TMPD/out" 2>&1
rc=$?

if [ "$rc" -eq 134 ] || grep -aq "terminate called" "$TMPD/out"; then
  echo "FAIL: uncaught goroutine error aborted the process (rc=$rc)"
  grep -a "terminate" "$TMPD/out" | head -2
  exit 1
fi
if [ "$rc" -ne 1 ]; then
  echo "FAIL: expected exit 1 (clean script error), got rc=$rc"
  tail -3 "$TMPD/out"
  exit 1
fi
if ! grep -aq "Execution error: Cannot read property 'foo' of nil" "$TMPD/out"; then
  echo "FAIL: boundary did not report the real message"
  tail -3 "$TMPD/out"
  exit 1
fi

echo "PASS: goroutine script error reported and exits 1 (no SIGABRT)"
