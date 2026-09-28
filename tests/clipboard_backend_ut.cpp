// clipboard_backend_ut.cpp — the clipboard-backend registry seam, headless.
//
// What this covers: extracting Qt out of libhavel_core.a moved Clipboard's
// inline Qt implementation behind the ClipboardBackendFactory registry
// (host/clipboard/ClipboardBackendFactory.hpp); the Qt implementation lives in
// havel_gui and is registered from the Qt bridge. This test exercises the
// seam with a test double standing in for the platform: the registry is
// consulted lazily, an explicitly set backend wins over the registry, a pinned
// non-registry method skips the registry, and the image path routes through
// the same seam.
//
// Expected result: all cases pass headless. The fallback tests do run the
// external xclip/xsel commands (harmless, read-only); nothing here asserts on
// their content, which would depend on the caller's real clipboard.

#include <gtest/gtest.h>

#include "host/clipboard/Clipboard.hpp"
#include "host/clipboard/ClipboardBackendFactory.hpp"

#include <string>
#include <vector>

namespace {

class FakeClipboardBackend : public havel::host::IClipboardBackend {
public:
    std::string text = "from-registry";
    std::string image = "from-registry-image";
    std::string lastSetImage;
    int setTextCalls = 0;

    std::string getText() const override { return text; }
    bool setText(const std::string &t) override {
        ++setTextCalls;
        return true;
    }
    bool clear() override { return true; }
    bool hasText() const override { return !text.empty(); }

    std::string getImage() const override { return image; }
    bool setImage(const std::string &base64Png) override {
        lastSetImage = base64Png;
        // Model a backend that stores what is set, so the round-trip is
        // observable through getImage.
        image = base64Png;
        return true;
    }
    bool hasImage() const override { return !image.empty(); }

    std::vector<std::string> getFiles() const override { return {}; }
    bool setFiles(const std::vector<std::string> &) override { return false; }
    bool hasFiles() const override { return false; }
};

class ClipboardBackendTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Unregister so the tests cannot contaminate each other (or anything
        // else in this binary) through the process-wide registry slot.
        havel::host::registerClipboardBackendFactory(nullptr);
    }
};

TEST_F(ClipboardBackendTest, RegisteredBackendServesText) {
    havel::host::registerClipboardBackendFactory(
        []() -> std::unique_ptr<havel::host::IClipboardBackend> {
            return std::make_unique<FakeClipboardBackend>();
        });
    havel::host::Clipboard clipboard;
    clipboard.setMethod(havel::host::Clipboard::Method::QT);

    EXPECT_EQ("from-registry", clipboard.getText());
    EXPECT_TRUE(clipboard.hasBackend());
}

TEST_F(ClipboardBackendTest, RegisteredBackendServesImage) {
    havel::host::registerClipboardBackendFactory(
        []() -> std::unique_ptr<havel::host::IClipboardBackend> {
            return std::make_unique<FakeClipboardBackend>();
        });
    havel::host::Clipboard clipboard;
    clipboard.setMethod(havel::host::Clipboard::Method::QT);

    EXPECT_TRUE(clipboard.setImage("payload-png"));
    EXPECT_EQ("payload-png", clipboard.getImage());

    // The same instance must have created exactly one backend: the image went
    // in and came back through it.
    ASSERT_TRUE(clipboard.backend() != nullptr);
}

TEST_F(ClipboardBackendTest, PinnedMethodSkipsRegistry) {
    havel::host::registerClipboardBackendFactory(
        []() -> std::unique_ptr<havel::host::IClipboardBackend> {
            return std::make_unique<FakeClipboardBackend>();
        });
    havel::host::Clipboard clipboard;
    clipboard.setMethod(havel::host::Clipboard::Method::X11);

    // Method::X11 pins the external-command path, which is what the old inline
    // Qt branch respected; the registry must not be consulted.
    clipboard.getText();

    EXPECT_FALSE(clipboard.hasBackend());
}

TEST_F(ClipboardBackendTest, NoFactoryLeavesBackendEmpty) {
    havel::host::Clipboard clipboard;
    clipboard.setMethod(havel::host::Clipboard::Method::QT);

    clipboard.getText();

    EXPECT_FALSE(clipboard.hasBackend());
}

TEST_F(ClipboardBackendTest, ExplicitBackendWinsOverRegistry) {
    havel::host::registerClipboardBackendFactory(
        []() -> std::unique_ptr<havel::host::IClipboardBackend> {
            return std::make_unique<FakeClipboardBackend>();
        });
    auto explicitBackend = std::make_unique<FakeClipboardBackend>();
    explicitBackend->text = "from-explicit";
    havel::host::IClipboardBackend *explicitPtr = explicitBackend.get();

    havel::host::Clipboard clipboard;
    clipboard.setBackend(std::move(explicitBackend));
    clipboard.setMethod(havel::host::Clipboard::Method::QT);

    EXPECT_EQ("from-explicit", clipboard.getText());
    EXPECT_EQ(explicitPtr, clipboard.backend());
}

} // namespace
