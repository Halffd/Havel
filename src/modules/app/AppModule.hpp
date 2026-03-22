#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * AppModule.hpp
 * 
 * Application module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerAppModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
