#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * BrightnessModule.hpp
 * 
 * Brightness management module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerBrightnessModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
