// IO::selectGrabsToRetry - the rule that decides which hotkeys get their
// OS-level key grab re-attempted.
//
// Background: an X11 key grab can fail with BadAccess (code 10) when
// another X client owns that key/modifier combination, or holds an active
// keyboard grab. That is what a long-running child process (a game,
// xdotool, a screencaster, an IME) does to the user's box.
//
// Before the fix, IO::GrabHotkey computed the grab result and threw it
// away, so a failed grab was reported as success: the binding looked
// armed, hotkey.list() reported grab=true, but the X server held no grab
// for the key and the hotkey never fired. Nothing ever retried it.
//
// The retry has to pick exactly the right set:
//   - bindings that want a grab but do not have one,
//   - and nothing else. Re-grabbing a binding the script disabled would
//     arm a hotkey that must stay silent; re-grabbing an evdev binding
//     would issue a pointless XGrabKey for a key the evdev path owns.
#include "core/io/IO.hpp"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <unordered_map>

using havel::HotKey;
using havel::IO;

namespace {

HotKey makeHotkey(bool enabled, bool evdev, int key) {
  HotKey hk;
  hk.enabled = enabled;
  hk.evdev = evdev;
  hk.key = key;
  hk.alias = "test";
  return hk;
}

} // namespace

TEST(SelectGrabsToRetry, BindingWithoutAnOsGrabIsSelected) {
  std::set<int> desired{7};
  std::set<int> actual; // the grab never succeeded
  std::unordered_map<int, HotKey> hotkeys{{7, makeHotkey(true, false, 70)}};

  auto pending = IO::selectGrabsToRetry(desired, actual, hotkeys);
  ASSERT_EQ(pending.size(), 1u);
  EXPECT_EQ(pending[0], 7);
}

TEST(SelectGrabsToRetry, BindingThatAlreadyHasAnOsGrabIsNotSelected) {
  std::set<int> desired{7};
  std::set<int> actual{7};
  std::unordered_map<int, HotKey> hotkeys{{7, makeHotkey(true, false, 70)}};

  EXPECT_TRUE(IO::selectGrabsToRetry(desired, actual, hotkeys).empty());
}

TEST(SelectGrabsToRetry, DisabledBindingIsNotSelected) {
  // The script turned the binding off: re-grabbing it would arm a hotkey
  // that must stay silent.
  std::set<int> desired{7};
  std::set<int> actual;
  std::unordered_map<int, HotKey> hotkeys{{7, makeHotkey(false, false, 70)}};

  EXPECT_TRUE(IO::selectGrabsToRetry(desired, actual, hotkeys).empty());
}

TEST(SelectGrabsToRetry, EvdevBindingIsNotSelected) {
  // evdev hotkeys have no X11 grab; the evdev device owns them.
  std::set<int> desired{7};
  std::set<int> actual;
  std::unordered_map<int, HotKey> hotkeys{{7, makeHotkey(true, true, 70)}};

  EXPECT_TRUE(IO::selectGrabsToRetry(desired, actual, hotkeys).empty());
}

TEST(SelectGrabsToRetry, BindingWithoutAKeycodeIsNotSelected) {
  std::set<int> desired{7};
  std::set<int> actual;
  std::unordered_map<int, HotKey> hotkeys{{7, makeHotkey(true, false, 0)}};

  EXPECT_TRUE(IO::selectGrabsToRetry(desired, actual, hotkeys).empty());
}

TEST(SelectGrabsToRetry, UnknownBindingIdIsNotSelected) {
  // The binding was removed between the grab request and the retry.
  std::set<int> desired{7};
  std::set<int> actual;
  std::unordered_map<int, HotKey> hotkeys;

  EXPECT_TRUE(IO::selectGrabsToRetry(desired, actual, hotkeys).empty());
}

TEST(SelectGrabsToRetry, OnlyTheFailedBindingIsSelectedAmongMany) {
  // The realistic shape: several hotkeys are registered, one of them lost
  // its grab, and only that one is retried.
  std::set<int> desired{1, 2, 3};
  std::set<int> actual{1, 3};
  std::unordered_map<int, HotKey> hotkeys{
      {1, makeHotkey(true, false, 70)},
      {2, makeHotkey(true, false, 71)},
      {3, makeHotkey(true, false, 72)},
  };

  auto pending = IO::selectGrabsToRetry(desired, actual, hotkeys);
  ASSERT_EQ(pending.size(), 1u);
  EXPECT_EQ(pending[0], 2);
}
