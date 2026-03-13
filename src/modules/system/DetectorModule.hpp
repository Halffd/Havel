#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * DetectorModule.hpp
 * 
 * System detection module for Havel language.
 */
#pragma once

#include "../../host/HostContext.hpp"

namespace havel {

class Environment;

namespace modules {

void registerDetectorModule(Environment& env, IHostAPI* hostAPI);

} // namespace modules
} // namespace havel
