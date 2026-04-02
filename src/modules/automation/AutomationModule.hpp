/*
 * AutomationModule.hpp - Automation module for Havel scripting
 * 
 * Exposes automation functionality to scripts:
 * - autoClicker(button, interval) - Auto clicker
 * - autoRunner(direction, interval) - Auto key runner (up/down/left/right)
 * - autoKeyPress(key, interval) - Auto key presser
 * - chainedTask(name, actions, loop) - Chained timed actions
 */
#pragma once

#include "havel-lang/compiler/vm/VMApi.hpp"

namespace havel::modules {

void registerAutomationModule(compiler::VMApi &api);

} // namespace havel::modules
