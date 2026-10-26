/*
 * LsanSuppressions.cpp - built-in LeakSanitizer suppressions
 *
 * fontconfig keeps a process-wide global font configuration (FcConfig)
 * populated on first font lookup. It is only released by FcFini(), which
 * Havel never calls because the Qt font database would touch freed
 * fontconfig data during process teardown.
 *
 * The fontconfig cache becomes unreachable because Havel intentionally
 * leaks lazily-created QGuiApplication instances: destroying them during
 * exit() teardown races Qt's own statics and the QXcbEventQueue thread
 * and crashes in ~QGuiApplication (see QtScreenshotBackend.hpp,
 * UIService::ensureApp). The leaked QGuiApplication keeps the font
 * database alive until process exit, where the OS reclaims everything.
 *
 * Suppressing the resulting report is correct: the allocation has
 * process lifetime by design, and freeing it would reintroduce an
 * exit-time crash.
 *
 * No preprocessor guard: the symbol is only invoked by LSan (which
 * only exists in ASAN builds).  In non-ASAN builds it simply sits
 * in the binary unused.
 */

#include "havel-lang/common/Export.hpp"

extern "C" HAVEL_EXPORT const char *__lsan_default_suppressions() {
    // Process-global fontconfig config cache, never freed by design.
    //
    // CPython's import machinery (import_find_and_load and below) makes a
    // handful of permanent allocations on the FIRST module import that
    // survive Py_FinalizeEx - measured here as 2289 bytes / 3 allocations on
    // test_python_bridge (stack: pyImport -> PyImport_ImportModule ->
    // PyImport_Import -> import_find_and_load -> PyObject_CallMethodObjArgs),
    // while py.eval-only usage of the same bridge runs clean. These are
    // CPython-internal allocations, not bridge objects: the bridge's own
    // reference discipline is balanced and shutdownPython drains the handle
    // registry before Py_FinalizeEx. Suppressing by the import-machinery
    // frame name is deliberately narrow - it hides nothing allocated in
    // havel frames, so a genuine bridge leak still fails the suite.
    return "leak:libfontconfig.so\n"
           "leak:import_find_and_load\n";
}
