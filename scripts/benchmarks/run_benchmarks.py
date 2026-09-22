#!/usr/bin/env python3
"""Havel Benchmark Runner

Runs all benchmarks in scripts/benchmarks/ and captures timing metrics.
Writes results to tests/baseline/benchmarks.json.
"""

import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone

HAVEL_BIN = "./build-release/havel"
BENCH_DIR = "scripts/benchmarks"
BASELINE_FILE = "tests/baseline/benchmarks.json"


def run_benchmark(script_path: str):
    """Run a benchmark and extract timing."""
    start = time.perf_counter()
    try:
        result = subprocess.run(
            [HAVEL_BIN, "run", script_path, "--headless", "--tiering"],
            capture_output=True,
            text=True,
            timeout=120,
            env={**os.environ, "HCL_BACKEND": "cranelift"},
        )
        elapsed = time.perf_counter() - start
        output = result.stdout + result.stderr
        returncode = result.returncode

        # Extract tiering info
        tiering = {}
        for line in output.splitlines():
            if "tiering]" in line:
                for part in line.split():
                    if "=" in part:
                        k, v = part.split("=", 1)
                        tiering[k] = int(v)
                break
        # Extract profiler stats
        profiler_stats = None
        for line in output.splitlines():
            if "profiler." in line:
                parts = line.split()
                stats = {}
                for p in parts:
                    if "calls=" in p:
                        stats["calls"] = int(p.split("=")[1])
                for p in parts:
                    if "instructions=" in p:
                        stats["instructions"] = int(p.split("=")[1])
                for p in parts:
                    if "allocations=" in p:
                        stats["allocations"] = int(p.split("=")[1])
                for p in parts:
                    if "throws=" in p:
                        stats["throws"] = int(p.split("=")[1])
                profiler_stats = stats
                break

        return {
            "benchmark": os.path.splitext(os.path.basename(script_path))[0],
            "elapsed_ms": elapsed * 1000,
            "pass": returncode == 0,
            "tiering": tiering,
            "profiler": profiler_stats,
        }
    except Exception as e:
        return {
            "benchmark": os.path.splitext(os.path.basename(script_path))[0],
            "elapsed_ms": (time.perf_counter() - start) * 1000,
            "pass": False,
            "error": str(e),
        }


def main():
    if not os.path.exists(HAVEL_BIN):
        print(
            f"Error: {HAVEL_BIN} not found. Build first: ./build.sh 5", file=sys.stderr
        )
        sys.exit(1)

    results = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "instrument": "havel_v312",
        "version": "0.1.0",
        "config": "release",
        "backend": "tiered",
        "benchmarks": {},
    }

    # Run each benchmark
    for script in sorted(os.listdir(BENCH_DIR)):
        if not script.endswith(".hv"):
            continue
        path = os.path.join(BENCH_DIR, script)
        result = run_benchmark(path)
        results["benchmarks"][result["benchmark"]] = {
            "elapsed_ms": result["elapsed_ms"],
            "pass": result["pass"],
            **{k: v for k, v in result.items() if k not in ("benchmark", "pass")},
        }

    # Write JSON output
    os.makedirs(os.path.dirname(BASELINE_FILE), exist_ok=True)
    with open(BASELINE_FILE, "w") as f:
        json.dump(results, f, indent=2)

    print(f"Benchmarks complete: {len(results['benchmarks'])} benchmarks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
