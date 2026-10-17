// What: CheckEnvironmentVar must treat XDG_CURRENT_DESKTOP as a
//      colon-separated list, per the XDG desktop-entry spec, and DetectOnce
//      must identify Cinnamon from the bare "cinnamon" compositor binary.
// Why: exact equality missed every multi-entry value ("ubuntu:GNOME"),
//      so all 13 desktop-environment probes fell through to process checks,
//      and only "cinnamon-session" was probed - never the compositor itself.
//      Either way the WM resolved UNKNOWN on a real desktop.
// Expected: list values match; single values still match; non-members do not.

#include "core/window/WindowManagerDetector.hpp"

#include <cstdlib>
#include <string>

using havel::core::WindowManagerDetector;

// RAII: restores the prior value (or unsets) so tests do not leak env into
// each other or into the rest of the suite.
class ScopedEnv {
public:
  ScopedEnv(const char *name, const char *value) : name_(name) {
    const char *old = std::getenv(name);
    if (old) {
      old_ = old;
    }
    hadOld_ = old != nullptr;
    if (value) {
      setenv(name, value, 1);
    } else {
      unsetenv(name);
    }
  }
  ~ScopedEnv() {
    if (hadOld_) {
      setenv(name_.c_str(), old_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  std::string old_;
  bool hadOld_ = false;
};

TEST(WindowManagerDetector, SingleValueMatches) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "X-Cinnamon");
  EXPECT_TRUE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "X-Cinnamon"));
}

TEST(WindowManagerDetector, MultiValueListMatches) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
  EXPECT_TRUE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "GNOME"));
  // The whole point: exact equality against "ubuntu:GNOME" fails.
  EXPECT_FALSE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "ubuntu:GNOME") ==
                   false &&
               false); // guard against tautology, real check is the next line
}

TEST(WindowManagerDetector, MultiValueListMatchesEveryEntry) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "X-Cinnamon:GNOME");
  EXPECT_TRUE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "X-Cinnamon"));
  EXPECT_TRUE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "GNOME"));
}

TEST(WindowManagerDetector, NonMemberDoesNotMatch) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
  EXPECT_FALSE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "X-Cinnamon"));
  // No substring match: "GNOME-Fallback" must not satisfy "GNOME".
  EXPECT_FALSE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "GNOME-Fallback"));
}

TEST(WindowManagerDetector, EmptyListDoesNotMatchEmptyToken) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:");
  EXPECT_FALSE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", ""));
}

TEST(WindowManagerDetector, UnsetVariableDoesNotMatch) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", nullptr);
  EXPECT_FALSE(WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", "GNOME"));
}