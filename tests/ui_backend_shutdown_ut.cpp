// ui_backend_shutdown_ut.cpp — the UI-backend shutdown path, headless.
//
// What this covers: a script error in a hotkey handler must not take the
// process down with "pure virtual method called" during teardown.
//
// The defect: HavelLauncher registered the shutdown callback as
//
//     [](int code) { host::UIManager::instance().backend()->quitEventLoop(code); }
//
// UIManager::backend() returns null when no UI backend could be created
// (headless, no display, or a toolkit that failed to load), and
// quitEventLoop is a PURE VIRTUAL method. Calling it through the null
// pointer reaches the pure-virtual thunk, which calls std::terminate:
// "pure virtual method called" -> "terminate called without an active
// exception" -> SIGABRT. The script's own error was already reported, so
// the abort turned a clean failure into a crash.
//
// The same callback re-fetched the backend instead of using the pointer the
// launcher had already validated, so it could not rely on it being non-null.
//
// This test drives the production helper (quitEventLoopIfAlive) that the
// launcher installs, so it fails if that helper regresses. A local copy of
// the lambda would only prove the copy works.
//
// Expected result: all cases pass headless, no GUI backend required. The
// test uses a test double standing in for the platform, so it never
// touches a real toolkit.

#include <gtest/gtest.h>

#include "host/ui/UIBackend.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

// A UIBackend that records the calls it receives. Only the methods the
// shutdown path touches need real bodies; the rest of the pure-virtual
// interface is implemented as no-ops.
class FakeUIBackend : public havel::host::UIBackend {
public:
    int quitEventLoopCalls = 0;
    int lastExitCode = -1;
    int shutdownCalls = 0;

    Api getApi() const override { return Api::IMGUI; }
    std::string getApiName() const override { return "fake"; }
    bool isAvailable() const override { return true; }
    bool initialize() override { return true; }
    void shutdown() override { ++shutdownCalls; }

    int runEventLoop() override { return 0; }
    void quitEventLoop(int exitCode = 0) override {
        ++quitEventLoopCalls;
        lastExitCode = exitCode;
    }

    std::shared_ptr<havel::ui::UIElement> window(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> panel(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> modal(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> text(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> label(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> image(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> icon(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> divider() override { return {}; }
    std::shared_ptr<havel::ui::UIElement> spacer(int) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> progress(int, int) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> spinner() override { return {}; }
    std::shared_ptr<havel::ui::UIElement> btn(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> input(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> textarea(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> checkbox(const std::string &, bool) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> toggle(const std::string &, bool) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> slider(int, int, int) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> dropdown(const std::vector<std::string> &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> row() override { return {}; }
    std::shared_ptr<havel::ui::UIElement> col() override { return {}; }
    std::shared_ptr<havel::ui::UIElement> grid(int) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> table(int, int) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> flex(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> scroll() override { return {}; }
    std::shared_ptr<havel::ui::UIElement> canvas(int, int) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> menu(const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> menuItem(const std::string &, const std::string &) override { return {}; }
    std::shared_ptr<havel::ui::UIElement> menuSeparator() override { return {}; }
    void realize(std::shared_ptr<havel::ui::UIElement>) override {}
    void show(std::shared_ptr<havel::ui::UIElement>) override {}
    void hide(std::shared_ptr<havel::ui::UIElement>) override {}
    void close(std::shared_ptr<havel::ui::UIElement>) override {}
    void alert(const std::string &) override {}
    bool confirm(const std::string &) override { return false; }
    std::string filePicker(const std::string &) override { return ""; }
    std::string dirPicker(const std::string &) override { return ""; }
    void notify(const std::string &, const std::string &) override {}
    void pumpEvents(int) override {}
    void setApplicationMetadata(const ApplicationMetadata &) override {}
    void resetPerRunState() override {}
    bool hasActiveWindows() const override { return false; }
    void onAllWindowsClosed(std::function<void()>) override {}
    std::string getValue(std::shared_ptr<havel::ui::UIElement>) override { return ""; }
    void setValue(std::shared_ptr<havel::ui::UIElement>, const std::string &) override {}
    void trayIcon(const std::string &, const std::string &) override {}
    void trayMenu(std::shared_ptr<havel::ui::UIElement>) override {}
    void trayNotify(const std::string &, const std::string &, const std::string &) override {}
    void trayShow() override {}
    void trayHide() override {}
    bool trayIsVisible() const override { return false; }
    void applyStyle(std::shared_ptr<havel::ui::UIElement>, const std::string &,
                    const havel::ui::PropValue &) override {}
    void canvasFlush(std::shared_ptr<havel::ui::UIElement>) override {}
    void canvasClear(std::shared_ptr<havel::ui::UIElement>) override {}
};

TEST(UIBackendShutdownTest, NullBackendDoesNotAbort) {
    // The regression: with no UI backend, asking the backend to quit must
    // return normally. Before the fix this called quitEventLoop through a
    // null pointer into a pure-virtual slot and terminated the process
    // ("pure virtual method called"). If the call aborts, the process dies
    // here and the test binary reports a crash instead of a result.
    havel::host::quitEventLoopIfAlive(nullptr, 1);

    SUCCEED() << "quitEventLoopIfAlive with a null backend returned normally";
}

TEST(UIBackendShutdownTest, LiveBackendReceivesTheExitCode) {
    auto backend = std::make_unique<FakeUIBackend>();

    havel::host::quitEventLoopIfAlive(backend.get(), 42);

    EXPECT_EQ(backend->quitEventLoopCalls, 1);
    EXPECT_EQ(backend->lastExitCode, 42);
}

TEST(UIBackendShutdownTest, ShutdownThenQuitDoesNotCrash) {
    // The teardown ordering that produced the original crash: the backend
    // is shut down (QtBackend::shutdown resets its service) and the quit
    // arrives afterwards. A backend that survives shutdown must still be
    // asked to quit exactly once.
    auto backend = std::make_unique<FakeUIBackend>();

    backend->shutdown();
    havel::host::quitEventLoopIfAlive(backend.get(), 7);

    EXPECT_EQ(backend->shutdownCalls, 1);
    EXPECT_EQ(backend->quitEventLoopCalls, 1);
    EXPECT_EQ(backend->lastExitCode, 7);
}

} // namespace
