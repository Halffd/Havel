#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * ScreenshotModule.hpp
 * 
 * Screenshot module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerScreenshotModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
