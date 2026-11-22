#!/usr/bin/env bash
# hvdb-smoke-test.sh - exercise the interactive Havel debugger end to end.
#
# hvdb has no ctest or smoke coverage. This drives the real binary over a
# bounded stdin script (no PTY, no user input), then asserts that breakpoints,
# inspection commands and continued execution all produced the expected state.
# The fixture prints 15 and 30; if the debugger silently skips a breakpoint or
# evaluates a stale frame, those lines are missing and the test fails.
set -euo pipefail

if [ -z "${HVDB_BIN:-}" ]; then
  echo "usage: HVDB_BIN=/absolute/path/to/hvdb $0" >&2
  exit 2
fi
if [ ! -x "$HVDB_BIN" ]; then
  echo "hvdb binary is not executable: $HVDB_BIN" >&2
  exit 2
fi

WORK="$(mktemp -d /tmp/opencode/hvdb-smoke-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
FIXTURE="$WORK/fixture.hv"
OUT="$WORK/output.txt"

cat >"$FIXTURE" <<'EOF'
// hvdb end-to-end fixture
a = 10
b = a + 5 // DBG_BREAK 3
print(b)
c = b * 2
print(c)
return 0
EOF

COMMANDS=$(cat <<'EOF'
help
break list
break 6
watch b
watch list
info breakpoints
info funcs
info watches
funcs
heap
threads
modules
hostfuncs print
hostfunc print
regs
stack
frame 0
locals
list
globals
catch throw
catch uncaught
catch
dis __main__
continue
print a
step
print a
continue
print b
watch clear 0
delete all
continue
EOF
)

set +e
printf '%s\n' "$COMMANDS" | timeout 90 "$HVDB_BIN" "$FIXTURE" >"$OUT" 2>&1
rc=${PIPESTATUS[1]}
set -e

fail() {
  echo "FAIL: $1" >&2
  echo "--- hvdb output ---" >&2
  tail -100 "$OUT" >&2
  exit 1
}

if [ "$rc" -eq 124 ]; then
  fail "hvdb timed out after 90s"
fi
if [ "$rc" -ne 0 ]; then
  fail "hvdb exited $rc"
fi

need() { grep -Fq -- "$1" "$OUT" || fail "missing output: $1"; }
# The debugger prints its prompt before the command runs, so a script's stdout
# can arrive on the same line as "(hvdb) ". Accept both shapes.
need_value() {
  grep -Eq "^(\(hvdb\) )?${1}$" "$OUT" || fail "missing output value: $1"
}
forbid() { grep -Fq -- "$1" "$OUT" && fail "unexpected output: $1"; return 0; }

need "hvdb - Havel Debugger"
need "Auto-breakpoint at line 3"
need "Breakpoint at $FIXTURE:1 (main entry)"
need "Breakpoint hit at $FIXTURE:2 in __main__"
need "Breakpoint hit at $FIXTURE:3 in __main__"
need "Breakpoint hit at $FIXTURE:6 in __main__"

need "print <expr>              Evaluate expression"
need "Breakpoint at $FIXTURE:6"
need "Watch: b"
need "#0 b"
need "#0 break $FIXTURE:3"
need "__main__ (0 params"
need "Heap size:"
need "Total goroutines:"
need "Loader search paths:"
need "name:        print"
need "Debugger: attached"
need "Switched to frame #0"
need "(no locals)"
need "Break on throw"
need "Break on uncaught"
need "Cleared watch #0"
need "Cleared all"

need "  a = 10"
need "  b = 15"
need_value "15"
need_value "30"
forbid "Unknown command"

echo "hvdb-smoke: PASS (breakpoints, inspection commands and execution verified)"
