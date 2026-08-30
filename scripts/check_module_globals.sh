#!/usr/bin/env bash
# Module globals drift guard.
#
# Prevents a real module global being registered via setGlobal() at runtime
# without also being known to strict-mode resolution in the VM-less bytecode
# build path (runBuild) and the pipeline fallback.
#
# The canonical list lives in src/havel-lang/compiler/core/ModuleGlobals.hpp
# (kModuleGlobals). If a developer registers a new module global via
# setGlobal() and forgets to add it there, this test fails.
#
# Names that are deliberately NOT in kModuleGlobals:
#   - VM-core internal type/constructor objects (class, struct, prot, load,
#     scheduler, None, Object, object, Option, newEnum, getVariant) and numeric
#     constants (E, INF, NAN, PI, RAND_MAX): handled by runBuild's keyword list.
#   - Names with an internal prefix (_G, __export_*, decimal-prefixed, $...):
#     runtime internals, not script-visible globals.
#   - Dotted member names (thread.spawn, random.choice, fs.read, ...): the first
#     segment is the actual global and must itself be covered; the member is not
#     a top-level global.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
HPP="${REPO_DIR}/src/havel-lang/compiler/core/ModuleGlobals.hpp"

if [[ ! -f "${HPP}" ]]; then
    echo "ERROR: ${HPP} not found" >&2
    exit 1
fi

# Names that must exist in kModuleGlobals OR be a known VM-core internal.
# The VM-core internal set is intentionally small and stable.
INTERNAL="
class Class struct Struct prot load scheduler None Object object Option
newEnum getVariant E INF NAN PI RAND_MAX
"

# Extract the canonical list from kModuleGlobals in ModuleGlobals.hpp.
canonical() {
    awk '/kModuleGlobals\[\] = \{/,/^\};/' "${HPP}" \
        | grep -oE '"[A-Za-z_][A-Za-z0-9_.]*"' \
        | tr -d '"' \
        | sort -u
}

# Every setGlobal("name", ...) first-segment prefix across the source tree.
registered() {
    grep -rhoE 'setGlobal\("[A-Za-z_][A-Za-z0-9_.]*"' "${REPO_DIR}/src" \
        | sed -E 's/.*"([A-Za-z_][A-Za-z0-9_.]*)"$/\1/' \
        | cut -d. -f1 \
        | grep -vE '^_G$|^__export_|^\$' \
        | sort -u
}

canonical_set="$(canonical)"
internal_set="$(printf '%s' "${INTERNAL}" | tr '[:space:]' '\n' | sed '/^$/d' | sort -u)"

failed=0

for name in $(registered); do
    # Skip VM-core internals.
    if printf '%s\n' "${internal_set}" | grep -qx "${name}"; then
        continue
    fi
    # Skip names already canonical.
    if printf '%s\n' "${canonical_set}" | grep -qx "${name}"; then
        continue
    fi
    echo "MISSING: setGlobal(\"${name}\"...) is registered but not in kModuleGlobals (ModuleGlobals.hpp)." >&2
    echo "         Add it to kModuleGlobals so strict-mode --build/--full-aot resolution accepts it." >&2
    failed=1
done

if [[ "${failed}" -ne 0 ]]; then
    echo "FAIL: module globals drift guard found uncovered setGlobal() globals." >&2
    exit 1
fi

echo "OK: all setGlobal() module globals are covered by kModuleGlobals."
exit 0
