#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * HotkeyModule.hpp
 *
 * Hotkey management module for Havel language.
 */
#pragma once

#include <memory>

namespace havel {

class Environment;
class Interpreter;

namespace modules {

void registerModuleStub();

// Set the global interpreter reference for hotkey callbacks (uses weak_ptr for safety)
void SetHotkeyInterpreter(std::weak_ptr<Interpreter> interp);

} // namespace modules
} // namespace havel
