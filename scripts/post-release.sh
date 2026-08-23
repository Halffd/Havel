#!/usr/bin/env bash
# Post-release hook: update the homebrew tap after a release.
# Usage: scripts/post-release.sh <version>
#   version: e.g. 1.2.3 (without leading v)
# Expects the tap repository checked out next to this repo:
#   ../homebrew-havel/Formula/havel.rb

set -euo pipefail

VERSION="${1:-}"
if [[ -z "${VERSION}" ]]; then
    echo "usage: $0 <version>   (e.g. $0 1.2.3)" >&2
    exit 1
fi
VERSION="${VERSION#v}"

TAP_DIR="$(dirname "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")/homebrew-havel"
FORMULA="${TAP_DIR}/Formula/havel.rb"

if [[ ! -f "${FORMULA}" ]]; then
    echo "no homebrew tap at ${TAP_DIR}, nothing to do"
    exit 0
fi

sed -i "s/version \".*\"/version \"${VERSION}\"/" "${FORMULA}"
git -C "${TAP_DIR}" add Formula/havel.rb
git -C "${TAP_DIR}" commit -m "Update to v${VERSION}"
git -C "${TAP_DIR}" push
echo "tap updated to v${VERSION}"
