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

void registerDetectorModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
