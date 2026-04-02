/*
 * UIModule.hpp - UI module for Havel bytecode VM
 *
 * Provides Havel language bindings for UI creation:
 * - ui.window(), ui.btn(), ui.text(), etc.
 * - All functions return UIElement objects
 * - Methods like .show(), .close(), .onClick()
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

/**
 * Register UI module with the VM
 *
 * Usage in Havel:
 *   win = ui.window("My App", width=800, height=600)
 *   win.add(ui.btn("Click", () => print("clicked")))
 *   win.show()
 */
void registerUIModule(compiler::VMApi &api);

} // namespace havel::modules
