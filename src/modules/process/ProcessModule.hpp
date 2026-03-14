/*
 * ProcessModule.hpp
 * 
 * Process management module for Havel language.
 */
#pragma once
#include "../../havel-lang/runtime/HostAPI.hpp"
#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::modules {

void registerProcessModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel::modules
