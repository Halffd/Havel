#!/bin/bash
# ===== Large-program compilation benchmark (self-hosted split ticket #17) =====
#
# Fixtures: 100/500/1000/2500/5000/10000-line synthetic scripts.
# Measures: native compile (--build), self-hosted run (--self-hosted, which
# now precompiles natively + serves from .hvc).
#
# Scaling question tracked: does doubling source size approximately double
# compile time? (linear-ish) — NOT absolute-ms targets.
#
# Usage: scripts/bench_compile.sh [build-dir]
# Env:    HAVEL_BENCH_KEEP=1 keep fixtures + artifacts after the run

set -u
HAVEL="${1:-build-release}/havel"
HAVEL=$(readlink -f "$HAVEL")
[ -x "$HAVEL" ] || { echo "error: $HAVEL not executable"; exit 2; }

SCRATCH="/tmp/opencode/havel-bench-$$"
mkdir -p "$SCRATCH"

make_fixture() {
  # $1 = number of functions (roughly lines * 2)
  local n=$1
  local file="$SCRATCH/bench_${n}.hv"
  {
    echo "// benchmark fixture: ${n} functions"
    echo "fn f0(x) { x + 0 }"
    local i=1
    while [ $i -lt $n ]; do
      echo "fn f${i}(x) { f$((i-1))(x) + $i }"
      i=$((i+1))
    done
    echo "print(f$((n-1))(1))"
  } > "$file"
  echo "$file"
}

timeit() {
  # prints wall seconds with 2 decimals
  local start end
  start=$(date +%s.%N)
  "$@" > /dev/null 2>&1
  end=$(date +%s.%N)
  echo "$end $start" | awk '{printf "%.2f", $1 - $2}'
}

echo "===== fixture | native --build | self-hosted run ====="
printf "%-10s %14s %18s\n" "lines" "native (s)" "selfhosted (s)"

for spec in 100 500 1000 2500 5000 10000; do
  f=$(make_fixture $spec)
  # native: build-only (parse+compile, cache miss forced via fresh content)
  nat=$(timeit "$HAVEL" --no-self-hosted --build "$f")
  # self-hosted: full run (precompile + launcher + VM exec)
  sh=$(timeit "$HAVEL" --self-hosted "$f")
  printf "%-10s %14s %18s\n" "$spec" "$nat" "$sh"
done

# Scaling ratios: t(2n)/t(n) for the native path — expect ~2x for linear.
echo "===== native scaling ratios (t(2n)/t(n), ~2.0 = linear) ====="
# re-run in a fixed order capturing values
prev=0; prev_n=0
for spec in 100 500 1000 2500 5000 10000; do
  f=$(make_fixture $spec)
  nat=$(timeit "$HAVEL" --no-self-hosted --build "$f")
  if [ "$prev" != "0" ]; then
    awk -v a="$nat" -v b="$prev" -v n1="$prev_n" -v n2="$spec" \
      'BEGIN { if (b > 0) printf "  %5d -> %5d : %.2fx\n", n1, n2, a / b }'
  fi
  prev=$nat; prev_n=$spec
done

if [ "${HAVEL_BENCH_KEEP:-0}" != "1" ]; then
  rm -rf "$SCRATCH"
fi
echo "done"
