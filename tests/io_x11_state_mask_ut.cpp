// havel::X11StateToAbstractMask - the conversion from an X11 event state to
// the platform-neutral ModifierMasks layout.
//
// The defect this locks down: X11HotkeyMonitor::MonitorLoop compared the
// cleaned X11 event state DIRECTLY against hotkey.modifiers:
//
//   cleanedState = CleanMask(keyEvent->state)   // X11 PLATFORM bits
//   hotkey.modifiers                             // ModifierMasks bits
//
// The two layouts are not interchangeable. The X11 state uses the server's
// modifier bits (Shift=0, Lock=1, Control=2, Mod1=3, Mod2=4, Mod4=6) while
// ModifierMasks uses the evdev bits (Ctrl=0, Shift=1, Alt=2, Meta=3).
// Comparing them directly could never match a hotkey that carries a
// modifier: the monitor was deaf to every Ctrl/Shift/Alt/Meta hotkey.
//
// The fix converts the state through one shared function, so the monitor and
// the backend cannot drift apart again.
//
// Pure function: no X server, no display, no timing side effects.
//
// gtest declares a helper named Bool(); <X11/Xlib.h> defines Bool as a
// macro. gtest must come first so its declaration is not rewritten by the
// macro.
#include <gtest/gtest.h>

#include "core/io/X11IOBackend.hpp"

using havel::X11StateToAbstractMask;

// ModifierMasks is a NAMESPACE of constants, not a class - so there is no
// using-declaration for it; the constants are named in full below.
namespace Masks = havel::ModifierMasks;

namespace {

// The X11 modifier bits from <X11/X.h>. They are included transitively by
// x11.h, but naming them here keeps the expectations readable.
constexpr unsigned int kShift = 1u << 0;
constexpr unsigned int kLock = 1u << 1;
constexpr unsigned int kControl = 1u << 2;
constexpr unsigned int kMod1 = 1u << 3;
constexpr unsigned int kMod2 = 1u << 4;
constexpr unsigned int kMod4 = 1u << 6;
// The modifier bit a server maps Num_Lock to. Mod2 is the usual one on a
// standard PC keymap.
constexpr unsigned int kNumLock = 1u << 4;

} // namespace

TEST(X11StateToAbstractMask, NoModifiersMeansNoBits) {
  EXPECT_EQ(X11StateToAbstractMask(0, kNumLock), 0);
}

// The core of the defect: a Ctrl press in the X11 state must come out as
// the CONTROL bit, not as some other bit.
TEST(X11StateToAbstractMask, ControlBecomesTheControlBit) {
  const int mask = X11StateToAbstractMask(kControl, kNumLock);
  EXPECT_EQ(mask & Masks::CONTROL, Masks::CONTROL);
  EXPECT_EQ(mask & Masks::SHIFT, 0);
}

TEST(X11StateToAbstractMask, ShiftBecomesTheShiftBit) {
  const int mask = X11StateToAbstractMask(kShift, kNumLock);
  EXPECT_EQ(mask & Masks::SHIFT, Masks::SHIFT);
  EXPECT_EQ(mask & Masks::CONTROL, 0);
}

TEST(X11StateToAbstractMask, AltBecomesTheAltBit) {
  const int mask = X11StateToAbstractMask(kMod1, kNumLock);
  EXPECT_EQ(mask & Masks::ALT, Masks::ALT);
}

TEST(X11StateToAbstractMask, MetaBecomesTheMetaBit) {
  const int mask = X11StateToAbstractMask(kMod4, kNumLock);
  EXPECT_EQ(mask & Masks::META, Masks::META);
}

// A hotkey with no modifier must match an event with no modifier: the
// monitor compares the converted mask against hotkey.modifiers exactly.
TEST(X11StateToAbstractMask, PlainKeyMatchesAPlainHotkey) {
  EXPECT_EQ(X11StateToAbstractMask(0, kNumLock), 0);
}

// A Ctrl+Shift chord must produce exactly the two bits the hotkey asks
// for.
TEST(X11StateToAbstractMask, ChordProducesBothBits) {
  const int mask = X11StateToAbstractMask(kControl | kShift, kNumLock);
  EXPECT_EQ(mask, Masks::CONTROL | Masks::SHIFT);
}

// CapsLock is reported as the LOCK bit, which is NOT one of the four
// modifier bits a hotkey matches on - so a CapsLock'd event does not
// silently satisfy a Ctrl hotkey.
TEST(X11StateToAbstractMask, CapsLockIsTheLockBitNotAControlBit) {
  const int mask = X11StateToAbstractMask(kLock, kNumLock);
  EXPECT_EQ(mask & Masks::LOCK, Masks::LOCK);
  EXPECT_EQ(mask & Masks::CONTROL, 0);
  EXPECT_EQ(mask & Masks::SHIFT, 0);
}

// NumLock is reported only when the server maps it to a modifier bit. On a
// keymap without NumLock (numlockMask == 0) its state cannot be told apart.
TEST(X11StateToAbstractMask, NumLockIsReportedWhenTheKeymapHasIt) {
  const int mask = X11StateToAbstractMask(kNumLock, kNumLock);
  EXPECT_EQ(mask & Masks::NUMLOCK, Masks::NUMLOCK);
}

TEST(X11StateToAbstractMask, NumLockIsAbsentWithoutAKeymapBit) {
  const int mask = X11StateToAbstractMask(kNumLock, 0);
  EXPECT_EQ(mask & Masks::NUMLOCK, 0);
}

// Mod2 is neither Alt nor Meta on an X11 server: with NumLock mapped to
// Mod2, a plain Mod2 state must not be reported as a modifier the hotkey
// matchers look at.
TEST(X11StateToAbstractMask, UnknownModifierBitsAreNotReported) {
  const int mask = X11StateToAbstractMask(kMod2, 0);
  EXPECT_EQ(mask & Masks::ALT, 0);
  EXPECT_EQ(mask & Masks::META, 0);
  EXPECT_EQ(mask & Masks::CONTROL, 0);
  EXPECT_EQ(mask & Masks::SHIFT, 0);
}
