/*
 * HotkeyModule.cpp
 *
 * Hotkey management module for Havel language.
 * Provides hotkey control and overlay functions.
 * 
 * THIN BINDING LAYER - Business logic is in HotkeyService
 */
#include "HotkeyModule.hpp"
#include "core/HotkeyManager.hpp"
#include "host/hotkey/HotkeyService.hpp"

namespace havel::modules {

// Global weak reference to interpreter for hotkey callbacks
static std::weak_ptr<Interpreter> g_hotkeyInterpreter;

void SetHotkeyInterpreter(std::weak_ptr<Interpreter> interp) {
  g_hotkeyInterpreter = interp;
  if (auto ptr = interp.lock()) {
    havel::info("Hotkey interpreter set to {}", (void *)ptr.get());
  } else {
    havel::warn("Hotkey interpreter cleared");
  }
}

void registerModuleStub() {
    // STUBBED FOR BYTECODE VM MIGRATION
    // env removed
    // hostAPI removed

}

} // namespace havel::modules
