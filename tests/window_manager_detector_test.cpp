// What: CheckEnvironmentVar must treat XDG_CURRENT_DESKTOP as a
//      colon-separated list, per the XDG desktop-entry spec.
// Why: exact equality missed every multi-entry value ("ubuntu:GNOME"),
//      so all 13 desktop-environment probes in DetectOnce fell through
//      to the bare process checks and the WM resolved UNKNOWN.
// Expected: each list entry matches; single values still match;
//      non-members and substrings do not.

#include "core/window/WindowManagerDetector.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

// RAII: restores the prior value (or unsets it) so tests neither leak env
// into each other nor into the rest of the suite.
class ScopedEnv {
public:
  ScopedEnv(const char *name, const char *value) : name_(name) {
    const char *old = std::getenv(name);
    hadOld_ = old != nullptr;
    if (hadOld_)
      old_ = old;
    if (value)
      setenv(name, value, 1);
    else
      unsetenv(name);
  }
  ~ScopedEnv() {
    if (hadOld_)
      setenv(name_.c_str(), old_.c_str(), 1);
    else
      unsetenv(name_.c_str());
  }

private:
  std::string name_;
  std::string old_;
  bool hadOld_ = false;
};

static bool Matches(const char *token) {
  return WindowManagerDetector::CheckEnvironmentVar("XDG_CURRENT_DESKTOP", token);
}

TEST(WindowManagerDetectorEnv, SingleValueMatches) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "X-Cinnamon");
  EXPECT_TRUE(Matches("X-Cinnamon"));
}

TEST(WindowManagerDetectorEnv, ListFirstEntryMatches) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
  EXPECT_TRUE(Matches("ubuntu"));
  EXPECT_TRUE(Matches("GNOME"));
}

TEST(WindowManagerDetectorEnv, ListMiddleEntryMatches) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "X-Cinnamon:KDE:foo");
  EXPECT_TRUE(Matches("X-Cinnamon"));
  EXPECT_TRUE(Matches("KDE"));
}

TEST(WindowManagerDetectorEnv, ListLastEntryMatches) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
  EXPECT_TRUE(Matches("GNOME"));
}

TEST(WindowManagerDetectorEnv, NonMemberDoesNotMatch) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
  EXPECT_FALSE(Matches("X-Cinnamon"));
}

TEST(WindowManagerDetectorEnv, SubstringDoesNotMatch) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:GNOME-Fallback");
  EXPECT_FALSE(Matches("GNOME"));
}

TEST(WindowManagerDetectorEnv, TrailingSeparatorYieldsNoEmptyToken) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "ubuntu:");
  EXPECT_FALSE(Matches(""));
}

TEST(WindowManagerDetectorEnv, UnsetVariableDoesNotMatch) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", nullptr);
  EXPECT_FALSE(Matches("GNOME"));
}

TEST(WindowManagerDetectorEnv, EmptyValueDoesNotMatchNonEmpty) {
  ScopedEnv env("XDG_CURRENT_DESKTOP", "");
  EXPECT_FALSE(Matches("GNOME"));
}