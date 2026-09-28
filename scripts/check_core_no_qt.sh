#!/usr/bin/env bash
# Qt leak guard for the embeddable core.
#
# libhavel_core.a and libhavel_lang.a are embedded by Qt-free hosts (havel-wm,
# the cranelift AOT shim, the AOT runtime). Those archives must not need Qt to
# link: a single Qt symbol reference in them forces every embedder to link the
# whole Qt stack, and a single Qt header in their include closure couples the
# core build to Qt being installed.
#
# The bridge layer already reaches Qt exclusively through explicit
# registration slots (src/host/module/BridgeSelection.hpp), so this guard has
# three parts:
#
#   1. Source gate: no core-facing bridge TU may include a Qt header, include
#      qt.hpp, or branch on HAVE_QT_EXTENSION.
#   2. Symbol gate: libhavel_lang_core.a and libhavel_lang.a must contain zero
#      undefined Qt symbols.
#   3. Symbol gate with a known-leaks ledger for libhavel_core.a: the service
#      layer has not been extracted yet, so the exact set of Qt-touching
#      objects is pinned here. The ledger is drift-checked in both directions:
#      a NEW Qt reference fails, and a listed object that no longer references
#      Qt also fails (so the ledger cannot rot into a blanket allowlist).
#      Each entry shrinks as the extraction lands; the file should end with an
#      empty ledger and no part 3.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# Absolutize: the per-object scan extracts archives inside a temp dir, so a
# relative build path would not resolve there.
BUILD_DIR="$(cd "${1:-${REPO_DIR}/build-debug}" 2>/dev/null && pwd)" \
    || { echo "FAIL: build dir '${1:-${REPO_DIR}/build-debug}' not found"; exit 1; }

BRIDGE_DIR="${REPO_DIR}/src/host/module/bridges"

# Qt symbols as they appear in demangled nm output. Matches Qt class
# constructors/methods (QString::, QWidget::), Qt meta-object machinery
# (QObject, QMetaObject, moc_), and namespace-qualified Qt enums.
QT_SYM_RE='(^|[^A-Za-z0-9_])(Q[A-Z][A-Za-z0-9_]*::|QObject|QMetaObject|QVariant|QString|QByteArray|moc_[A-Za-z0-9_]+|Qt::)'

# havel_core objects still known to reference Qt (service layer, not yet
# extracted). Keep sorted; see part 3 above.
# No known leaks left: every Qt implementation that used to sit in havel_core
# (clipboard, pixel automation, UI backends, services) has been extracted into
# havel_gui. The ledger is empty, so part 3 asserts the archive is Qt-free.
CORE_KNOWN_QT_LEAKS=""

fail=0

echo "== part 1: core-facing bridge sources must be Qt-free =="

# qt/QtBridge.cpp is the optional Qt half of the bridges; it is compiled into
# havel_gui, never into havel_core, so it is the one allowed exception.
offenders="$(grep -rlE '#include[[:space:]]*[<"](qt\.hpp|Q[A-Z][A-Za-z0-9_]*)' \
    --include='*.cpp' --include='*.hpp' \
    "${BRIDGE_DIR}" 2>/dev/null | grep -v '/bridges/qt/' || true)"

if [ -n "${offenders}" ]; then
    echo "FAIL: Qt header included from a core-facing bridge TU:"
    echo "${offenders}" | sed 's/^/  /'
    fail=1
else
    echo "ok: no Qt headers outside bridges/qt/"
fi

# HAVE_QT_EXTENSION inside a core-facing TU compiles one flavour and leaves the
# core's behaviour dependent on which GUI backend the *host* was built with.
guarded="$(grep -rl 'HAVE_QT_EXTENSION' --include='*.cpp' --include='*.hpp' \
    "${BRIDGE_DIR}" 2>/dev/null | grep -v '/bridges/qt/' || true)"

if [ -n "${guarded}" ]; then
    echo "FAIL: HAVE_QT_EXTENSION conditional in a core-facing bridge TU:"
    echo "${guarded}" | sed 's/^/  /'
    echo "  Use an explicit registration slot (BridgeSelection.hpp) instead."
    fail=1
else
    echo "ok: no HAVE_QT_EXTENSION conditionals outside bridges/qt/"
fi

archive_undefined_qt_symbols() {
    # Undefined (referenced, not provided) Qt symbols in a static archive.
    nm -C --undefined-only "$1" 2>/dev/null | sed 's/^ *U //' | sort -u \
        | grep -E "${QT_SYM_RE}" || true
}

objects_with_qt() {
    # Names of archive members that reference at least one Qt symbol, one per
    # line, sorted.
    local archive="$1" tmp obj
    local -a found=()
    tmp="$(mktemp -d)"
    if ! ( cd "${tmp}" && ar x "${archive}" ) 2>/dev/null; then
        rm -rf "${tmp}"
        return 1
    fi
    for obj in "${tmp}"/*.o; do
        [ -e "${obj}" ] || continue
        if nm -C --undefined-only "${obj}" 2>/dev/null | grep -qE "${QT_SYM_RE}"; then
            found+=("$(basename "${obj}")")
        fi
    done
    rm -rf "${tmp}"
    printf '%s\n' "${found[@]+"${found[@]}"}" | sed '/^$/d' | sort
}

check_archive_zero() {
    local label="$1" archive="${BUILD_DIR}/$2" syms
    [ -f "${archive}" ] || { echo "FAIL: ${label}: ${archive} not built"; fail=1; return; }
    syms="$(archive_undefined_qt_symbols "${archive}")"
    if [ -n "${syms}" ]; then
        echo "FAIL: ${label} references Qt (${archive}):"
        echo "${syms}" | head -20 | sed 's/^/  /'
        echo "  (move the implementation into the optional GUI target)"
        fail=1
    else
        echo "ok: ${label} has no undefined Qt symbols"
    fi
}

echo "== part 2: language archives must be Qt-free =="
if [ -f "${BUILD_DIR}/libhavel_lang_core.a" ]; then
    check_archive_zero "libhavel_lang_core.a" "libhavel_lang_core.a"
    check_archive_zero "libhavel_lang.a" "libhavel_lang.a"
else
    echo "SKIP: ${BUILD_DIR}/libhavel_lang_core.a not built"
fi

echo "== part 3: havel_core.a known-leaks ledger =="
if [ ! -f "${BUILD_DIR}/libhavel_core.a" ]; then
    echo "SKIP: ${BUILD_DIR}/libhavel_core.a not built"
elif [ -z "${CORE_KNOWN_QT_LEAKS}" ]; then
    check_archive_zero "libhavel_core.a" "libhavel_core.a"
else
    actual="$(objects_with_qt "${BUILD_DIR}/libhavel_core.a")"
    if [ "${actual}" = "${CORE_KNOWN_QT_LEAKS}" ]; then
        echo "ok: havel_core.a Qt references match the pinned ledger exactly:"
        echo "${actual}" | sed 's/^/  /'
    else
        echo "FAIL: havel_core.a Qt references drifted from the pinned ledger."
        echo "  newly referencing Qt (extract them, or extend the ledger deliberately):"
        comm -13 <(printf '%s\n' "${CORE_KNOWN_QT_LEAKS}") \
                 <(printf '%s\n' "${actual}") | sed 's/^/    + /'
        echo "  listed but no longer referencing Qt (remove from the ledger):"
        comm -23 <(printf '%s\n' "${CORE_KNOWN_QT_LEAKS}") \
                 <(printf '%s\n' "${actual}") | sed 's/^/    - /'
        fail=1
    fi
fi

if [ "${fail}" -ne 0 ]; then
    echo ""
    echo "Qt leak guard: FAILED"
    exit 1
fi

echo ""
echo "Qt leak guard: passed"
