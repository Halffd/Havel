#!/usr/bin/env bash
# Runtime ABI drift guard (TODO.md #27).
#
# The Runtime ABI (src/havel-lang/compiler/runtime/RuntimeABI.hpp) is the
# single source of truth for the havel_vm_*/havel_gc_*/havel_deoptimize
# symbol surface: backends include its declarations, and BytecodeOrcJIT
# registers every entry with the LLJIT dylib straight from the X-macro, so
# hand-maintained name lists cannot drift.
#
# This test verifies the X-macro matches the definitions actually compiled
# into the runtime: every symbol DEFINED in the runtime sources
# (BytecodeOrcJIT.cpp JIT bridge + CoreRuntimeExports.cpp AOT bridge) must
# be an ENTRY in the X-macro, and every ENTRY must be defined somewhere in
# src/ (a definition, not just a declaration). A symbol defined in the
# runtime but missing from the ABI would be unreachable from generated code;
# a listed symbol with no definition would fail to link the JIT dylib.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ABI_HPP="${REPO_DIR}/src/havel-lang/compiler/runtime/RuntimeABI.hpp"
RUNTIME_SRCS=(
    "${REPO_DIR}/src/havel-lang/compiler/BytecodeOrcJIT.cpp"
    "${REPO_DIR}/src/havel-lang/runtime/CoreRuntimeExports.cpp"
)

if [[ ! -f "${ABI_HPP}" ]]; then
    echo "FAIL: RuntimeABI.hpp missing: ${ABI_HPP}"
    exit 1
fi

status=0

# 1) Defined runtime symbols must all be ABI entries.
defined=$(grep -hoE '^(extern "C" )?[A-Za-z_][A-Za-z0-9_ *]*\**\s*\(?(havel_(vm|gc)_[a-z_0-9]+|havel_deoptimize)\)?\s*\(' \
            "${RUNTIME_SRCS[@]}" 2>/dev/null \
         | grep -oE 'havel_(vm|gc)_[a-z_0-9]+|havel_deoptimize' | sort -u)

# 2) ABI entries from the X-macro.
entries=$(grep -oE 'ENTRY\(havel_[a-z_0-9]+' "${ABI_HPP}" | sed 's/ENTRY(//' | sort -u)

missing_in_abi=$(comm -23 <(printf '%s\n' "${defined}") <(printf '%s\n' "${entries}"))
if [[ -n "${missing_in_abi}" ]]; then
    echo "FAIL: symbols defined in the runtime but missing from RuntimeABI.hpp:"
    printf '%s\n' "${missing_in_abi}" | sed 's/^/  - /'
    status=1
fi

# 3) Every ABI entry must have a definition (not just a declaration) in the
#    runtime sources: a line containing `sym(` whose statement does not end
#    in `;`. Declarations end with `);`, definitions open a body `{`.
while IFS= read -r sym; do
    def_line=$(grep -E "${sym}[[:space:]]*\(" \
         "${REPO_DIR}/src/havel-lang/compiler/BytecodeOrcJIT.cpp" \
         "${REPO_DIR}/src/havel-lang/runtime/CoreRuntimeExports.cpp" 2>/dev/null \
         | grep -vE "\)[[:space:]]*;[[:space:]]*$" | head -1)
    if [[ -z "${def_line}" ]]; then
        echo "FAIL: ABI entry ${sym} has no definition in the runtime sources."
        status=1
    fi
done < <(printf '%s\n' "${entries}")

if [[ ${status} -eq 0 ]]; then
    echo "OK: RuntimeABI.hpp ($(printf '%s\n' "${entries}" | wc -l) entries) matches runtime definitions."
fi
exit ${status}
