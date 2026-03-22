#include "../../havel-lang/runtime/HostAPI.hpp"
/*
 * MediaModule.hpp
 * 
 * Media control module for Havel language.
 */
#pragma once


namespace havel {

class Environment;

namespace modules {

void registerMediaModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace modules
} // namespace havel
