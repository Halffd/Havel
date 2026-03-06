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

void registerWindowModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
