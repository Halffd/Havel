/*
 * SystemModule.hpp
 * 
 * System utilities module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerSystemModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
