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

void registerScreenshotModule(Environment& env, HostContext& ctx);

} // namespace modules
} // namespace havel
