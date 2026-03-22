#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * TimerModule.hpp
 * 
 * Timer module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerTimerModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
