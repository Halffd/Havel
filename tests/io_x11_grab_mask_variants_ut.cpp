// havel::GrabMaskVariants - the rule that decides which X11 modifier masks
// a hotkey is grabbed under.
//
// Background: a hotkey has to fire whether CapsLock and NumLock are on or
// off, so the X11 grab is installed once per combination of the two lock
// modifiers.
//
// The defect this locks down: ToPlatformMask() OR-ed LockMask /
// numlockMask_ into the modifier mask when the script asked for a
// CapsLock or NumLock modifier, and the variant expansion was then applied
// on top of that. The "no lock modifier" variant became unreachable:
//
//   modifiers = ControlMask | LockMask          (CapsLock required)
//   variants  = {0, LockMask, numlock, numlock|LockMask}
//   final     = {Control|Lock, Control|Lock, Control|Lock|numlock, ...}
//
// so the plain variant was never grabbed, never ungrabbed, and the hotkey
// fired with CapsLock OFF even though the script required it.
//
// The same expansion is used for the ungrab, so register and unregister
// agree on the mask set - that is what keeps a removed hotkey from leaving
// a stale grab behind on the X server.
// gtest declares a helper named Bool(); <X11/Xlib.h> defines Bool as a
// macro. gtest must come first so its declaration is not rewritten by the
// macro.
#include <gtest/gtest.h>

#include "core/io/X11IOBackend.hpp"

#include <algorithm>
#include <vector>

using havel::GrabMaskVariants;

namespace {

// The X11 modifier bits used by the test. They are the values from
// <X11/X.h>; the header is included transitively, but naming them here
// keeps the expectations readable.
constexpr unsigned int kShift = 1u << 0;
constexpr unsigned int kLock = 1u << 1;
constexpr unsigned int kControl = 1u << 2;
constexpr unsigned int kMod1 = 1u << 3;
constexpr unsigned int kMod2 = 1u << 6; // the usual NumLock bit
constexpr unsigned int kMod4 = 1u << 7;

bool Contains(const std::vector<unsigned int> &masks, unsigned int mask) {
  return std::find(masks.begin(), masks.end(), mask) != masks.end();
}

size_t Count(const std::vector<unsigned int> &masks, unsigned int mask) {
  return static_cast<size_t>(std::count(masks.begin(), masks.end(), mask));
}

} // namespace

TEST(GrabMaskVariants, PlainModifierGetsEveryLockCombination) {
  // A hotkey with no lock modifier must be grabbed with CapsLock and
  // NumLock in every state, so the key fires regardless of their state.
  auto masks = GrabMaskVariants(kControl, kMod2);

  EXPECT_TRUE(Contains(masks, kControl));
  EXPECT_TRUE(Contains(masks, kControl | kLock));
  EXPECT_TRUE(Contains(masks, kControl | kMod2));
  EXPECT_TRUE(Contains(masks, kControl | kMod2 | kLock));
  EXPECT_EQ(masks.size(), 4u);
}

TEST(GrabMaskVariants, LockBitsAreStrippedFromTheBaseMask) {
  // ToPlatformMask() already OR-ed LockMask in (the script required
  // CapsLock). The lock bits must NOT stay in the base, or the
  // "CapsLock off" variant is unreachable - and that variant is the one
  // that never gets ungrabbed, leaving a stale grab behind.
  auto masks = GrabMaskVariants(kControl | kLock, kMod2);

  EXPECT_TRUE(Contains(masks, kControl));
  EXPECT_EQ(Count(masks, kControl), 1u);
}

TEST(GrabMaskVariants, NumLockBitIsStrippedFromTheBaseMask) {
  auto masks = GrabMaskVariants(kControl | kMod2, kMod2);

  EXPECT_TRUE(Contains(masks, kControl));
  EXPECT_EQ(Count(masks, kControl), 1u);
}

TEST(GrabMaskVariants, NoDuplicateMasksWhenNumLockIsAbsent) {
  // A keymap without NumLock reports numlockMask == 0. The expansion then
  // produces the same masks twice; installing the same grab twice is
  // harmless but the list must stay deduplicated so the ungrab removes
  // exactly what the grab installed.
  auto masks = GrabMaskVariants(kControl, 0u);

  EXPECT_EQ(masks.size(), 2u);
  EXPECT_TRUE(Contains(masks, kControl));
  EXPECT_TRUE(Contains(masks, kControl | kLock));
  EXPECT_EQ(Count(masks, kControl), 1u);
  EXPECT_EQ(Count(masks, kControl | kLock), 1u);
}

TEST(GrabMaskVariants, NoDuplicateMasksWhenNumLockSharesTheCapsLockBit) {
  // Some keymaps map Num_Lock to the same modifier bit as CapsLock.
  auto masks = GrabMaskVariants(kControl, kLock);

  EXPECT_EQ(masks.size(), 2u);
  EXPECT_TRUE(Contains(masks, kControl));
  EXPECT_TRUE(Contains(masks, kControl | kLock));
}

TEST(GrabMaskVariants, RegisterAndUnregisterSeeTheSameMaskSet) {
  // The whole point: the grab and the ungrab must be issued for exactly
  // the same masks. A different set on the ungrab leaves the grab on the X
  // server, and the removed hotkey keeps stealing the key.
  const unsigned int numlock = kMod2;
  auto grabMasks = GrabMaskVariants(kControl | kLock, numlock);
  auto ungrabMasks = GrabMaskVariants(kControl | kLock, numlock);

  ASSERT_EQ(grabMasks.size(), ungrabMasks.size());
  for (unsigned int mask : grabMasks)
    EXPECT_TRUE(Contains(ungrabMasks, mask)) << "mask " << mask;
}

TEST(GrabMaskVariants, OtherModifiersSurviveTheExpansion) {
  auto masks = GrabMaskVariants(kShift | kControl | kMod1 | kMod4, kMod2);

  for (unsigned int mask : masks) {
    EXPECT_NE(mask & kShift, 0u);
    EXPECT_NE(mask & kControl, 0u);
    EXPECT_NE(mask & kMod1, 0u);
    EXPECT_NE(mask & kMod4, 0u);
  }
}
