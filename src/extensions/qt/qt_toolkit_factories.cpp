// qt_toolkit_factories.cpp — the Qt toolkit's factory installer.
//
// The executable no longer links havel_gui: Qt enters the process exclusively
// through havel_toolkit_qt.so. The in-process registrations havel_gui used to
// perform from QtBridge.cpp's static initializer (before main()) now live here
// and run when the host calls the ABI's install_factories slot right after
// this plugin is dlopen'd.
//
// Without this, a Qt-less core binary that loads the plugin would still lack:
//   - the in-process UI backend (QtBackend) behind UIBackendFactory's registry
//   - the in-process screenshot backend (QtScreenshotBackend)
//   - the clipboard backend behind ClipboardBackendFactory's registry
//   - the pixel automation service behind PixelAutomationFactory's slot
//   - the screen pixel provider behind core/automation/ScreenCapture.hpp
//
// All five are idempotent, exactly like the old installers.

#include "core/automation/ScreenCapture.hpp"
#include "extensions/qt/QtClipboardBackend.hpp"
#include "extensions/qt/QtScreenshotBackend.hpp"
#include "host/automation/PixelAutomationFactory.hpp"
#include "host/automation/PixelAutomationService.hpp"
#include "host/clipboard/ClipboardBackendFactory.hpp"
#include "host/ui/QtBackend.hpp"
#include "host/ui/UIBackendFactory.hpp"

#include <mutex>

namespace havel::host {

void installQtUIBackendFactories() {
  static std::once_flag once;
  std::call_once(once, [] {
    registerUIBackendFactory(UIBackend::Api::QT,
                             []() -> std::unique_ptr<UIBackend> {
                               return std::make_unique<QtBackend>();
                             });
    registerInProcessScreenshotBackendFactory(
        "qt", []() -> std::unique_ptr<IScreenshotBackend> {
          return std::make_unique<QtScreenshotBackend>();
        });
  });
}

void installQtClipboardBackend() {
  static std::once_flag once;
  std::call_once(once, [] {
    registerClipboardBackendFactory(
        []() -> std::unique_ptr<IClipboardBackend> {
          return std::make_unique<QtClipboardBackend>();
        });
  });
}

void installQtPixelAutomationFactory() {
  static std::once_flag once;
  std::call_once(once, [] {
    setPixelAutomationFactory([]() -> std::shared_ptr<IPixelAutomation> {
      return std::make_shared<PixelAutomationService>();
    });
  });
}

// The ABI's install_factories entry: the exact set havel_gui's QtBridge static
// initializer ran, so a host that loads this plugin gets byte-for-byte the
// registrations the old in-process Qt side provided.
void qt_toolkit_install_factories() {
  installQtUIBackendFactories();
  installQtClipboardBackend();
  installQtPixelAutomationFactory();
  havel::qt::installQtScreenCapture();
}

} // namespace havel::host