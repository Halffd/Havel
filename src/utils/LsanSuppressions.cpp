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

extern "C" __attribute__((visibility("default"))) const char *__lsan_default_suppressions() {
    // Process-global fontconfig config cache, never freed by design.
    return "leak:libfontconfig.so\n";
}
