#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * ConfigModule.hpp
 * 
 * Configuration module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerConfigModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
