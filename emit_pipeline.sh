#!/usr/bin/env bash
# Compile all self-hosted modules/lang/*.hv, modules/std/*.hv, and modules/app/*.hv.
# Bytecode cache (.hvc) lives only in ~/.cache/havel/ with namespaced filenames:
#   lang.<name>.hvc for lang modules, std.<name>.hvc for std modules, app.<name>.hvc for app modules.
# Source mirrors stay in out/modules/{lang,std,app} for the self-hosted
# launcher gate (HavelLauncher checks out/modules/lang is non-empty).
# Usage: ./emit_pipeline.sh [havel_binary]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HAVEL="${1:-$SCRIPT_DIR/build-release/havel}"
CACHE_DIR="$HOME/.cache/havel"
SRC_DIR="$SCRIPT_DIR/modules/lang"
STD_SRC_DIR="$SCRIPT_DIR/modules/std"
APP_SRC_DIR="$SCRIPT_DIR/modules/app"
OUT_DIR="$SCRIPT_DIR/out/modules/lang"
STD_OUT_DIR="$SCRIPT_DIR/out/modules/std"
APP_OUT_DIR="$SCRIPT_DIR/out/modules/app"

if [ ! -x "$HAVEL" ]; then
    echo "emit_pipeline: havel binary not found: $HAVEL" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
mkdir -p "$STD_OUT_DIR"
mkdir -p "$APP_OUT_DIR"
mkdir -p "$CACHE_DIR"

PASS=0
FAIL=0
VERSION_HASHES=""

emit_one() {
    local hv="$1"
    local name
    name="$(basename "$hv" .hv)"
    local prefix="$2"
    local cache_name="$3"
    local out="$CACHE_DIR/${cache_name}.hvc"
    if "$HAVEL" --build "$hv" -o "$out" --no-strict-semantics 2>/dev/null; then
        sz=$(stat -c%s "$out" 2>/dev/null || echo 0)
        VERSION_HASHES="${VERSION_HASHES}${prefix}${name}:${sz}\n"
        PASS=$((PASS + 1))
        # Copy source .hv next to the .hvc cache for hash/mtime validation
        cp "$hv" "$CACHE_DIR/${cache_name}.hv"
        # Keep the source mirror in out/ (self-hosted launcher gate marker)
        cp "$hv" "$4/$name.hv"
    else
        echo "emit_pipeline: FAILED ${prefix}${name}" >&2
        FAIL=$((FAIL + 1))
        # Clean up stale cache entries from previous successful builds
        rm -f "$CACHE_DIR/${cache_name}.hvc" "$CACHE_DIR/${cache_name}.hv"
        rm -f "$4/$name.hv"
    fi
}

echo "emit_pipeline: building lang modules -> $CACHE_DIR (lang.*)"
for hv in "$SRC_DIR"/*.hv; do
    emit_one "$hv" "lang." "lang.$(basename "$hv" .hv)" "$OUT_DIR"
done

# Also build the launcher itself (self-hosted compiler driver)
echo "emit_pipeline: building launcher -> $CACHE_DIR (launcher)"
LAUNCHER_HV="$SRC_DIR/launcher.hv"
LAUNCHER_CACHE="$CACHE_DIR/launcher.hvc"
if "$HAVEL" --build "$LAUNCHER_HV" -o "$LAUNCHER_CACHE" --no-strict-semantics 2>/dev/null; then
    sz=$(stat -c%s "$LAUNCHER_CACHE" 2>/dev/null || echo 0)
    VERSION_HASHES="${VERSION_HASHES}launcher:${sz}\n"
    PASS=$((PASS + 1))
    cp "$LAUNCHER_HV" "$CACHE_DIR/launcher.hv"
    cp "$LAUNCHER_HV" "$OUT_DIR/launcher.hv"
else
    echo "emit_pipeline: FAILED launcher" >&2
    FAIL=$((FAIL + 1))
    rm -f "$CACHE_DIR/launcher.hvc" "$CACHE_DIR/launcher.hv"
    rm -f "$OUT_DIR/launcher.hv"
fi

echo "emit_pipeline: building std modules -> $CACHE_DIR (std.*)"
for hv in "$STD_SRC_DIR"/*.hv; do
    emit_one "$hv" "std." "std.$(basename "$hv" .hv)" "$STD_OUT_DIR"
done

echo "emit_pipeline: building app modules -> $CACHE_DIR (app.*)"
for hv in "$APP_SRC_DIR"/*.hv; do
    name="$(basename "$hv" .hv)"
    # Skip async and async_mod - they have C++ compiler bugs with named imports
    # They will be compiled on-demand by the self-hosted compiler
    if [ "$name" = "async" ] || [ "$name" = "async_mod" ]; then
        echo "emit_pipeline: skipping $name (on-demand compile)"
        continue
    fi
    emit_one "$hv" "app." "app.$name" "$APP_OUT_DIR"
done

# Write VERSION file with module sizes
VERSION_FILE="$SCRIPT_DIR/out/VERSION"
mkdir -p "$(dirname "$VERSION_FILE")"
echo -e "$VERSION_HASHES" > "$VERSION_FILE"
echo "emit_pipeline: $PASS compiled, $FAIL failed -> $CACHE_DIR"
echo "Version info: $VERSION_FILE"