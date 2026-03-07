/*
 * MediaModule.hpp
 * 
 * Media control module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerMediaModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
