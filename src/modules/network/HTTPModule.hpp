#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * HTTPModule.hpp
 * 
 * HTTP client module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerHTTPModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
