#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * WindowModule.hpp
 * 
 * Window management module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerWindowModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
