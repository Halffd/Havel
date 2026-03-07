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

void registerAppModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
