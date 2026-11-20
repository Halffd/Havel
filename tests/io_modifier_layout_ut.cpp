// havel::ModifierMasks and IO::ParseModifiersAndFlags - ONE modifier bit
// layout for every backend.
//
// The defect this locks down: the non-evdev branch of
// ParseModifiersAndFlags() produced ModifierMasks::SHIFT for Shift and
// ModifierMasks::CONTROL for Ctrl, and ModifierMasks itself had SHIFT on
// bit 0 and CONTROL on bit 1. The evdev branch produced the bits inline
// instead - (1 << 0) for ctrl, (1 << 1) for shift - and the hotkey matchers
// (EventListener::CheckModifierMatch and HotkeyManager::checkModifierMatch)
// read that evdev layout.
//
// So the two layouts disagreed on the Ctrl/Shift bits, and the disagreement
// was invisible on the evdev path (both sides used the evdev layout) and
// fatal on the X11 path:
//
//   hotkey "^A"  (Ctrl)   -> hotkey.modifiers = CONTROL = (1 << 1)
//   matcher reads bit 1   -> "Shift"
//   => a Ctrl hotkey was matched as if it required Shift, and a Shift
//      hotkey was matched as if it required Ctrl.
//
// The fix aligns ModifierMasks with the evdev layout (Ctrl = bit 0,
// Shift = bit 1) and makes ParseModifiersAndFlags produce ModifierMasks for
// every backend.
//
// The test is a pure string -> bits mapping, so it needs no X server, no
// display and no timing side effects.
//
// gtest declares a helper named Bool(); <X11/Xlib.h> defines Bool as a
// macro. gtest must come first so its declaration is not rewritten by the
// macro.
#include <gtest/gtest.h>

#include "core/io/IO.hpp"
#include "core/io/IOBackend.hpp"

using havel::IO;
using havel::ParsedHotkey;

// ModifierMasks is a NAMESPACE of constants, not a class - so there is no
// using-declaration for it; the constants are named in full below.
namespace Masks = havel::ModifierMasks;

namespace {

// The modifier bits of a parsed hotkey, as a set of names. Comparing names
// instead of raw bits keeps a failure message readable and states the
// intent: WHICH modifier was requested, not just "some bit differs".
std::string ModifierNames(int modifiers) {
  std::string names;
  auto append = [&names](const char *name) {
    if (!names.empty()) names += "+";
    names += name;
  };
  if (modifiers & Masks::CONTROL) append("ctrl");
  if (modifiers & Masks::SHIFT) append("shift");
  if (modifiers & Masks::ALT) append("alt");
  if (modifiers & Masks::META) append("meta");
  if (names.empty()) names = "none";
  return names;
}

} // namespace

TEST(ModifierMasks, CtrlAndShiftHaveDistinctBits) {
  // The whole defect is these two constants sitting on the same bits as
  // the inline evdev layout, so they must not alias each other.
  EXPECT_NE(Masks::CONTROL, Masks::SHIFT);
  EXPECT_EQ(Masks::CONTROL & Masks::SHIFT, 0);
}

// The layout every backend and every matcher agrees on.
TEST(ModifierMasks, LayoutIsCtrlShiftAltMeta) {
  EXPECT_EQ(Masks::CONTROL, 1 << 0);
  EXPECT_EQ(Masks::SHIFT, 1 << 1);
  EXPECT_EQ(Masks::ALT, 1 << 2);
  EXPECT_EQ(Masks::META, 1 << 3);
}

// The parse produces the SAME bits for the evdev path and for every other
// backend. Before the fix the two disagreed on the Ctrl/Shift bits.
TEST(ParseModifiersAndFlags, EvdevAndX11ProduceTheSameModifierBits) {
  struct Case {
    const char *input;
    const char *expected;
  };
  const Case cases[] = {
      {"ctrl+a", "ctrl"},
      {"shift+a", "shift"},
      {"ctrl+shift+a", "ctrl+shift"},
      {"alt+a", "alt"},
      {"meta+a", "meta"},
      {"win+a", "meta"},
      {"^a", "ctrl"},
      {"+a", "shift"},
      {"!a", "alt"},
      {"#a", "meta"},
  };

  for (const auto &c : cases) {
    const ParsedHotkey evdev = IO::ParseModifiersAndFlags(c.input, true);
    const ParsedHotkey x11 = IO::ParseModifiersAndFlags(c.input, false);
    EXPECT_EQ(ModifierNames(evdev.modifiers), c.expected)
        << "evdev parse of '" << c.input << "'";
    EXPECT_EQ(ModifierNames(x11.modifiers), c.expected)
        << "x11 parse of '" << c.input << "'";
    EXPECT_EQ(evdev.modifiers, x11.modifiers)
        << "evdev and x11 disagree on '" << c.input << "'";
  }
}

// The specific symptom: a Ctrl hotkey must request Ctrl, not Shift.
TEST(ParseModifiersAndFlags, CtrlHotkeyRequestsCtrlNotShift) {
  const ParsedHotkey parsed = IO::ParseModifiersAndFlags("^a", false);
  EXPECT_EQ(parsed.modifiers & Masks::CONTROL, Masks::CONTROL);
  EXPECT_EQ(parsed.modifiers & Masks::SHIFT, 0);
}

TEST(ParseModifiersAndFlags, ShiftHotkeyRequestsShiftNotCtrl) {
  const ParsedHotkey parsed = IO::ParseModifiersAndFlags("+a", false);
  EXPECT_EQ(parsed.modifiers & Masks::SHIFT, Masks::SHIFT);
  EXPECT_EQ(parsed.modifiers & Masks::CONTROL, 0);
}

// A hotkey with no modifier carries no modifier bits at all.
TEST(ParseModifiersAndFlags, NoModifierMeansNoBits) {
  const ParsedHotkey parsed = IO::ParseModifiersAndFlags("a", false);
  EXPECT_EQ(parsed.modifiers, 0);
}
