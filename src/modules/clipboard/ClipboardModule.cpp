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
  // Get ClipboardService from registry
  auto& registry = host::ServiceRegistry::instance();
  auto clipboardService = registry.get<host::ClipboardService>();
  
  if (!clipboardService) {
    // Create service (doesn't need constructor args)
    clipboardService = std::make_shared<host::ClipboardService>();
    registry.registerService<host::ClipboardService>(clipboardService);
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

  // Register clipboard module
  env.Define("clipboard", HavelValue(clip));
}

} // namespace havel::modules
