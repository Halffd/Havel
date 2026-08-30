#!/usr/bin/env bash
# Module globals drift guard.
#
# Rust-like guarantee: the canonical module-global list (kModuleGlobals) is
# GENERATED at build time from the actual setGlobal("name") declarations in
# source (scripts/gen_module_globals.py -> ModuleGlobals.generated.hpp). The
# compiler's strict-mode resolver derives its known globals from that generated
# list, so a runtime setGlobal("x") can never silently go unknown to
# --build/--full-aot.
#
# This test verifies the checked-in ModuleGlobals.generated.hpp matches exactly
# what the generator would produce from the current source. It catches a stale
# checked-in copy (e.g. when source changed but the generated file was not
# regenerated/committed). Drift between source declarations and resolution is
# structurally impossible because both share the generated list.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEN="../scripts/gen_module_globals.py"
GEN_HPP="${REPO_DIR}/src/havel-lang/compiler/core/ModuleGlobals.generated.hpp"

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 not found; cannot verify generated module-global header."
    echo "      (The checked-in header is what compilation uses.)"
    exit 0
fi

if [[ ! -f "${GEN_HPP}" ]]; then
    echo "ERROR: ${GEN_HPP} not found. Run scripts/gen_module_globals.py to generate it." >&2
    exit 1
fi

tmp="$(mktemp)"
trap 'rm -f "${tmp}"' EXIT

if ! python3 "${REPO_DIR}/scripts/gen_module_globals.py" "${REPO_DIR}/src" "${tmp}" >/dev/null 2>&1; then
    echo "ERROR: module-globals generator failed." >&2
    exit 1
fi

# Extract just the kModuleGlobals array body from both files for comparison,
# ignoring incidental header/comment differences.
extract_array() {
    awk '/kModuleGlobals\[\] = \{/,/^\};/' "$1" \
        | grep -oE '"[A-Za-z_][A-Za-z0-9_.]*"' \
        | tr -d '"' \
        | sort -u
}

gen_set="$(extract_array "${tmp}")"
hpp_set="$(extract_array "${GEN_HPP}")"

if [[ "${gen_set}" != "${hpp_set}" ]]; then
    echo "FAIL: ModuleGlobals.generated.hpp is stale." >&2
    echo "      The checked-in header no longer matches what the generator" >&2
    echo "      produces from the current setGlobal() declarations in src/." >&2
    echo "      Run: python3 scripts/gen_module_globals.py src src/havel-lang/compiler/core/ModuleGlobals.generated.hpp" >&2
    echo "      and commit the refreshed header." >&2
    echo "--- Generated (fresh) vs checked-in (current) ---" >&2
    echo "${gen_set}" >&2
    echo "---" >&2
    echo "${hpp_set}" >&2
    exit 1
fi

echo "OK: ModuleGlobals.generated.hpp ($(printf '%s\n' "${hpp_set}" | sed '/^$/d' | wc -l | tr -d ' ') names) matches generated source."
exit 0
