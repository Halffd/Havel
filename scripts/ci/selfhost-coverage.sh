#!/bin/bash
# Self-hosted compiler coverage gate: lint every .hv script in the repo
# through the Havel-implemented pipeline (modules/lang launcher --lint)
# and report per-directory parse coverage.
#
# Lint is side-effect-free: tokenize -> parse -> infer, never executes the
# target. Safe for hardware-touching scripts (brightness, io, dsl input).
#
# Usage: scripts/ci/selfhost-coverage.sh [build-dir] [baseline]
#   build-dir  defaults to build-nollvm
#   baseline   optional expected pass count; exit 1 when below it
#
# Output: results TSV (status, file, first error) on stdout, summary on
# stderr. Exit 0 when every file lints, 1 otherwise (or below baseline).

set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${1:-$REPO/build-nollvm}"
BASELINE="${2:-}"
LAUNCHER="$REPO/modules/lang/launcher.hv"
LANG_DIR="$REPO/modules/lang"
WORK="$(mktemp -d /tmp/havel-selfhost-coverage.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$BUILD_DIR/havel" ]; then
    echo "error: $BUILD_DIR/havel not found (build first)" >&2
    exit 2
fi

# One warm-up run: populates the module .hvc cache so the sweep is not
# dominated by N parallel cold compiles of pratt/emitter.
"$BUILD_DIR/havel" run "$LAUNCHER" -- --script-dir "$LANG_DIR" \
    --lint "$LANG_DIR/lexer.hv" >/dev/null 2>&1 || true

find "$REPO" -name "*.hv" \
    -not -path "$REPO/build*" -not -path "$REPO/.git*" \
    -not -path "$REPO/out/*" -not -path "$REPO/havel-wm/*" \
    | sed "s|^$REPO/||" | sort > "$WORK/files.txt"

run_one() {
    f="$1"
    out=$(timeout 120 "$BUILD_DIR/havel" run "$LAUNCHER" \
        -- --script-dir "$LANG_DIR" --lint "$REPO/$f" 2>&1)
    if echo "$out" | grep -aq "Linting successful"; then
        printf 'PASS\t%s\n' "$f"
    else
        err=$(echo "$out" | grep -aE "Parse error|Bytecode error|Error:" | head -1 | cut -c1-160)
        [ -z "$err" ] && err="NO-OUTPUT-OR-TIMEOUT"
        printf 'FAIL\t%s\t%s\n' "$f" "$err"
    fi
}
export -f run_one
export BUILD_DIR LAUNCHER LANG_DIR REPO

xargs -a "$WORK/files.txt" -P 8 -I{} bash -c 'run_one "$@"' _ {} > "$WORK/results.tsv"

total=$(wc -l < "$WORK/results.tsv")
pass=$(grep -c "^PASS" "$WORK/results.tsv" || true)
fail=$((total - pass))

echo "selfhost coverage: $pass/$total pass"
grep "^FAIL" "$WORK/results.tsv" | cut -f2,3 | sed 's/^/  FAIL /'
cat "$WORK/results.tsv"

if [ -n "$BASELINE" ] && [ "$pass" -lt "$BASELINE" ]; then
    echo "error: $pass below baseline $BASELINE" >&2
    exit 1
fi
[ "$fail" -eq 0 ]
