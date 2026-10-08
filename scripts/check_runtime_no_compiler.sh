#!/usr/bin/env bash
# Runtime/compiler boundary guard.
#
# libhavel_runtime.a is the archive a runtime-only embedder links: it has to
# stand on its own, without the compiler. The archive split is enforced at
# configure time (a lost source fails the build), but nothing stopped a
# runtime translation unit from calling into the compiler afterwards - a
# single runBytecodePipeline reference in havel_state.cpp kept
# libhavel_compiler.a required for every runtime consumer, silently, because
# the full build links both and never notices.
#
# This guard has three parts:
#
#   1. Source gate: no runtime-partition TU may include a compiler-only
#      header. Catches the leak before it becomes a link error.
#   2. Symbol gate: every symbol libhavel_runtime.a references must be
#      resolvable without libhavel_compiler.a. Implemented as "no symbol
#      undefined in the runtime archive is defined in the compiler archive".
#   3. Detector self-check: libhavel_lang_core.a, which really does reference
#      the compiler, must FAIL part 2. Without this the guard could pass
#      vacuously - e.g. if the nm invocation silently produced no output and
#      an empty intersection trivially matched.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="$(cd "${1:-${REPO_DIR}/build-headless}" 2>/dev/null && pwd)" \
    || { echo "FAIL: build dir '${1:-${REPO_DIR}/build-headless}' not found"; exit 1; }

RUNTIME_ARCHIVE="${BUILD_DIR}/libhavel_runtime.a"
COMPILER_ARCHIVE="${BUILD_DIR}/libhavel_compiler.a"
# Compatibility archive that still holds the whole source set; used only as the
# negative control in part 3.
LEGACY_ARCHIVE="${BUILD_DIR}/libhavel_lang_core.a"

fail=0

# Mirrors HAVEL_RUNTIME_SOURCE_PATTERNS in CMakeLists.txt. Directories holding
# bytecode-execution sources that live under compiler/, plus the genuinely
# runtime layers. Keep in sync deliberately: a new directory is either runtime
# or it is compiler, and this list must say which.
RUNTIME_SOURCE_DIRS="
src/havel-lang/compiler/vm
src/havel-lang/compiler/gc
src/havel-lang/compiler/runtime
src/havel-lang/compiler/module
src/havel-lang/compiler/prototypes
src/havel-lang/runtime
src/havel-lang/stdlib
src/havel-lang/lexer
src/havel-lang/core
src/havel-lang/ffi
src/havel-lang/capi
"
RUNTIME_SINGLE_FILES="src/havel-lang/compiler/core/RuntimeProfiler.cpp"

# Headers that only exist to reach the frontend. BytecodeIR.hpp is deliberately
# absent: the runtime VM owns BytecodeChunk and its methods are inline.
COMPILER_ONLY_INCLUDE_RE='#include[[:space:]]*[<"]([^">]*/)?(Pipeline|ByteCompiler|Parser)\.h(pp)?[>"]|#include[[:space:]]*[<"][^">]*/semantic/'

echo "== part 1: runtime-partition sources must not include compiler headers =="

offenders=""
for dir in ${RUNTIME_SOURCE_DIRS}; do
    [ -d "${REPO_DIR}/${dir}" ] || continue
    # Headers included by a runtime TU matter too: the leak is a transitive
    # reference, and the frontend reaches the runtime through headers just as
    # easily as through .cpp files.
    found="$(grep -rlE "${COMPILER_ONLY_INCLUDE_RE}" \
        --include='*.cpp' --include='*.hpp' --include='*.h' \
        "${REPO_DIR}/${dir}" 2>/dev/null || true)"
    offenders="${offenders}${found}"$'\n'
done
for file in ${RUNTIME_SINGLE_FILES}; do
    [ -f "${REPO_DIR}/${file}" ] || continue
    if grep -qE "${COMPILER_ONLY_INCLUDE_RE}" "${REPO_DIR}/${file}" 2>/dev/null; then
        offenders="${offenders}${REPO_DIR}/${file}"$'\n'
    fi
done
offenders="$(printf '%s' "${offenders}" | sed '/^$/d' | sort -u)"

if [ -n "${offenders}" ]; then
    echo "FAIL: runtime-partition source includes a compiler-only header:"
    printf '%s\n' "${offenders}" | sed "s|^${REPO_DIR}/|  |"
    echo "  (call it through ModuleCompilerHook instead)"
    fail=1
else
    echo "ok: no compiler-only includes in the runtime partition"
fi

# Undefined symbols of an archive, mangled, one per line.
archive_undefined() {
    nm --undefined-only "$1" 2>/dev/null | awk '{ $1=""; sub(/^ +/, ""); print }' | sort -u
}

# Symbols an archive defines (global and weak text), mangled, one per line.
archive_defined() {
    nm --defined-only "$1" 2>/dev/null | awk '{ $1=""; $2=""; sub(/^ ++/, ""); print }' \
        | grep -v '^$' | sort -u
}

# Symbols archive A needs that only archive B can provide.
leaks_between() {
    local needing="$1" provider="$2" tmp_need tmp_prov
    tmp_need="$(mktemp)"
    tmp_prov="$(mktemp)"
    archive_undefined "${needing}" > "${tmp_need}"
    archive_defined "${provider}" > "${tmp_prov}"
    if [ ! -s "${tmp_need}" ] || [ ! -s "${tmp_prov}" ]; then
        rm -f "${tmp_need}" "${tmp_prov}"
        return 2 # not enough data to decide; caller must fail
    fi
    comm -12 "${tmp_need}" "${tmp_prov}"
    rm -f "${tmp_need}" "${tmp_prov}"
}

echo "== part 2: libhavel_runtime.a must not need libhavel_compiler.a =="

if [ ! -f "${RUNTIME_ARCHIVE}" ]; then
    echo "FAIL: ${RUNTIME_ARCHIVE} not built"
    fail=1
elif [ ! -f "${COMPILER_ARCHIVE}" ]; then
    echo "FAIL: ${COMPILER_ARCHIVE} not built"
    fail=1
else
    leaks="$(leaks_between "${RUNTIME_ARCHIVE}" "${COMPILER_ARCHIVE}")"
    rc=$?
    if [ "${rc}" -ne 0 ]; then
        echo "FAIL: could not compare the archives (nm produced no symbols)"
        fail=1
    elif [ -n "${leaks}" ]; then
        echo "FAIL: libhavel_runtime.a references symbols only the compiler provides:"
        printf '%s\n' "${leaks}" | head -30 | sed 's/^/  /'
        echo "  (route it through ModuleCompilerHook, or move the caller into the compiler archive)"
        fail=1
    else
        echo "ok: libhavel_runtime.a resolves without libhavel_compiler.a"
    fi
fi

echo "== part 3: the leak detector must detect a real leak =="

if [ ! -f "${LEGACY_ARCHIVE}" ] || [ ! -f "${COMPILER_ARCHIVE}" ]; then
    echo "FAIL: ${LEGACY_ARCHIVE} not built, cannot self-check the detector"
    fail=1
else
    control="$(leaks_between "${LEGACY_ARCHIVE}" "${COMPILER_ARCHIVE}")"
    rc=$?
    if [ "${rc}" -ne 0 ]; then
        echo "FAIL: self-check could not read symbols (nm produced no output)"
        fail=1
    elif [ -z "${control}" ]; then
        echo "FAIL: libhavel_lang_core.a shows no compiler dependency, so part 2"
        echo "  proves nothing - the comparison cannot be trusted to find leaks."
        fail=1
    else
        echo "ok: detector finds $(printf '%s\n' "${control}" | wc -l) compiler symbols"
        echo "     referenced by libhavel_lang_core.a (expected: it still bundles the compiler)"
    fi
fi

if [ "${fail}" -ne 0 ]; then
    echo ""
    echo "runtime/compiler boundary guard: FAILED"
    exit 1
fi

echo ""
echo "runtime/compiler boundary guard: passed"