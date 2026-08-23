#!/usr/bin/env bash
# Build and package Havel as .deb and .rpm
# Usage: ./packaging/package.sh [build_dir] [clean]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${1:-${REPO_ROOT}/build-release}"
CLEAN="${2:-}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log() {
    local level=$1
    local message=$2
    local color=$3
    echo -e "${color}[${level}]${NC} ${message}"
}

# Clean if requested
if [[ "${CLEAN}" == "clean" ]]; then
    log "INFO" "Cleaning ${BUILD_DIR}..." "${YELLOW}"
    rm -rf "${BUILD_DIR}"
fi

# Configure and build
log "INFO" "Configuring CMake in ${BUILD_DIR}..." "${BLUE}"
mkdir -p "${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -S "${REPO_ROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPORTABLE_BUILD=ON \
    -DENABLE_LLVM=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_HAVEL_LANG=ON \
    -DENABLE_MODULE_PLUGINS=ON \
    -DENABLE_QT=OFF \
    -DENABLE_QT_UI_BACKEND=OFF \
    -DENABLE_GTK=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld" \
    -DCMAKE_MODULE_LINKER_FLAGS="-fuse-ld=lld" \
    -Wno-dev

log "INFO" "Building havel..." "${BLUE}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

log "INFO" "Precompiling stdlib bytecode..." "${BLUE}"
cmake --build "${BUILD_DIR}" --target compile-stdlib-bytecode

log "INFO" "Building DEB package..." "${BLUE}"
(cd "${BUILD_DIR}" && cpack -G DEB)

log "INFO" "Building RPM package..." "${BLUE}"
(cd "${BUILD_DIR}" && cpack -G RPM)

# Find and report packages
log "SUCCESS" "Packages built:" "${GREEN}"
find "${BUILD_DIR}" -name '*.deb' -o -name '*.rpm' | while read -r pkg; do
    log "INFO" "  ${pkg} ($(du -h "${pkg}" | cut -f1))" "${GREEN}"
done