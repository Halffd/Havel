#!/usr/bin/env bash
# Benchmark runner (TODO.md #37 / section 42 step 9).
#
# Runs every scripts/benchmarks/*.hv workload across three configurations
# of the release binary and reports wall-clock time plus the runtime
# profiler's counters:
#
#   plain    - interpreter, no optimizer
#   -O       - optimizer pipeline (PipelineOptions::optimizeBytecode)
#   tiering  - interpreter + HAVEL_TIERING=1 JIT tiering
#
# Correctness gate: each script prints name=value checksum lines; the
# runner fails when any configuration's output differs from plain (a
# benchmark that measures a miscompile is worthless).
#
# Usage: scripts/run_benchmarks.sh [build-dir]   (default: build-release)

set -u

BUILD_DIR="${1:-build-release}"
HAVEL="${BUILD_DIR}/havel"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_DIR="${SCRIPT_DIR}/benchmarks"

if [[ ! -x "${HAVEL}" ]]; then
    echo "FAIL: ${HAVEL} not found/executable (build first: ./build.sh 5 build)"
    exit 1
fi

# Time budget: benchmarks are sized for release builds; ASAN debug runs
# would take minutes each.
TIMEOUT="${BENCH_TIMEOUT:-120}"

strip_noise() {
    sed -E 's/\x1b\[[0-9;]*m//g' \
        | grep -vE 'TIMING|Evdev|Brightness|EventListener|uinput|goroutine|startGoroutine|shim|load_module|FFICall|DEBUG CLOSURE'
}

# Correctness view: only the checksum lines (category/bench=value).
checksums() {
    grep -E '^[a-z_]+/[a-z_]+=' "$1" 2>/dev/null
}

# run_config LABEL SCRIPT OUT_FILE ENV_STRING [EXTRA_HAVEL_ARGS...]
run_config() {
    local label="$1"; shift
    local script="$1"; shift
    local out_file="$1"; shift
    local env_str="$1"; shift
    local start end
    start=$(date +%s%N)
    if ! env ${env_str} timeout "${TIMEOUT}" "${HAVEL}" run "$@" "$script" > "${out_file}.raw" 2>/dev/null; then
        echo "run-failed"
        return
    fi
    end=$(date +%s%N)
    strip_noise < "${out_file}.raw" > "${out_file}"
    rm -f "${out_file}.raw"
    echo "$(( (end - start) / 1000000 ))"
}

profiler_line() {
    sed -nE 's/.*\[profiler\] profiler: (.*)/\1/p' "$1" 2>/dev/null | head -1
}

printf "%-12s %-10s %8s %10s %10s %12s\n" "benchmark" "config" "ms" "calls" "backedges" "instructions"
echo "-----------------------------------------------------------------------------------"

total_fail=0
for script in "${BENCH_DIR}"/*.hv; do
    name="$(basename "${script}" .hv)"
    [[ "${name}" == "test_simple" ]] && continue

    base_out="/tmp/havel_bench_${name}"
    plain_ms=$(run_config plain "${script}" "${base_out}.plain.out" "HAVEL_TIERING=0")
    if [[ "${plain_ms}" == "run-failed" ]]; then
        echo "FAIL: ${name} plain run failed/timeout"
        total_fail=$((total_fail+1))
        continue
    fi

    opt_ms=$(run_config -O "${script}" "${base_out}.opt.out" "HAVEL_TIERING=0" -O)
    tier_ms=$(run_config tiering "${script}" "${base_out}.tier.out" "HAVEL_TIERING=1")

    # Correctness: checksum lines must match plain exactly.
    if ! diff -q <(checksums "${base_out}.plain.out") <(checksums "${base_out}.opt.out") >/dev/null 2>&1; then
        echo "FAIL: ${name}: -O output differs from plain"
        total_fail=$((total_fail+1))
    fi
    if ! diff -q <(checksums "${base_out}.plain.out") <(checksums "${base_out}.tier.out") >/dev/null 2>&1; then
        echo "FAIL: ${name}: tiering output differs from plain"
        total_fail=$((total_fail+1))
    fi

    p_plain=$(profiler_line "${base_out}.plain.out" || true)
    p_opt=$(profiler_line "${base_out}.opt.out" || true)
    p_tier=$(profiler_line "${base_out}.tier.out" || true)

    get_field() { echo "$1" | grep -oE "$2=[0-9]+" | head -1 | cut -d= -f2; }

    printf "%-12s %-10s %8s %10s %10s %12s\n" \
        "${name}" "plain" "${plain_ms}" \
        "$(get_field "${p_plain}" calls)" \
        "$(get_field "${p_plain}" backedges)" \
        "$(get_field "${p_plain}" instructions)"
    printf "%-12s %-10s %8s %10s %10s %12s\n" \
        "${name}" "-O" "${opt_ms}" \
        "$(get_field "${p_opt}" calls)" \
        "$(get_field "${p_opt}" backedges)" \
        "$(get_field "${p_opt}" instructions)"
    printf "%-12s %-10s %8s %10s %10s %12s\n" \
        "${name}" "tiering" "${tier_ms}" \
        "$(get_field "${p_tier}" calls)" \
        "$(get_field "${p_tier}" backedges)" \
        "$(get_field "${p_tier}" instructions)"

    # Per-benchmark tier transitions when tiering was on.
    t1=$(get_field "${p_tier}" tier1)
    [[ -n "${t1}" && "${t1}" != "0" ]] && \
        printf "             (tier1=%s tier2=%s)\n" "${t1}" "$(get_field "${p_tier}" tier2)"
done

echo "-----------------------------------------------------------------------------------"
if [[ ${total_fail} -eq 0 ]]; then
    echo "OK: all benchmark outputs consistent across configurations"
else
    echo "FAILED: ${total_fail} correctness mismatch(es)"
fi
exit $((total_fail > 0))
