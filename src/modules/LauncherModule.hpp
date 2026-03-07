/*
 * LauncherModule.hpp
 * 
 * Process launching module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerLauncherModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
