// havel::X11IOBackend::Cleanup - the exit-cleanup ordering that killed the
// process in _XReply <- XSync <- X11IOBackend::Cleanup (coredump
// 3006494, 2026-11-27).
//
// The defect this locks down: exit cleanup runs DisplayManager::Close()
// (XCloseDisplay, display = nullptr) BEFORE IO::cleanup() reaches
// X11IOBackend::Cleanup(). The backend still held the now-freed Display*
// and called XSync through it - a use-after-close inside _XReply that took
// the whole REPL down with SIGSEGV.
//
// The fix: Cleanup only touches the display while DisplayManager still
// owns a live one (IsInitialized() == display != nullptr). This test runs
// the post-close state - a fresh process where DisplayManager owns no
// display, exactly what IsInitialized() reports after Close() - with a
// deliberately bogus Display* in the backend. The pre-fix code XSynced
// that pointer and faulted; the guard makes Cleanup a no-op.
//
// No X server, no connection: the bogus pointer must never be dereferenced,
// and ASAN/UBSAN on debug builds turn any deref into a loud failure.

#include <gtest/gtest.h>

#include "core/display/DisplayManager.hpp"
#include "core/io/X11IOBackend.hpp"

namespace {

// A recognizable non-null garbage pointer: the shape of a stale Display*
// after XCloseDisplay freed the object.
void *kStaleDisplay = reinterpret_cast<void *>(0x0000BEEF0000BEEFULL);

TEST(X11ExitCleanupOrder, CleanupSkipsDisplayTheManagerNoLongerOwns) {
  // Post-Close() state: DisplayManager::IsInitialized() is false.
  ASSERT_FALSE(havel::DisplayManager::IsInitialized());

  havel::X11IOBackend backend(static_cast<Display *>(kStaleDisplay));
  ASSERT_TRUE(backend.IsAvailable());

  // The exact call the core shows: Cleanup during exit, display gone.
  // Pre-fix: XSync(kStaleDisplay) -> SIGSEGV in _XReply.
  EXPECT_NO_FATAL_FAILURE(backend.Cleanup());

  // The guard leaves the stale pointer alone (it does not null it while
  // the manager owns nothing) and never issues X protocol traffic.
  EXPECT_TRUE(backend.IsAvailable());
}

TEST(X11ExitCleanupOrder, CleanupIsIdempotentAcrossDestructor) {
  // Exit cleanup and the destructor both call Cleanup(); the second call
  // must stay equally harmless.
  ASSERT_FALSE(havel::DisplayManager::IsInitialized());
  {
    havel::X11IOBackend backend(static_cast<Display *>(kStaleDisplay));
    backend.Cleanup();
  } // destructor runs Cleanup again
  SUCCEED();
}

} // namespace
