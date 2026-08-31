#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HAVEL_BIN="${PROJECT_ROOT}/build-debug/havel"
BENCHMARKS_DIR="${SCRIPT_DIR}/benchmarks"
RESULTS_DIR="${PROJECT_ROOT}/tests/baseline"
BASELINE_FILE="${RESULTS_DIR}/baseline_$(date +%Y%m%d).json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

mkdir -p "$RESULTS_DIR"

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[!]${NC} $1"
}

# Verify havel binary exists
if [[ ! -f "$HAVEL_BIN" ]]; then
    log_error "Havel binary not found at: $HAVEL_BIN"
    log_info "Building Havel..."
    cd "$PROJECT_ROOT"
    ./build.sh 6 build
    if [[ ! -f "$HAVEL_BIN" ]]; then
        log_error "Build failed"
        exit 1
    fi
fi

log_info "Havel binary: $HAVEL_BIN"
log_info "Benchmarks directory: $BENCHMARKS_DIR"
log_info "Results will be saved to: $BASELINE_FILE"
echo ""

# Initialize results JSON
echo "{" > "$BASELINE_FILE"
echo "  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"," >> "$BASELINE_FILE"
echo "  \"system_info\": {" >> "$BASELINE_FILE"
echo "    \"cores\": $(nproc 2>/dev/null || echo 'unknown')," >> "$BASELINE_FILE"
echo "    \"hostname\": \"$(hostname)\"," >> "$BASELINE_FILE"
echo "    \"os\": \"$(uname -s)\"" >> "$BASELINE_FILE"
echo "  }," >> "$BASELINE_FILE"
echo "  \"benchmarks\": {" >> "$BASELINE_FILE"

first_benchmark=true

# Run each benchmark
for benchmark_file in "$BENCHMARKS_DIR"/*.hv; do
    if [[ ! -f "$benchmark_file" ]]; then
        continue
    fi
    
    benchmark_name=$(basename "$benchmark_file" .hv)
    log_info "Running benchmark: $benchmark_name"
    
    # Run benchmark 3 times and collect timing
    times=()
    for run in 1 2 3; do
        # Time the havel execution
        start_time=$(date +%s%N)
        
        if "$HAVEL_BIN" run "$benchmark_file" > /dev/null 2>&1; then
            end_time=$(date +%s%N)
            elapsed_ms=$(( (end_time - start_time) / 1000000 ))
            times+=($elapsed_ms)
            log_info "  Run $run: ${elapsed_ms}ms"
        else
            log_error "  Run $run failed"
            times+=(0)
        fi
    done
    
    # Calculate statistics
    min_time=${times[0]}
    max_time=${times[0]}
    sum_time=0
    
    for t in "${times[@]}"; do
        sum_time=$((sum_time + t))
        if [[ $t -lt $min_time ]]; then min_time=$t; fi
        if [[ $t -gt $max_time ]]; then max_time=$t; fi
    done
    
    avg_time=$((sum_time / 3))
    
    log_success "$benchmark_name: min=${min_time}ms avg=${avg_time}ms max=${max_time}ms"
    
    # Add to JSON
    if [[ "$first_benchmark" == false ]]; then
        echo "," >> "$BASELINE_FILE"
    fi
    first_benchmark=false
    
    echo "    \"$benchmark_name\": {" >> "$BASELINE_FILE"
    echo "      \"runs\": [${times[0]}, ${times[1]}, ${times[2]}]," >> "$BASELINE_FILE"
    echo "      \"min_ms\": $min_time," >> "$BASELINE_FILE"
    echo "      \"avg_ms\": $avg_time," >> "$BASELINE_FILE"
    echo "      \"max_ms\": $max_time" >> "$BASELINE_FILE"
    echo "    }" >> "$BASELINE_FILE"
    
    echo ""
done

# Close JSON
echo "  }" >> "$BASELINE_FILE"
echo "}" >> "$BASELINE_FILE"

log_success "Baseline results saved to: $BASELINE_FILE"
log_info "Displaying results:"
echo ""
cat "$BASELINE_FILE" | jq '.' 2>/dev/null || cat "$BASELINE_FILE"
