// pixel_provider_ut.cpp — the screen-provider seam, tested without a display.
//
// What this covers: extracting Qt out of libhavel_core.a moved every "ask the
// platform for screen pixels" call in PixelAutomation behind the
// havel::ScreenProvider slot in core/automation/ScreenCapture.hpp. Before that,
// the only way to check any of it was a live X server, so the BGRA channel
// order and the screenshot cache had no coverage at all.
//
// A test double stands in for the platform here, so this asserts core-side
// behaviour only: the slot is honoured, the BGRA bytes a provider reports reach
// Color as (r, g, b, a) rather than some other permutation, region geometry is
// passed through unchanged, and the capture -> cache -> template-match pipeline
// finds a known pattern in the captured screen — coverage the direct
// QGuiApplication calls never had, since none of it was checkable without a
// live X server.
//
// Expected result: all cases pass on a headless box, with no Qt, no X server
// and no real screen involved.

#include <gtest/gtest.h>

#include "core/automation/PixelAutomation.hpp"
#include "core/automation/ScreenCapture.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Distinct per channel: a swapped or duplicated channel cannot still match.
constexpr unsigned char kRed = 11;
constexpr unsigned char kGreen = 97;
constexpr unsigned char kBlue = 203;
constexpr unsigned char kAlpha = 255;

// getPixel samples a 3x3 grab at its centre, which is what the old
// QScreen::grabWindow(x - 1, y - 1, 3, 3) call did.
constexpr int kSampleSpan = 3;
constexpr int kSampleX = 100;
constexpr int kSampleY = 50;

// Fake screen geometry for the template-matching case.
constexpr int kScreenW = 64;
constexpr int kScreenH = 48;
constexpr int kTemplateW = 8;
constexpr int kTemplateH = 8;
constexpr int kTemplateX = 20;
constexpr int kTemplateY = 12;

havel::ScreenBounds g_bounds{};
havel::ScreenBounds g_lastRegion{};
int g_captureCalls = 0;
bool g_captureSucceeds = true;

// Deterministic and non-uniform, mixed so that shifted windows do not resemble
// each other: a flat or smooth pattern has either zero variance (TM_CCOEFF_NORMED
// returns NaN) or correlates near-perfectly with its own shifts, which floods
// matchTemplate with candidates and turns the dedup pass into an O(n^2) slog.
unsigned char patternChannel(int x, int y, int channel) {
    unsigned hash = static_cast<unsigned>(x) * 2654435761u +
                    static_cast<unsigned>(y) * 40503u +
                    static_cast<unsigned>(channel) * 97u;
    hash ^= hash >> 13;
    hash *= 1274126177u;
    hash ^= hash >> 16;
    return static_cast<unsigned char>(hash % 251);
}

bool fakeBounds(havel::ScreenBounds &out) {
    out = g_bounds;
    return true;
}

bool fakeCapture(const havel::ScreenBounds &region, havel::ScreenPixels &out) {
    ++g_captureCalls;
    g_lastRegion = region;
    if (!g_captureSucceeds) {
        return false;
    }
    // w or h <= 0 means "the whole screen" to the provider contract, so the
    // screen size is fixed here rather than derived from the request.
    out.w = kScreenW;
    out.h = kScreenH;
    out.bgra.assign(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 4, 0);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            const size_t base = (static_cast<size_t>(y) * out.w + x) * 4;
            out.bgra[base + 0] = patternChannel(x, y, 0);
            out.bgra[base + 1] = patternChannel(x, y, 1);
            out.bgra[base + 2] = patternChannel(x, y, 2);
            out.bgra[base + 3] = 255;
        }
    }
    return true;
}

const havel::ScreenProvider kFakeProvider{&fakeBounds, &fakeCapture};

// Writes a binary PPM of the pattern, which is what the template has to be: a
// crop of the fake screen, in the same channel order, so that after OpenCV's
// BGR2GRAY on both sides the correlation is ~1.
std::string writeTemplatePpm() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "havel_pixel_provider_template.ppm";
    std::string body = "P6\n" + std::to_string(kTemplateW) + " " +
                       std::to_string(kTemplateH) + "\n255\n";
    body.reserve(body.size() + static_cast<size_t>(kTemplateW) * kTemplateH * 3);
    for (int y = 0; y < kTemplateH; ++y) {
        for (int x = 0; x < kTemplateW; ++x) {
            const int screenX = kTemplateX + x;
            const int screenY = kTemplateY + y;
            body.push_back(static_cast<char>(patternChannel(screenX, screenY, 0)));
            body.push_back(static_cast<char>(patternChannel(screenX, screenY, 1)));
            body.push_back(static_cast<char>(patternChannel(screenX, screenY, 2)));
        }
    }
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return {};
    }
    const size_t written = std::fwrite(body.data(), 1, body.size(), file);
    std::fclose(file);
    if (written != body.size()) {
        return {};
    }
    return path.string();
}

// A flat grab, which is enough to pin the channel permutation: getPixel reads a
// single pixel, and the point is which byte becomes which channel.
bool solidCapture(const havel::ScreenBounds &, havel::ScreenPixels &out) {
    out.w = kSampleSpan;
    out.h = kSampleSpan;
    out.bgra.assign(static_cast<size_t>(kSampleSpan) * kSampleSpan * 4, 0);
    for (size_t i = 0; i < out.bgra.size(); i += 4) {
        out.bgra[i + 0] = kBlue;
        out.bgra[i + 1] = kGreen;
        out.bgra[i + 2] = kRed;
        out.bgra[i + 3] = kAlpha;
    }
    return true;
}

class PixelProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_bounds = {0, 0, kScreenW, kScreenH};
        g_lastRegion = {0, 0, 0, 0};
        g_captureCalls = 0;
        g_captureSucceeds = true;
        previous_ = havel::screenProvider();
        havel::setScreenProvider(&kFakeProvider);
    }

    void TearDown() override {
        havel::setScreenProvider(previous_);
        std::error_code ignored;
        std::filesystem::remove(std::filesystem::temp_directory_path() /
                                    "havel_pixel_provider_template.ppm",
                                ignored);
    }

    const havel::ScreenProvider *previous_ = nullptr;
};

TEST_F(PixelProviderTest, FullScreenComesFromProviderBounds) {
    g_bounds = {7, 11, 2560, 1440};

    const havel::ScreenRegion region = havel::ScreenRegion::fullScreen();

    EXPECT_EQ(7, region.x);
    EXPECT_EQ(11, region.y);
    EXPECT_EQ(2560, region.w);
    EXPECT_EQ(1440, region.h);
}

TEST_F(PixelProviderTest, GetPixelMapsProviderBgraToColorChannels) {
    const havel::ScreenProvider solidProvider{&fakeBounds, &solidCapture};
    havel::setScreenProvider(&solidProvider);

    const havel::Color got = havel::PixelAutomation{}.getPixel(kSampleX, kSampleY);

    EXPECT_EQ(kRed, got.r);
    EXPECT_EQ(kGreen, got.g);
    EXPECT_EQ(kBlue, got.b);
    EXPECT_EQ(kAlpha, got.a);
}

TEST_F(PixelProviderTest, GetPixelRequestsGrabCentredOnTheTarget) {
    havel::PixelAutomation automation;

    automation.getPixel(kSampleX, kSampleY);

    ASSERT_EQ(1, g_captureCalls);
    EXPECT_EQ(kSampleX - 1, g_lastRegion.x);
    EXPECT_EQ(kSampleY - 1, g_lastRegion.y);
    EXPECT_EQ(kSampleSpan, g_lastRegion.w);
    EXPECT_EQ(kSampleSpan, g_lastRegion.h);
}

TEST_F(PixelProviderTest, GetPixelWithoutProviderIsBlack) {
    havel::setScreenProvider(nullptr);

    const havel::Color got = havel::PixelAutomation{}.getPixel(kSampleX, kSampleY);

    EXPECT_EQ(0, got.r);
    EXPECT_EQ(0, got.g);
    EXPECT_EQ(0, got.b);
    // A host with no screen access reports opaque black, as it did before.
    EXPECT_EQ(255, got.a);
    EXPECT_EQ(0, g_captureCalls);
}

TEST_F(PixelProviderTest, GetPixelWhenCaptureFailsIsBlack) {
    g_captureSucceeds = false;

    const havel::Color got = havel::PixelAutomation{}.getPixel(kSampleX, kSampleY);

    EXPECT_EQ(0, got.r);
    EXPECT_EQ(0, got.g);
    EXPECT_EQ(0, got.b);
    EXPECT_EQ(1, g_captureCalls);
}

TEST_F(PixelProviderTest, CachedScreenshotOutlivesProviderBuffer) {
    const std::string templatePath = writeTemplatePpm();
    ASSERT_FALSE(templatePath.empty()) << "could not write the template PPM";
    havel::PixelAutomation automation;

    const havel::ImageMatch match = automation.findImage(templatePath, {}, 0.9f);

    // The provider's std::vector is a local of the capture call by the time
    // matchTemplate reads the cached Mat. The cache must own its pixels by
    // then; if it still aliased the vector and that aliasing went wrong, this
    // reads garbage and no match is found.
    ASSERT_TRUE(match.found) << "template not found in the captured screen";
    EXPECT_EQ(kTemplateW, match.w);
    EXPECT_EQ(kTemplateH, match.h);
    EXPECT_GE(automation.countImage(templatePath, {}, 0.9f), 1);
}

} // namespace
