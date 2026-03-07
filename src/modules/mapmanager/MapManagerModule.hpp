/*
 * MapManagerModule.hpp
 * 
 * Map Manager module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerMapManagerModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
