#!/bin/bash

# Havel Benchmark Runner
# Runs all benchmarks in the scripts/benchmarks directory

set -e

HAVEL_BIN="./build-release/havel"
BENCH_DIR="scripts/benchmarks"
BASELINE_FILE="tests/baseline/benchmarks.json"
BACKEND="${1:-}"

if [ ! -f "$HAVEL_BIN" ]; then
    echo "Error: $HAVEL_BIN not found. Build first: ./build.sh 5"
    exit 1
fi

if [ -z "$BACKEND" ]; then
    BACKEND="default"
fi

# Run all benchmarks
timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
results="{"
results+="\"timestamp\": \"$timestamp\"," 
results+="\"backend\": \"$BACKEND\","
results+="\"config\": \"release\",
results+="\"benchmarks\": {"

first=true
for script in "$BENCH_DIR"/*.hv; do
    name=$(basename "$script" .hv)
    
    if [ "$first" = true ]; then
        first=false
    else
        results+=","
    fi
    
    # Run benchmark with tiering and capture timing
    output=$($HAVEL_BIN run "$script" --headless --tiering 2>&1 | strings)
    
    # Extract timing information (total script run time)
    timing=$(echo "$output" | grep -E '^TIMING:|Total.*time' || echo "")
    
    # Extract any benchmark-specific outputs
    results_str=$(echo "$output" | grep -E 'PASS|RESULT|result|done' || echo "")
    
    results+="\"$name\": \"$timing\""
done

results+="}"}," 
results+="\"instruments\": 300," 
results+="\"timestamp\": \"$timestamp\"" 
results+="}"

echo "$results"

# Write to baseline
mkdir -p "$(dirname "$BASELINE_FILE")"
echo "$results" > "$BASELINE_FILE"
echo ""
echo "Benchmark results written to $BASELINE_FILE"