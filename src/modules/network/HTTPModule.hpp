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

void registerHTTPModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
