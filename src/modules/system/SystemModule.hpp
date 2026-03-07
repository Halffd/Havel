/*
 * SystemModule.hpp
 * 
 * System information module for Havel language.
 * Provides CPU, memory, OS, and temperature information.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerSystemModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
