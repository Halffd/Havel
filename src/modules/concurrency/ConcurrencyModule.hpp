/*
 * ConcurrencyModule.hpp
 * 
 * Concurrency module for Havel language.
 */
#pragma once
#include "../../havel-lang/runtime/Environment.hpp"
#include "../../havel-lang/runtime/HostAPI.hpp"

namespace havel::modules {

void registerConcurrencyModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel::modules
