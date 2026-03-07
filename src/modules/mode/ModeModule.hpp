/*
 * ModeModule.hpp
 * 
 * Mode system module for Havel language.
 * Provides mode switching for conditional hotkeys.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerModeModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
