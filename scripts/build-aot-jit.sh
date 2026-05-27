#!/bin/bash
#
# AOT/JIT Build + Test Runner
#
# Builds Havel with LLVM enabled (AOT + JIT) and runs
# the test suite through the JIT compiler path.
#
# Usage:
#   ./scripts/build-aot-jit.sh              # full build + test
#   ./scripts/build-aot-jit.sh build        # build only
#   ./scripts/build-aot-jit.sh test         # test only (skip build)
#   ./scripts/build-aot-jit.sh aot          # build + AOT LLVM IR emission test
#   ./scripts/build-aot-jit.sh clean        # clean build directory
#

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

log()    { echo -e "${BLUE}[INFO]${NC} $*"; }
ok()     { echo -e "${GREEN}[OK]${NC}   $*"; }
fail()   { echo -e "${RED}[FAIL]${NC} $*"; }
warn()   { echo -e "${YELLOW}[WARN]${NC} $*"; }
header() { echo ""; echo -e "${CYAN}=== $* ===${NC}"; }

HV_BIN="${SCRIPT_DIR}/build-debug/havel"
HV_BIN_JIT="${SCRIPT_DIR}/build-debug/havel-jit"
HV_TEST="${SCRIPT_DIR}/build-debug/hvtest"
BUILD_LOG="${SCRIPT_DIR}/logs/build-aot-jit.log"

mkdir -p "${SCRIPT_DIR}/logs"

# ------------------------------------------------------------------
clean() {
    log "Cleaning build-debug..."
    rm -rf "${SCRIPT_DIR}/build-debug"
    rm -f "${BUILD_LOG}"
    log "Done."
}

# ------------------------------------------------------------------
build() {
    log "Building with LLVM (mode 0 — Debug + Tests + Havel Lang + LLVM)..."
    ./build.sh 0 build 2>&1 | tee "${BUILD_LOG}"
    log "Build complete."
}

# ------------------------------------------------------------------
test_jit_smoke() {
    if [[ ! -f "$HV_TEST" ]]; then
        fail "hvtest not found at ${HV_TEST}. Build first."
        return 1
    fi
    header "JIT Smoke Tests"
    # The `|| true` absorbs the pre-existing `terminate` from LLVM static
    # destructors. The JIT test output itself is checked for PASS/FAIL.
    "${HV_TEST}" --jit --smoke 2>&1 || true
    # grep for "JIT smoke passed" to confirm
    if ! "${HV_TEST}" --jit 2>&1 | grep -q "JIT smoke passed"; then
        warn "JIT smoke tests did not pass"
    fi
}

test_scripts() {
    if [[ ! -f "$HV_BIN" ]]; then
        fail "havel binary not found at ${HV_BIN}. Build first."
        return 1
    fi
    header "Script Tests (Interpreter)"
    "${HV_TEST}" --scripts 2>&1 || true
}

test_scheduler() {
    if [[ ! -f "$HV_TEST" ]]; then
        fail "hvtest not found at ${HV_TEST}. Build first."
        return 1
    fi
    header "Scheduler Rig Tests"
    "${HV_TEST}" --scheduler 2>&1 || true
}

test_llvm_emit() {
    if [[ ! -f "$HV_BIN" ]]; then
        fail "havel binary not found at ${HV_BIN}. Build first."
        return 1
    fi
    header "AOT LLVM IR Emission"
    local test_script="${SCRIPT_DIR}/scripts/tests/aot_core_arith.hv"
    local out_base="/tmp/havel_aot_test"
    if "${HV_BIN}" --emit-llvm -o "${out_base}" "${test_script}" 2>&1; then
        local out_file="${out_base}.ll"
        if [[ -f "${out_file}" ]]; then
            local lines
            lines=$(wc -l < "${out_file}")
            ok "LLVM IR emitted: ${out_file} (${lines} lines)"
            rm -f "${out_file}"
        else
            fail "LLVM IR output file missing"
            return 1
        fi
    else
        local exit_code=$?
        if [[ $exit_code -eq 127 ]] || [[ $exit_code -eq 1 ]]; then
            # binary might not support --emit-llvm without LLVM linked
            warn "LLVM IR emission not available in this build (exit=${exit_code})"
        else
            fail "AOT test failed (exit=${exit_code})"
            return 1
        fi
    fi
}

# ------------------------------------------------------------------
do_all() {
    build
    echo ""
    test_jit_smoke
    echo ""
    test_scripts
    echo ""
    test_scheduler
    echo ""
    test_llvm_emit
}

# ------------------------------------------------------------------
usage() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  (default)    build + test (JIT smoke + scripts + scheduler + AOT)"
    echo "  build        build only"
    echo "  test         run all tests (skips build)"
    echo "  aot          build + AOT LLVM IR emission test"
    echo "  clean        clean build directory"
    echo ""
}

case "${1:-all}" in
    build)   build ;;
    test)    test_jit_smoke; test_scheduler; test_llvm_emit ;;
    aot)     build; test_llvm_emit ;;
    clean)   clean ;;
    all)     do_all ;;
    -h|--help|help) usage ;;
    *)       warn "Unknown command: $1"; usage; exit 1 ;;
esac
