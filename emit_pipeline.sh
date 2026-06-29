#!/usr/bin/env bash
# Compile all self-hosted modules/lang/*.hv -> out/modules/lang/*.hvc
# Usage: ./emit_pipeline.sh [havel_binary] [output_dir]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HAVEL="${1:-$SCRIPT_DIR/build-release/havel}"
OUT_DIR="${2:-$SCRIPT_DIR/out/modules/lang}"
SRC_DIR="$SCRIPT_DIR/modules/lang"

if [ ! -x "$HAVEL" ]; then
    echo "emit_pipeline: havel binary not found: $HAVEL" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

PASS=0
FAIL=0
VERSION_HASHES=""

for hv in "$SRC_DIR"/*.hv; do
    name="$(basename "$hv" .hv)"
    out="$OUT_DIR/$name.hvc"
    if "$HAVEL" --build "$hv" -o "$out" 2>/dev/null; then
        sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
        VERSION_HASHES="${VERSION_HASHES}${name}:${sz}\n"
        PASS=$((PASS + 1))
    else
        echo "emit_pipeline: FAILED $name" >&2
        FAIL=$((FAIL + 1))
    fi
done

# Write VERSION file with module sizes
VERSION_FILE="$SCRIPT_DIR/out/VERSION"
echo -e "$VERSION_HASHES" > "$VERSION_FILE"
echo "emit_pipeline: $PASS compiled, $FAIL failed -> $OUT_DIR"
echo "Version info: $VERSION_FILE"
