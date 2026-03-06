/*
 * PhysicsModule.hpp
 * 
 * Physics constants module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerPhysicsModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
