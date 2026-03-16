/*
 * WindowModule.hpp
 * 
 * Window query module for Havel language.
 */
#pragma once
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/HostAPI.hpp"

namespace havel::modules {

void registerWindowQueryModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel::modules
