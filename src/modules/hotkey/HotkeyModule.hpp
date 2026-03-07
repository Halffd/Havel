/*
 * HotkeyModule.hpp
 * 
 * Hotkey management module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerHotkeyModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
