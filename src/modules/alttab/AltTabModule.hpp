#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * AltTabModule.hpp
 * 
 * Alt-Tab window switcher module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerAltTabModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
