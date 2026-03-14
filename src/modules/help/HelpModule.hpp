#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * HelpModule.hpp
 * 
 * Help documentation module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerHelpModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
