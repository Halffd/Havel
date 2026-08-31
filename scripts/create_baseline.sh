#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HAVEL_BIN="${PROJECT_ROOT}/build-debug/havel"
RESULTS_DIR="${PROJECT_ROOT}/tests/baseline"

mkdir -p "$RESULTS_DIR"

BASELINE_FILE="${RESULTS_DIR}/vm_baseline_$(date +%Y%m%d_%H%M%S).json"

# Colors
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[✓]${NC} $1"; }
log_error() { echo -e "${RED}[✗]${NC} $1"; }

# Verify binary exists
if [[ ! -f "$HAVEL_BIN" ]]; then
    log_error "Havel binary not found at: $HAVEL_BIN"
    exit 1
fi

log_info "Starting VM baseline benchmarks..."
log_info "Havel binary: $HAVEL_BIN"
log_info "Results: $BASELINE_FILE"
echo ""

# Initialize JSON results
{
    echo "{"
    echo "  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"system\": {"
    echo "    \"cores\": $(nproc 2>/dev/null || echo 'unknown'),"
    echo "    \"hostname\": \"$(hostname)\","
    echo "    \"os\": \"$(uname -s)\""
    echo "  },"
    echo "  \"tests\": {"
} > "$BASELINE_FILE"

first=true

# Run each smoke test and collect timing
for test_file in $(cd "$PROJECT_ROOT" && find scripts/smoke -name "*.hv" -type f | sort | head -20); do
    test_name=$(basename "$test_file" .hv)
    
    # Run test with timing
    log_info "Running: $test_name"
    
    start=$(date +%s%N)
    if timeout 60 "$HAVEL_BIN" run "$test_file" > /dev/null 2>&1; then
        end=$(date +%s%N)
        elapsed_ms=$(( (end - start) / 1000000 ))
        result="PASS"
        log_success "$test_name: ${elapsed_ms}ms"
    else
        end=$(date +%s%N)
        elapsed_ms=$(( (end - start) / 1000000 ))
        result="FAIL"
        log_error "$test_name: ${elapsed_ms}ms (failed)"
    fi
    
    # Add to JSON
    if [[ "$first" == false ]]; then
        echo "," >> "$BASELINE_FILE"
    fi
    first=false
    
    {
        echo "    \"$test_name\": {"
        echo "      \"file\": \"$test_file\","
        echo "      \"status\": \"$result\","
        echo "      \"elapsed_ms\": $elapsed_ms"
        echo "    }"
    } >> "$BASELINE_FILE"
    
    echo ""
done

# Close JSON
{
    echo "  }"
    echo "}"
} >> "$BASELINE_FILE"

log_success "Baseline saved to: $BASELINE_FILE"
log_info ""
log_info "Baseline Summary:"
echo ""
cat "$BASELINE_FILE" | jq '.' 2>/dev/null || cat "$BASELINE_FILE"
