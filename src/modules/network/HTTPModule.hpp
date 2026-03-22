#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * HTTPModule.hpp
 * 
 * HTTP client module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerHTTPModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
