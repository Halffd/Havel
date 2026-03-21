/*
 * ClipboardModule.cpp
 *
 * Clipboard module for Havel language.
 * Thin binding layer - delegates to ClipboardService.
 */
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../host/HostContext.hpp"
#include "../../host/ServiceRegistry.hpp"
#include "../../host/clipboard/ClipboardService.hpp"
#include <QGuiApplication>

namespace havel::modules {

void registerClipboardModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  (void)hostAPI;  // Services don't use hostAPI directly

  // Get ClipboardService from registry - FAIL if not registered
  auto& registry = host::ServiceRegistry::instance();
  auto clipboardService = registry.get<host::ClipboardService>();

  if (!clipboardService) {
    throw std::runtime_error("ClipboardService not registered. Call initializeServiceRegistry() first.");
  }

  // Create clipboard module object
  auto clip = std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // Basic clipboard functions - thin wrappers over ClipboardService
  // =========================================================================

  (*clip)["get"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.get() requires GUI application");
        }
        return HavelValue(clipboardService->get());
      }));

  (*clip)["in"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.in() requires GUI application");
        }
        return HavelValue(clipboardService->in());
      }));

  (*clip)["out"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.out() requires GUI application");
        }
        return HavelValue(clipboardService->out());
      }));

  (*clip)["set"] = HavelValue(BuiltinFunction(
      [clipboardService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clipboard.set() requires text");
        }
        std::string text = args[0].isString()
            ? args[0].asString()
            : std::to_string(static_cast<int>(args[0].asNumber()));

        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.set() requires GUI application");
        }

        return HavelValue(clipboardService->setText(text));
      }));

  (*clip)["clear"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        if (!QGuiApplication::instance()) {
          return HavelRuntimeError("clipboard.clear() requires GUI application");
        }
        return HavelValue(clipboardService->clear());
      }));

  // =========================================================================
  // Clipboard history functions - thin wrappers over ClipboardService
  // =========================================================================

  (*clip)["addToHistory"] = HavelValue(BuiltinFunction(
      [clipboardService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clipboard.addToHistory() requires text");
        }
        std::string text = args[0].asString();
        clipboardService->addToHistory(text);
        return HavelValue(nullptr);
      }));

  (*clip)["getHistory"] = HavelValue(BuiltinFunction(
      [clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        auto history = clipboardService->getHistory();
        auto arr = std::make_shared<std::vector<HavelValue>>();
        for (const auto& item : history) {
          arr->push_back(HavelValue(item));
        }
        return HavelValue(arr);
      }));

  (*clip)["getHistoryItem"] = HavelValue(BuiltinFunction(
      [clipboardService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clipboard.getHistoryItem() requires index");
        }
        int index = static_cast<int>(args[0].asNumber());
        return HavelValue(clipboardService->getHistoryItem(index));
      }));

  (*clip)["getHistoryCount"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(static_cast<double>(clipboardService->getHistoryCount()));
      }));

  (*clip)["clearHistory"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        clipboardService->clearHistory();
        return HavelValue(nullptr);
      }));

  (*clip)["setMaxHistorySize"] = HavelValue(BuiltinFunction(
      [clipboardService](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("clipboard.setMaxHistorySize() requires size");
        }
        int size = static_cast<int>(args[0].asNumber());
        clipboardService->setMaxHistorySize(size);
        return HavelValue(nullptr);
      }));

  (*clip)["getMaxHistorySize"] = HavelValue(
      BuiltinFunction([clipboardService](const std::vector<HavelValue> &) -> HavelResult {
        return HavelValue(static_cast<double>(clipboardService->getMaxHistorySize()));
      }));

  // Register clipboard module
  env.Define("clipboard", HavelValue(clip));
}

} // namespace havel::modules
