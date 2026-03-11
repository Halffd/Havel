/*
 * HotkeyModule.hpp
 *
 * Hotkey management module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;
class Interpreter;

namespace modules {

void registerHotkeyModule(Environment& env, HostContext& ctx);

// Set the global interpreter reference for hotkey callbacks
void SetHotkeyInterpreter(Interpreter* interp);

} // namespace modules
} // namespace havel
