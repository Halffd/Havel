#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * AppModule.hpp
 * 
 * Application module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerAppModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
