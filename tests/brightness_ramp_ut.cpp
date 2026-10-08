// Regression test for the gamma-ramp brightness readback.
//
// extractBrightnessFromGammaRamp() estimated brightness as
// red[middle] / 65535. The set path (BrightnessManager::setBrightness ->
// fill_gamma_ramp) writes a *linear* ramp whose value at index j is
// j/(size-1) * brightness, so the top of the ramp IS the applied brightness
// and the middle is exactly half of it. The readback therefore returned half
// the real value for every linear ramp.
//
// That is not cosmetic: brightness.increase() steps a monitor from its current
// value (BrightnessManager::increaseBrightness, reached via the
// brightness.increase host function), so a halved readback made every increase()
// step land below where it started and the screen dimmed on the first keypress.
// getBrightnessGamma() already carries that fix in a comment and uses the ramp
// top; this test pins the same invariant on the helper that had been missed.
//
// Pure function of the caller-supplied ramp, so no X display is required --
// which is what lets this run inside the headless ctest sandbox. The
// BrightnessManager constructor is also display-tolerant now (it used to throw
// "No X11 display available" from DisplayManager::GetRootWindow(), which would
// have made this suite silently skip in ctest and never actually check
// anything).

#include <gtest/gtest.h>

#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <vector>

#include "core/BrightnessManager.hpp"

namespace {

// Build a ramp the way the set path does: linear, scaled by brightness, with
// neutral gamma and no temperature tint. This is what XRRGetCrtcGamma returns
// after XRRSetCrtcGamma.
XRRCrtcGamma *makeLinearRamp(int size, double brightness) {
  auto *gamma = XRRAllocGamma(size);
  for (int j = 0; j < size; ++j) {
    const double normalized = static_cast<double>(j) / (size - 1);
    const auto value = static_cast<unsigned short>(
        std::clamp(normalized * brightness * 65535.0, 0.0, 65535.0));
    gamma->red[j] = value;
    gamma->green[j] = value;
    gamma->blue[j] = value;
  }
  return gamma;
}

constexpr int kGammaSize = 1024;

// Constructed once: the readback under test is const and stateless, and the
// constructor probes the display, so there is no reason to rebuild per case.
// Also proves the class is constructible with no display at all.
havel::BrightnessManager &manager() {
  static havel::BrightnessManager instance;
  return instance;
}

} // namespace

TEST(BrightnessRamp, ReadbackWorksWithNoDisplayAtAll) {
  // Guards the reason the rest of this suite can run headless. If the
  // constructor regresses to throwing without a display, this fails loudly here
  // instead of the whole suite erroring out with an unrelated message.
  EXPECT_NO_THROW(manager());
}

TEST(BrightnessRamp, ReadsBackTheValueThatWasWritten) {
  // 0.25 and 0.35 are the values observed on the two attached monitors, so
  // this covers the real case rather than a convenient round number.
  for (double written : {0.25, 0.35, 0.5, 0.85, 1.0}) {
    XRRCrtcGamma *gamma = makeLinearRamp(kGammaSize, written);
    ASSERT_NE(gamma, nullptr);

    const double read = manager().extractBrightnessFromGammaRamp(gamma, "test-monitor");

    // 16-bit rounding costs at most one step; allow a hair over that.
    EXPECT_NEAR(read, written, 1.0 / 65535.0)
        << "readback of a ramp written at " << written << " returned " << read
        << " -- the middle index halves a linear ramp";

    XRRFreeGamma(gamma);
  }
}

TEST(BrightnessRamp, DoesNotUnderreadAtLowBrightness) {
  XRRCrtcGamma *gamma = makeLinearRamp(kGammaSize, 0.25);
  ASSERT_NE(gamma, nullptr);

  // The specific failure: the old middle-index readback returned ~0.125 here.
  EXPECT_GT(manager().extractBrightnessFromGammaRamp(gamma, "test-monitor"),
            0.24);

  XRRFreeGamma(gamma);
}

TEST(BrightnessRamp, IncreaseStepMovesUpward) {
  // brightness.increase() is read-then-write. If the readback underreads, the
  // set lands below the current value and the display dims instead of
  // brightening. Model the sequence the hotkey performs.
  const double start = 0.5;
  const double step = 0.05;

  XRRCrtcGamma *gamma = makeLinearRamp(kGammaSize, start);
  ASSERT_NE(gamma, nullptr);

  const double current =
      manager().extractBrightnessFromGammaRamp(gamma, "test-monitor");
  XRRFreeGamma(gamma);

  const double afterIncrease = current + step;
  EXPECT_GT(afterIncrease, start)
      << "read-then-write increase() would have dimmed the screen from " << start
      << " to " << afterIncrease;
}

TEST(BrightnessRamp, IgnoresDimmerChannelsFromTemperatureTint) {
  // A temperature tint scales channels below 1.0, so a blue-heavy ramp has a
  // low blue top. Reading a single channel would underread; the max channel
  // keeps the estimate closest to the brightness that was applied.
  const double brightness = 0.5;

  auto *gamma = XRRAllocGamma(kGammaSize);
  for (int j = 0; j < kGammaSize; ++j) {
    const double normalized = static_cast<double>(j) / (kGammaSize - 1);
    const double scaled = normalized * brightness * 65535.0;
    gamma->red[j] = static_cast<unsigned short>(std::clamp(scaled, 0.0, 65535.0));
    // Blue tinted down to 0.8 of red, as kelvin_to_rgb() would produce.
    gamma->blue[j] =
        static_cast<unsigned short>(std::clamp(scaled * 0.8, 0.0, 65535.0));
    gamma->green[j] =
        static_cast<unsigned short>(std::clamp(scaled * 0.9, 0.0, 65535.0));
  }

  const double read =
      manager().extractBrightnessFromGammaRamp(gamma, "test-monitor");
  EXPECT_NEAR(read, brightness, 1.0 / 65535.0)
      << "a tinted ramp should still read back as the brightness that was set";

  XRRFreeGamma(gamma);
}

TEST(BrightnessRamp, RejectsInvalidInput) {
  EXPECT_EQ(manager().extractBrightnessFromGammaRamp(nullptr, "m"), -1.0);

  // size 0 must not be indexed.
  XRRCrtcGamma *empty = XRRAllocGamma(0);
  ASSERT_NE(empty, nullptr);
  empty->size = 0;
  EXPECT_EQ(manager().extractBrightnessFromGammaRamp(empty, "m"), -1.0);
  XRRFreeGamma(empty);
}
