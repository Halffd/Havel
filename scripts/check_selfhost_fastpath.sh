#!/usr/bin/env bash
# Self-hosted fast-path guard.
#
# `havel --self-hosted` runs modules/lang/launcher.hv on the VM. If the user's
# source reaches that launcher as SOURCE, the Havel-implemented lexer, Pratt
# parser, typechecker and emitter tokenize/parse/compile it while the
# interpreter executes them: the compiler compiling itself before it can
# compile the program. Measured on the pre-Milestone-A tree at roughly
# 0.2-0.3 s/line - a 2200-line script spent 225315 ms inside parseAST alone and
# a 5003-line fixture did not finish inside 120 s.
#
# SelfHostedStrategy avoids that: it compiles user scripts with the NATIVE
# frontend (compiler::NativeCompiler -> CompiledUnit), writes the chunk to the
# .hvc cache and hands the launcher bytecode paths. The launcher then loads
# bytecode directly ("precompiled: load", skipping tokenize/parse/infer/emit).
#
# Nothing asserted that. The only evidence was a `TIMING:` line printed by
# launcher.hv that nobody grepped, so the day the precompile path stopped
# engaging every `--self-hosted` run would have quietly gone back to minutes -
# and the whole gate is one line, so it is easy to miss.
#
# This guard runs a fixture larger than the ticket's 5000-line acceptance size
# through both paths and asserts:
#
#   1. both exit 0 and print the same program output (self-hosting still agrees
#      with the native compiler),
#   2. the self-hosted run took the fast path ("precompiled: load"),
#   3. the self-hosted run never entered the interpreted compiler
#      (no "TIMING: parse:" stage timings),
#   4. the self-hosted run finished inside a wall-clock bound, so a silent fall
#      back to the interpreted path fails here instead of in someone's editor,
#   5. the native run did NOT take the fast path, which is what makes (1) a
#      comparison of two genuinely different code paths rather than one path
#      compared with itself.
#
# The fixture path is unique per run so the .hvc cache key cannot collide with
# an earlier run: a guard that passes on a warm cache proves nothing about
# compilation.
#
# Everything runs inside the headless sandbox (HAVEL_HEADLESS=1 with the display
# and session-bus variables cleared) so the guard cannot touch input devices or
# display gamma.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="$(cd "${1:-${REPO_DIR}/build-headless}" 2>/dev/null && pwd)" \
    || { echo "FAIL: build dir '${1:-${REPO_DIR}/build-headless}' not found"; exit 1; }

HAVEL="${BUILD_DIR}/havel"
# The interpreted path took >120 s on a 5003-line fixture. 30 s leaves ~10x
# headroom over the measured fast path (~4 s wall, of which ~3 s is fixed
# startup) while still failing well below the regression.
MAX_MS="${SELF_HOSTED_MAX_MS:-30000}"
# Ticket acceptance size: "large (5000-line) programs must not take minutes to
# parse under normal self-hosted workflow".
MIN_LINES=5000

fail=0

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT
FIXTURE="${WORK_DIR}/parse_scaling_fixture.hv"

# Runs the binary inside the headless sandbox. usage: run_havel <outfile> <args...>
run_havel() {
    local out="$1"; shift
    env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY \
        -u DBUS_SESSION_BUS_ADDRESS \
        HAVEL_HEADLESS=1 HAVEL_STARTUP_TIMING=1 \
        "${HAVEL}" "$@" > "${out}" 2>&1
}

if [ ! -x "${HAVEL}" ]; then
    echo "FAIL: ${HAVEL} not built (build the project, or pass a build dir)"
    exit 1
fi

echo "== fixture =="
# ~11 lines per function plus 200 call sites: comfortably past MIN_LINES while
# still compiling in well under a second natively.
awk 'BEGIN {
    for (i = 0; i < 830; i++)
        printf "\nfn helper%d(a, b) {\n    total = a + b * %d\n    if total > %d {\n        total = total - %d\n    }\n    else {\n        total = total + %d\n    }\n    return total\n}\n",
               i, i % 7 + 1, i, i % 3, i % 5
    print "\nacc = 0"
    for (i = 0; i < 200; i++) printf "acc = helper%d(acc, %d)\n", i, i
    # Sentinel so the comparison below picks the program output out of the log
    # stream instead of diffing timestamps and module-load chatter.
    print "print(\"SENTINEL acc $acc\")"
}' > "${FIXTURE}"

lines="$(wc -l < "${FIXTURE}" | tr -d ' ')"
echo "fixture: ${lines} lines at ${FIXTURE}"
if [ "${lines}" -lt "${MIN_LINES}" ]; then
    echo "FAIL: fixture is ${lines} lines, expected at least ${MIN_LINES}"
    exit 1
fi

echo "== native path =="
native_start=$(date +%s%3N)
run_havel "${WORK_DIR}/native.log" run "${FIXTURE}" --minimal
native_rc=$?
native_ms=$(( $(date +%s%3N) - native_start ))
if [ "${native_rc}" -ne 0 ]; then
    echo "FAIL: native run exited ${native_rc}"
    sed 's/\x1b\[[0-9;]*m//g' "${WORK_DIR}/native.log" | tail -20 | sed 's/^/  /'
    fail=1
fi
native_sentinel="$(grep -ac '^SENTINEL ' "${WORK_DIR}/native.log" 2>/dev/null || true)"
if [ "${native_sentinel}" -eq 0 ]; then
    echo "FAIL: native run produced no SENTINEL output (expected exactly 1)"
    fail=1
fi

# Distinct-file copy so the self-hosted run compiles cold rather than serving
# the .hvc the native run just wrote under the same cache key.
cp "${FIXTURE}" "${WORK_DIR}/parse_scaling_fixture_selfhosted.hv"

echo "== self-hosted path =="
self_start=$(date +%s%3N)
run_havel "${WORK_DIR}/selfhosted.log" run "${WORK_DIR}/parse_scaling_fixture_selfhosted.hv" --self-hosted --minimal
self_rc=$?
self_ms=$(( $(date +%s%3N) - self_start ))
if [ "${self_rc}" -ne 0 ]; then
    echo "FAIL: self-hosted run exited ${self_rc}"
    sed 's/\x1b\[[0-9;]*m//g' "${WORK_DIR}/selfhosted.log" | tail -20 | sed 's/^/  /'
    fail=1
fi
self_sentinel="$(grep -ac '^SENTINEL ' "${WORK_DIR}/selfhosted.log" 2>/dev/null || true)"
if [ "${self_sentinel}" -eq 0 ]; then
    echo "FAIL: self-hosted run produced no SENTINEL output (expected exactly 1)"
    fail=1
fi

if [ "${native_sentinel}" -eq 1 ] && [ "${self_sentinel}" -eq 1 ]; then
    if ! diff <(grep -a '^SENTINEL ' "${WORK_DIR}/native.log") \
              <(grep -a '^SENTINEL ' "${WORK_DIR}/selfhosted.log") > /dev/null; then
        echo "FAIL: self-hosted output differs from the native compiler's"
        echo "  native:      $(grep -a '^SENTINEL ' "${WORK_DIR}/native.log")"
        echo "  self-hosted: $(grep -a '^SENTINEL ' "${WORK_DIR}/selfhosted.log")"
        fail=1
    else
        echo "ok: both paths print $(grep -a '^SENTINEL ' "${WORK_DIR}/selfhosted.log")"
    fi
fi

# -a everywhere: the interpreted path can emit NUL bytes alongside its stage
# timings, and a binary-file verdict from grep would hide the very markers these
# assertions exist to read.
if grep -aq 'precompiled: load' "${WORK_DIR}/selfhosted.log"; then
    echo "ok: self-hosted took the fast path (launcher loaded bytecode: $(grep -ao 'TIMING: precompiled: load = [0-9]*ms' "${WORK_DIR}/selfhosted.log" | head -1))"
else
    echo "FAIL: self-hosted run never reported 'precompiled: load' - user source"
    echo "  reached the interpreted compiler. Check SelfHostedStrategy's precompile step."
    fail=1
fi

if grep -aq 'TIMING: parse:' "${WORK_DIR}/selfhosted.log"; then
    echo "FAIL: self-hosted run executed the interpreted compiler on user source:"
    grep -a 'TIMING: parse:' "${WORK_DIR}/selfhosted.log" | sed 's/\x1b\[[0-9;]*m//g' | sed 's/^/  /'
    fail=1
else
    echo "ok: no interpreted tokenize/parse/typecheck/emit stages ran on user source"
fi

if grep -aq 'precompiled: load' "${WORK_DIR}/native.log"; then
    echo "FAIL: the native run also took the self-hosted fast path, so the"
    echo "  output comparison above compares one code path with itself"
    fail=1
else
    echo "ok: native run did not enter the self-hosted launcher (paths differ)"
fi

if [ "${self_ms}" -gt "${MAX_MS}" ]; then
    echo "FAIL: self-hosted run took ${self_ms}ms, bound is ${MAX_MS}ms"
    fail=1
else
    echo "ok: self-hosted finished in ${self_ms}ms (native ${native_ms}ms, bound ${MAX_MS}ms)"
fi

if [ "${fail}" -ne 0 ]; then
    echo ""
    echo "self-hosted fast-path guard: FAILED"
    exit 1
fi

echo ""
echo "self-hosted fast-path guard: passed (${lines}-line fixture, native ${native_ms}ms, self-hosted ${self_ms}ms)"