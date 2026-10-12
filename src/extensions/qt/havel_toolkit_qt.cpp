#include "c/ToolkitPlugin.h"
#include "extensions/qt/QtScreenshotBackend.hpp"
#include "extensions/qt/QtAltTabBackend.hpp"
#include "extensions/qt/QtClipboardBackend.hpp"
#include "extensions/qt/QtClipboardManagerBackend.hpp"
#include "extensions/qt/QtExtensionFunctions.hpp"
#include "host/ui/QtBackend.hpp"

using namespace havel::host;

static void *create_qt_ui_backend() {
    return new QtBackend();
}

static void destroy_qt_ui_backend(void *p) {
    delete castUIBackend(p);
}

static void *create_qt_screenshot_backend() {
    return new QtScreenshotBackend();
}

static void destroy_qt_screenshot_backend(void *p) {
    delete castScreenshotBackend(p);
}

static void *create_qt_alttab_backend() {
    return new QtAltTabBackend();
}

static void destroy_qt_alttab_backend(void *p) {
    delete castAltTabBackend(p);
}

static void *create_qt_clipboard_backend() {
    return new QtClipboardBackend();
}

static void destroy_qt_clipboard_backend(void *p) {
    delete castClipboardBackend(p);
}

static void *create_qt_clipboard_mgr_backend() {
    return new QtClipboardManagerBackend();
}

static void destroy_qt_clipboard_mgr_backend(void *p) {
    delete castClipboardManagerBackend(p);
}

HAVEL_TOOLKIT_PLUGIN_IMPL(qt, "1.0.0", "Qt6 UI toolkit backend")

// Declared in qt_toolkit_factories.cpp (same plugin): installs the in-process
// UI/screenshot/clipboard factories, the pixel service and the screen
// provider the executable used to get from havel_gui.
namespace havel::host {
void qt_toolkit_install_factories();
} // namespace havel::host

namespace {
struct QtToolkitInit {
    QtToolkitInit() {
        HAVEL_TOOLKIT_SET_UI_BACKEND(create_qt_ui_backend, destroy_qt_ui_backend)
        HAVEL_TOOLKIT_SET_SCREENSHOT_BACKEND(create_qt_screenshot_backend, destroy_qt_screenshot_backend)
        HAVEL_TOOLKIT_SET_ALTTAB_BACKEND(create_qt_alttab_backend, destroy_qt_alttab_backend)
        HAVEL_TOOLKIT_SET_CLIPBOARD_BACKEND(create_qt_clipboard_backend, destroy_qt_clipboard_backend)
        HAVEL_TOOLKIT_SET_CLIPBOARD_MGR_BACKEND(create_qt_clipboard_mgr_backend, destroy_qt_clipboard_mgr_backend)
        HAVEL_TOOLKIT_SET_EXT_FUNCTIONS(qt_toolkit_register_functions)
        // The executable no longer links havel_gui, so none of the in-process
        // registrations run anywhere else. install_factories is called by the
        // host right after this plugin is dlopen'd.
        HAVEL_TOOLKIT_SET_FACTORIES(havel::host::qt_toolkit_install_factories)
    }
} init;
}
