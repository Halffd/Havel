#include "../../havel-lang/runtime/HostAPI.hpp"
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

void registerMapManagerModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
