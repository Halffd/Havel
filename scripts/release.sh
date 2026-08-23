#!/usr/bin/env bash
# Build release artifacts for Havel: .deb, .rpm, portable tarball, checksums.
# Usage: scripts/release.sh [version]
#   version: optional, e.g. v1.2.3 or 1.2.3
#            Defaults to the latest git tag, falling back to the project
#            version declared in CMakeLists.txt.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_DIR}")"
DIST_DIR="${REPO_ROOT}/dist"
BUILD_DIR="${REPO_ROOT}/build-release"

# Resolve version: argument > git tag > CMakeLists.txt project version
if [[ $# -ge 1 && -n "${1}" ]]; then
    VERSION="${1}"
elif VERSION_TAG="$(git -C "${REPO_ROOT}" describe --tags --abbrev=0 2>/dev/null)"; then
    VERSION="${VERSION_TAG}"
else
    VERSION="$(sed -n 's/^project(Havel VERSION \([^ ]*\) .*/\1/p' "${REPO_ROOT}/CMakeLists.txt")"
fi
VERSION="${VERSION#v}"

log() {
    local color=$1 msg=$2
    local NC='\033[0m'
    case "${color}" in
        green) echo -e "\033[0;32m[release]${NC} ${msg}" ;;
        blue)  echo -e "\033[0;34m[release]${NC} ${msg}" ;;
        red)   echo -e "\033[0;31m[release]${NC} ${msg}" ;;
    esac
}

log blue "Building Havel v${VERSION}..."

# Clean previous artifacts
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

# Build packages (.deb, .rpm) via the canonical packaging pipeline
# (clang + lld, PORTABLE_BUILD, stdlib bytecode precompilation)
"${SCRIPT_DIR}/../packaging/package.sh" "${BUILD_DIR}" clean

log blue "Assembling dist/..."

# Distribution packages
find "${BUILD_DIR}" -maxdepth 1 \( -name '*.deb' -o -name '*.rpm' \) \
    -exec cp {} "${DIST_DIR}/" \;

# Portable tarball: prefix layout (bin/share/lib) so it runs from anywhere,
# same structure the .deb installs to. Mirrors the Dockerfile runtime stage.
STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT
mkdir -p "${STAGE}/prefix/bin" "${STAGE}/prefix/lib/havel"
cp "${BUILD_DIR}/havel" "${STAGE}/prefix/bin/havel"
cp -r "${BUILD_DIR}/share/havel" "${STAGE}/prefix/share_tmp"
mkdir -p "${STAGE}/prefix/share"
mv "${STAGE}/prefix/share_tmp" "${STAGE}/prefix/share/havel"
if [[ -d "${BUILD_DIR}/modules" ]]; then
    cp -r "${BUILD_DIR}/modules/." "${STAGE}/prefix/lib/havel/"
fi
[[ -f "${BUILD_DIR}/libgamma_ramp.so" ]] && \
    cp "${BUILD_DIR}/libgamma_ramp.so" "${STAGE}/prefix/lib/havel/"

TARBALL="havel-${VERSION}-linux-x86_64.tar.gz"
tar -czf "${DIST_DIR}/${TARBALL}" -C "${STAGE}" prefix --transform 's|^prefix|.|'

# Standalone binary (basic execution only; use the tarball for full stdlib)
cp "${BUILD_DIR}/havel" "${DIST_DIR}/havel-linux-x86_64"

# Checksums (exclude the sums file itself)
cd "${DIST_DIR}"
find . -maxdepth 1 -type f ! -name 'SHA256SUMS' -printf '%f\n' | sort | \
    xargs sha256sum > SHA256SUMS
cd "${REPO_ROOT}"

log green "Release artifacts in dist/:"
ls -lh "${DIST_DIR}/"
