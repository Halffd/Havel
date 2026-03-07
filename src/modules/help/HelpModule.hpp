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

void registerHelpModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
