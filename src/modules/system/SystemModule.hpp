#include "../../havel-lang/runtime/HostAPI.hpp"
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

void registerSystemModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
