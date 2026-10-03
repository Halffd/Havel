#!/usr/bin/env bash
# Orphaned module plugin drift guard.
#
# The module output directory is never pruned, so a plugin whose CMake target is
# renamed or removed leaves its .so behind forever. The loader keeps probing
# those files and, because a plugin built against other VM headers carries a
# different build id, rejects them with
#
#   build id mismatch (plugin <name>, host 0x...) - stale plugin ... rebuild it
#
# which is actively misleading: the plugin is not stale, it was deleted from the
# build on purpose. Observed: `use math` logged that error on every run (the
# target had been renamed to math_native) and `use alt_tab_gui` failed outright
# instead of reporting the module as absent (commit 91d03c115 removed those GUI
# modules).
#
# This fails if any havel_mod_*.so in a module directory is not produced by a
# current add_havel_module_plugin() call. The expected set is derived from those
# call sites rather than hand-listed, so renaming a plugin in CMakeLists.txt is
# enough to keep the guard correct.
#
# Usage: scripts/check_module_plugins.sh [<module-dir> ...]
# With no arguments, checks the build tree's modules/ and out/lib/havel/modules/
# when they exist.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_FILE="${REPO_DIR}/CMakeLists.txt"

if [[ ! -f "${CMAKE_FILE}" ]]; then
    echo "FAIL: ${CMAKE_FILE} missing"
    exit 1
fi

# Expected plugin names: the first argument of every add_havel_module_plugin()
# call. Commented-out calls are excluded by requiring the call to start the line
# (after optional whitespace).
expected_names=$(
    grep -oE '^[[:space:]]*add_havel_module_plugin\([[:space:]]*[A-Za-z_][A-Za-z_0-9]*' \
        "${CMAKE_FILE}" \
    | grep -oE '[A-Za-z_][A-Za-z_0-9]*$' \
    | sort -u
)

if [[ -z "${expected_names}" ]]; then
    echo "FAIL: no add_havel_module_plugin() calls found in ${CMAKE_FILE}."
    echo "      The derivation is broken; refusing to report success."
    exit 1
fi

# Directories to scan: explicit arguments, else the conventional locations that
# exist. A directory that does not exist yet is skipped, not an error, so a
# fresh tree still passes.
if [[ $# -gt 0 ]]; then
    dirs=("$@")
else
    dirs=()
    for candidate in \
        "${REPO_DIR}/build-debug/modules" \
        "${REPO_DIR}/build-release/modules" \
        "${REPO_DIR}/out/lib/havel/modules"
    do
        [[ -d "${candidate}" ]] && dirs+=("${candidate}")
    done
fi

if [[ ${#dirs[@]} -eq 0 ]]; then
    echo "OK: no module directories present; nothing to check."
    exit 0
fi

status=0
checked=0

for dir in "${dirs[@]}"; do
    if [[ ! -d "${dir}" ]]; then
        echo "FAIL: module directory does not exist: ${dir}"
        status=1
        continue
    fi

    shopt -s nullglob
    plugins=("${dir}"/havel_mod_*.so)
    shopt -u nullglob

    for plugin in "${plugins[@]}"; do
        base="$(basename "${plugin}")"          # havel_mod_<name>.so
        name="${base#havel_mod_}"
        name="${name%.so}"
        checked=$((checked + 1))
        if ! printf '%s\n' "${expected_names}" | grep -qx -- "${name}"; then
            echo "FAIL: orphaned plugin ${plugin}"
            echo "      no add_havel_module_plugin(${name} ...) in CMakeLists.txt."
            echo "      The loader will keep probing it and report a misleading"
            echo "      build-id mismatch. Delete the file."
            status=1
        fi
    done
done

if [[ ${status} -eq 0 ]]; then
    echo "OK: ${checked} plugin(s) across ${#dirs[@]} directory(ies) all come from a current add_havel_module_plugin() call."
fi
exit ${status}
