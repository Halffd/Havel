// headless_sandbox_ut.cpp — the headless gate must not depend on call order.
//
// What this covers: hvtest and ctest sandbox themselves with HAVEL_HEADLESS=1 in
// the environment (script_runner sets it before exec). Both DisplayManager and IO
// used to gate on a *settable* flag only -- DisplayManager::headlessMode and
// IO::headlessMode_ -- so the sandbox depended on SetHeadlessMode() having been
// called first. IO::ensureBackend() runs under std::call_once, meaning a caller
// that touched IO before the setter ran would build a real XTest/uinput backend
// that could never be torn down again. DisplayManager::Initialize() was worse: with
// DISPLAY unset or empty it falls back to ":0", so a missed setter turned into a
// live connection to the user's session.
//
// These cases pin both gates to the environment flag so a missed setter is
// harmless. Each one sets the settable flag to the *unsandboxed* value on purpose:
// that is the ordering bug, and it is what makes these fail against the old code.
//
// Expected result: all cases pass with no X server, no input devices and no real
// screen involved -- the display must stay null and no IO backend may be built.

#include "core/display/DisplayManager.hpp"
#include "core/io/IO.hpp"
#include "utils/HeadlessRuntime.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

// RAII so one case changing the environment cannot decide the next one.
class ScopedEnv {
public:
  ScopedEnv() {
    const char *d = std::getenv("DISPLAY");
    const char *h = std::getenv("HAVEL_HEADLESS");
    hadDisplay_ = d != nullptr;
    hadHeadless_ = h != nullptr;
    display_ = d ? d : "";
    headless_ = h ? h : "";
  }
  ~ScopedEnv() {
    if (hadDisplay_)
      ::setenv("DISPLAY", display_.c_str(), 1);
    else
      ::unsetenv("DISPLAY");
    if (hadHeadless_)
      ::setenv("HAVEL_HEADLESS", headless_.c_str(), 1);
    else
      ::unsetenv("HAVEL_HEADLESS");
  }

private:
  bool hadDisplay_;
  bool hadHeadless_;
  std::string display_;
  std::string headless_;
};

} // namespace

// The regression: HAVEL_HEADLESS is set, but the settable flag says false, exactly
// as it would if something touched the display before SetHeadlessMode() ran.
// Initialize must still refuse to open anything.
TEST(HeadlessSandbox, DisplayManagerIgnoresMissingHeadlessSetter) {
  ScopedEnv guard;
  ::setenv("HAVEL_HEADLESS", "1", 1);
  // Empty rather than unset: this is what script_runner passes, and it is the
  // input that used to be rewritten to ":0" and connected to the live session.
  ::setenv("DISPLAY", "", 1);
  havel::DisplayManager::SetHeadlessMode(false);

  havel::  DisplayManager::Initialize();

  // NB: the "X11 display is invalid or corrupted" ERROR in this test's output is
  // expected -- GetDisplay() logs it whenever there is no display, which is the
  // state being asserted here. It is not a sign the display was touched.
  EXPECT_EQ(havel::DisplayManager::GetDisplay(), nullptr)
      << "opened a display despite HAVEL_HEADLESS=1 with the setter missed";
  EXPECT_FALSE(havel::DisplayManager::IsInitialized())
      << "marked itself initialized, so a later SetHeadlessMode(true) cannot retry";
}

// The same ordering hazard on the IO side, where call_once makes a mistake
// permanent. GetIOBackend() forces ensureBackend(), which is the call that used
// to build an XTest/uinput backend.
TEST(HeadlessSandbox, IoBuildsNoBackendWithoutHeadlessSetter) {
  ScopedEnv guard;
  ::setenv("HAVEL_HEADLESS", "1", 1);
  ::setenv("DISPLAY", "", 1);

  havel::IO io;
  io.SetHeadlessMode(false);

  EXPECT_EQ(io.GetIOBackend(), nullptr)
      << "IO built an output backend under HAVEL_HEADLESS=1 because the settable "
         "flag was still false -- this backend can send real XTest/uinput events "
         "and call_once means it can never be replaced";

  // And the send path itself must be a no-op rather than a real event.
  io.SendX11Key("a", true);
  io.SendX11Key("a", false);
  EXPECT_EQ(io.GetIOBackend(), nullptr)
      << "a send attempt created a backend after the fact";
}

// The env flag is what the sandbox actually relies on, so it has to be honoured
// with no flag flipped anywhere in the process.
TEST(HeadlessSandbox, EnvFlagIsAuthoritativeOnItsOwn) {
  ScopedEnv guard;
  ::setenv("HAVEL_HEADLESS", "1", 1);

  EXPECT_TRUE(havel::isHeadlessRuntime())
      << "HAVEL_HEADLESS=1 was not honoured on its own";
}
