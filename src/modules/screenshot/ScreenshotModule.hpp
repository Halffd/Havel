#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * ScreenshotModule.hpp
 * 
 * Screenshot module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerScreenshotModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
