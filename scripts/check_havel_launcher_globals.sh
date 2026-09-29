#!/usr/bin/env bash
# Drift check for the generated host-function name list.
#
# Rust-like guarantee: kHostFunctionNames is GENERATED at build time from the
# actual host-function registration sites in source
# (scripts/gen_host_function_names.py -> HostFunctionNames.generated.hpp). The
# strict-mode resolver in HavelLauncher::runBuild derives its known globals
# from that generated list, so a runtime registerHostFunction("x") can never
# silently go unknown to --build/--strict.
#
# This test verifies the checked-in HostFunctionNames.generated.hpp matches
# exactly what the generator would produce from the current source. It catches
# a stale checked-in copy (e.g. when a new host function was registered but
# the generated file was not regenerated/committed). Drift between source
# registrations and resolution is structurally impossible because both share
# the generated list.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEN_HPP="${REPO_DIR}/src/havel-lang/compiler/core/HostFunctionNames.generated.hpp"
LAUNCHER="${REPO_DIR}/src/core/init/HavelLauncher.cpp"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found; cannot verify generated host-function header."
    exit 0
fi

if [[ ! -f "${GEN_HPP}" ]]; then
    echo "ERROR: ${GEN_HPP} not found. Run scripts/gen_host_function_names.py to generate it." >&2
    exit 1
fi

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT

if ! python3 "${REPO_DIR}/scripts/gen_host_function_names.py" "${REPO_DIR}/src" "${tmp}" >/dev/null 2>&1; then
    echo "ERROR: host-function-name generator failed." >&2
    exit 1
fi

# Extract just the kHostFunctionNames array body from both files for
# comparison, ignoring incidental header/comment differences.
extract_array() {
    awk '/kHostFunctionNames\[\] = \{/,/^\};/' "$1" \
        | grep -oE '"[A-Za-z_][A-Za-z0-9_.]*"' \
        | tr -d '"' \
        | sort -u
}

gen_set="$(extract_array "${tmp}")"
hpp_set="$(extract_array "${GEN_HPP}")"

if [[ "${gen_set}" != "${hpp_set}" ]]; then
    echo "FAIL: HostFunctionNames.generated.hpp is stale." >&2
    echo "      The checked-in header no longer matches what the generator" >&2
    echo "      produces from the current registration sites in src/." >&2
    echo "      Run: python3 scripts/gen_host_function_names.py src src/havel-lang/compiler/core/HostFunctionNames.generated.hpp" >&2
    echo "      and commit the refreshed header." >&2
    echo "--- Generated (fresh) vs checked-in (current) ---" >&2
    diff <(printf '%s\n' "${gen_set}") <(printf '%s\n' "${hpp_set}") >&2 || true
    exit 1
fi

# The runBuild path must derive its known globals from the generated list,
# not from a hand-maintained copy.
if ! grep -q "kHostFunctionNames" "${LAUNCHER}"; then
    echo "FAIL: HavelLauncher::runBuild no longer seeds known globals from" >&2
    echo "      kHostFunctionNames; strict --build resolution would drift from" >&2
    echo "      the real registration sites again." >&2
    exit 1
fi

echo "OK: HostFunctionNames.generated.hpp ($(printf '%s\n' "${hpp_set}" | wc -l | tr -d ' ') names) matches generated source."
exit 0
