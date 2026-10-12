// QtBridge.cpp — the Qt bridge: every host function whose implementation needs
// a Qt class.
//
// Compiled into havel_gui only. libhavel_core.a and libhavel_lang*.a are
// embedded by Qt-free hosts (havel-wm, the cranelift AOT shim, the AOT
// runtime), so none of this may sit in a core-facing library: QClipboard and
// GUIManager are QObjects, and their definitions drag in every Qt header.
//
// The core installs this bridge through the selection slot in
// src/host/module/BridgeSelection.hpp, which the application entry point fills
// in. That is a strong reference from an app translation unit, so the linker
// must pull this object out of libhavel_gui.a. A weak undefined symbol is not
// enough: ld does not extract an archive member to satisfy a weak reference,
// so a weak hook registers nothing unless the defining object happens to be
// extracted for some unrelated reason.
//
// Qt comes in through the repo's include wrapper (the managers below include
// qt.hpp themselves); do not #undef anything here.

#include "../../ModularHostBridges.hpp"
#include "../BridgesInternal.hpp"
#include "core/automation/ScreenCapture.hpp"
#include "extensions/gui/clipboard_manager/ClipboardManager.hpp"
#include "extensions/gui/common/GUIManager.hpp"
#include "extensions/qt/QtClipboardBackend.hpp"
#include "extensions/qt/QtScreenshotBackend.hpp"
#include "host/clipboard/ClipboardBackendFactory.hpp"
#include "host/automation/PixelAutomationFactory.hpp"
#include "host/automation/PixelAutomationService.hpp"
#include "host/ui/QtBackend.hpp"
#include "host/ui/UIBackendFactory.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/runtime/HostContext.hpp"

#include <mutex>

namespace havel::compiler {

// ============================================================================
// clipboard.* and io.getClipboard
//
// These call QClipboard through the ClipboardManager service. They used to be
// glue at the bottom of extensions/gui/clipboard_manager/ClipboardManager.cpp
// (a service, not a bridge); they live with the rest of the Qt bridge now.
// ============================================================================

static Value clipboardBridgeGet(const std::vector<Value> &args,
                                const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm || !ctx->clipboardManager) {
    return Value::makeNull();
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return Value::makeNull();
  }
  const QString text = clipboard->text();
  // Clipboard read returns a heap-allocated string so callers keep a stable
  // reference across collection cycles.
  auto ref = vm->getHeap().allocateString(text.toStdString());
  return Value::makeStringId(ref.id);
}

static Value clipboardBridgeSet(const std::vector<Value> &args,
                                const HostContext *ctx) {
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm || args.empty() || !ctx->clipboardManager) {
    return Value::makeBool(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return Value::makeBool(false);
  }
  const std::string text = vm->resolveStringKey(args[0]);
  clipboard->setText(QString::fromStdString(text));
  return Value::makeBool(true);
}

static Value clipboardBridgeClear(const std::vector<Value> &args,
                                  const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->clipboardManager) {
    return Value::makeBool(false);
  }
  auto *clipboard = ctx->clipboardManager->getClipboard();
  if (!clipboard) {
    return Value::makeBool(false);
  }
  clipboard->clear();
  return Value::makeBool(true);
}

// ============================================================================
// gui.notify
// ============================================================================

// Out-of-class definition of the static member declared in
// ModularHostBridges.hpp. It lives here rather than in UIBridge.cpp because
// GUIManager is a QObject.
Value UIBridge::handleGUINotify(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx || !ctx->guiManager) {
    return Value::makeBool(false);
  }
  if (args.size() < 2) {
    throw std::runtime_error("gui.notify() requires title and message");
  }

  const std::string title = strVal(args[0], ctx->vm);
  const std::string message = strVal(args[1], ctx->vm);

  std::string icon = "info";
  int durationMs = 0;
  if (args.size() > 2) {
    icon = strVal(args[2], ctx->vm);
  }
  if (args.size() > 3 && args[3].isInt()) {
    durationMs = static_cast<int>(args[3].asInt());
  }

  ctx->guiManager->showNotification(title, message, icon, durationMs);
  return Value::makeBool(true);
}

} // namespace havel::compiler

// ============================================================================
// in-process Qt UI backend registration
//
// These used to be constructed inline by UIManager::createBackend behind
// HAVE_QT_EXTENSION, which is what dragged Qt into libhavel_core.a. The core
// now only knows about the registry in host/ui/UIBackendFactory.hpp.
// ============================================================================

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

} // namespace havel::host

// The Qt UI backend has to be constructible before any host code calls
// UIManager::backend(), which selects a backend on first use and only tries
// once. HavelLauncher::execute() makes exactly that call before it runs the
// script pipeline, so waiting for installQtBridge to run would leave the
// launcher with no backend at all.
//
// The screen provider behind pixel automation and the clipboard backend need
// the same treatment: the language host can read pixels through HostAPI.cpp
// and the clipboard modules construct ClipboardService with no bridge
// installed.
//
// The application references installQtBridge strongly, so this translation unit
// is always extracted from libhavel_gui.a, and an initializer here runs before
// main(). installQtUIBackendFactories and installQtScreenCapture are
// idempotent, so installQtBridge calling them again is harmless.
namespace {
const bool g_qt_ui_backends_registered = [] {
  havel::host::installQtUIBackendFactories();
  havel::host::installQtClipboardBackend();
  havel::host::installQtPixelAutomationFactory();
  havel::qt::installQtScreenCapture();
  return true;
}();
} // namespace

namespace havel::compiler {

// ============================================================================
// entry point used by the bridge selection slot
// ============================================================================

void installQtBridge(PipelineOptions &options, const HostContext *ctx) {
  havel::host::installQtUIBackendFactories();
  havel::host::installQtClipboardBackend();
  havel::host::installQtPixelAutomationFactory();
  havel::qt::installQtScreenCapture();
  options.host_functions["clipboard.get"] = [ctx](const auto &args) {
    return clipboardBridgeGet(args, ctx);
  };
  options.host_functions["clipboard.set"] = [ctx](const auto &args) {
    return clipboardBridgeSet(args, ctx);
  };
  options.host_functions["clipboard.clear"] = [ctx](const auto &args) {
    return clipboardBridgeClear(args, ctx);
  };
  options.host_functions["io.getClipboard"] = [ctx](const auto &args) {
    return clipboardBridgeGet(args, ctx);
  };
  options.host_functions["gui.notify"] = [ctx](const auto &args) {
    return UIBridge::handleGUINotify(args, ctx);
  };
}

} // namespace havel::compiler
