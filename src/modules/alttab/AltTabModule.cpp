/*
 * AltTabModule.cpp
 *
 * Alt-Tab window switcher module for Havel language.
 * Host binding - connects language to AltTabWindow.
 */
#include "AltTabModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "gui/AltTab.hpp"

namespace havel::modules {

// Static instance - matches the pattern in Interpreter.cpp
static std::unique_ptr<AltTabWindow> altTabWindow;

void registerAltTabModule(Environment &env, std::shared_ptr<IHostAPI>) {
  // Create altTab module object
  auto altTabObj =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // =========================================================================
  // Show Alt-Tab
  // =========================================================================

  (*altTabObj)["show"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (!altTabWindow) {
          altTabWindow = std::make_unique<AltTabWindow>();
        }
        altTabWindow->showAltTab();
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Hide Alt-Tab
  // =========================================================================

  (*altTabObj)["hide"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (altTabWindow) {
          altTabWindow->hideAltTab();
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Next window
  // =========================================================================

  (*altTabObj)["next"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (altTabWindow) {
          altTabWindow->nextWindow();
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Previous window
  // =========================================================================

  (*altTabObj)["prev"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (altTabWindow) {
          altTabWindow->prevWindow();
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Select current window
  // =========================================================================

  (*altTabObj)["select"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (altTabWindow) {
          altTabWindow->selectCurrentWindow();
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Refresh window list
  // =========================================================================

  (*altTabObj)["refresh"] = HavelValue(
      BuiltinFunction([](const std::vector<HavelValue> &) -> HavelResult {
        if (altTabWindow) {
          altTabWindow->refreshWindows();
        }
        return HavelValue(nullptr);
      }));

  // =========================================================================
  // Set thumbnail size
  // =========================================================================

  (*altTabObj)["setThumbnailSize"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError(
              "altTab.setThumbnailSize() requires (width, height)");
        }

        int width = static_cast<int>(args[0].asNumber());
        int height = static_cast<int>(args[1].asNumber());

        if (altTabWindow) {
          altTabWindow->setThumbnailSize(width, height);
        }
        return HavelValue(nullptr);
      }));

  // Register altTab module
  // MIGRATED TO BYTECODE VM: env.Define("altTab", HavelValue(altTabObj));
}

} // namespace havel::modules
