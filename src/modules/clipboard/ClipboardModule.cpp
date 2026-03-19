/*
 * ClipboardModule.cpp
 *
 * Clipboard module for Havel language.
 * Host binding - connects language to ClipboardManager and Qt clipboard.
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../host/HostContext.hpp"
#include "gui/ClipboardManager.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QMetaObject>
#include <QTimer>

namespace havel::modules {

void registerClipboardModule(Environment &env,
                             std::shared_ptr<IHostAPI> hostAPI) {
  // Basic clipboard functions don't need ClipboardManager
  // Create clipboard module object
  auto clip = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // Basic clipboard functions (always available)
  // =========================================================================

  (*clip)["get"] = HavelValue(
      makeBuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.get() requires GUI application");
        }
        QClipboard *clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
      }));

  (*clip)["in"] = HavelValue(
      makeBuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.in() requires GUI application");
        }
        QClipboard *clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
      }));

  (*clip)["out"] = HavelValue(
      makeBuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.out() requires GUI application");
        }
        QClipboard *clipboard = QGuiApplication::clipboard();
        return HavelValue(clipboard->text().toStdString());
      }));

  (*clip)["set"] = HavelValue(makeBuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clipboard.set() requires text");
        }
        std::string text =
            args[0].isString()
                ? args[0].asString()
                : std::to_string(static_cast<int>(args[0].asNumber()));

        // Check if Qt application exists
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.set() requires GUI application "
                                   "(not available in REPL mode)");
        }

        QClipboard *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
          return HavelRuntimeError("Failed to access clipboard");
        }

        // Use QTimer to make clipboard operation asynchronous and avoid X11
        // blocking This prevents freezing when browsers or other apps are
        // holding clipboard selection
        QString textToSet = QString::fromStdString(text);
        QTimer::singleShot(
            0, [clipboard, textToSet]() { clipboard->setText(textToSet); });

        return HavelValue(true);
      }));

  (*clip)["clear"] = HavelValue(
      makeBuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError(
              "clipboard.clear() requires GUI application");
        }
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
          return HavelRuntimeError("Failed to access clipboard");
        }

        // Use QTimer to make clipboard operation asynchronous
        QTimer::singleShot(0, [clipboard]() { clipboard->clear(); });

        return HavelValue(nullptr);
      }));

  (*clip)["send"] = HavelValue(makeBuiltinFunction(
      [hostAPI](const std::vector<HavelValue> &args) -> HavelResult {
        // Get text from argument or clipboard
        std::string text;
        if (!args.empty()) {
          text = args[0].isString()
                     ? args[0].asString()
                     : std::to_string(static_cast<int>(args[0].asNumber()));
        } else {
          // Get from clipboard
          if (!QGuiApplication::instance()) {
            return HavelRuntimeError(
                "clipboard.send() requires GUI application");
          }
          QClipboard *clipboard = QGuiApplication::clipboard();
          text = clipboard->text().toStdString();
        }

        // Send clipboard text using IO
        if (hostAPI->GetIO()) {
          // Use async send to avoid blocking
          QTimer::singleShot(
              0, [io = hostAPI->GetIO(), text]() { io->Send(text.c_str()); });
          return HavelValue(true);
        }

        return HavelRuntimeError("clipboard.send() requires IO context");
      }));

  // =========================================================================
  // ClipboardManager functions (only if available)
  // =========================================================================

  if (auto *cm = hostAPI->GetClipboardManager()) {
    auto clipMgrObj =
        std::make_shared<std::unordered_map<std::string, HavelValue>>();

    // Show/hide clipboard manager window
    (*clipMgrObj)["show"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          QMetaObject::invokeMethod(cm, "showAndFocus", Qt::QueuedConnection);
          return HavelValue(nullptr);
        }));

    (*clipMgrObj)["hide"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          cm->hide();
          return HavelValue(nullptr);
        }));

    (*clipMgrObj)["toggle"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          QMetaObject::invokeMethod(cm, "toggleVisibility",
                                    Qt::QueuedConnection);
          return HavelValue(nullptr);
        }));

    // Clipboard history operations
    (*clipMgrObj)["history"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          auto historyArray = std::make_shared<std::vector<HavelValue>>();
          int count = cm->getHistoryCount();
          for (int i = 0; i < count; ++i) {
            QString item = cm->getHistoryItem(i);
            historyArray->push_back(HavelValue(item.toStdString()));
          }
          return HavelValue(historyArray);
        }));

    (*clipMgrObj)["count"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          return HavelValue(cm->getHistoryCount());
        }));

    (*clipMgrObj)["getItem"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &args) -> HavelResult {
          if (args.empty()) {
            return HavelRuntimeError(
                "clipboardmanager.getItem() requires index");
          }
          int index = static_cast<int>(args[0].asNumber());
          QString item = cm->getHistoryItem(index);
          return HavelValue(item.toStdString());
        }));

    (*clipMgrObj)["clear"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          QMetaObject::invokeMethod(cm, "clearHistoryPublic",
                                    Qt::QueuedConnection);
          return HavelValue(nullptr);
        }));

    (*clipMgrObj)["copy"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &args) -> HavelResult {
          if (args.empty()) {
            return HavelRuntimeError("clipboardmanager.copy() requires text");
          }
          std::string text =
              args[0].isString()
                  ? args[0].asString()
                  : std::to_string(static_cast<int>(args[0].asNumber()));
          QMetaObject::invokeMethod(
              cm,
              [cm, text]() {
                cm->addToHistoryPublic(QString::fromStdString(text));
              },
              Qt::QueuedConnection);
          return HavelValue(nullptr);
        }));

    (*clipMgrObj)["paste"] = HavelValue(
        makeBuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
          QClipboard *clipboard = QGuiApplication::clipboard();
          return HavelValue(clipboard->text().toStdString());
        }));

    (*clipMgrObj)["enableHotkeys"] = HavelValue(makeBuiltinFunction(
        [cm](const std::vector<HavelValue> &) -> HavelResult {
          cm->initializeHotkeys();
          return HavelValue(nullptr);
        }));

    env.Define("clipboardmanager", HavelValue(clipMgrObj));
  }

  // Register clipboard module
  env.Define("clipboard", HavelValue(clip));
}

} // namespace havel::modules
