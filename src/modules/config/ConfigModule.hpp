/*
 * ConfigModule.hpp
 *
 * Config module for Havel language.
 */
#pragma once
#include "../../havel-lang/runtime/Environment.hpp"
#include "../IHostAPI.hpp"

namespace havel::modules {

void registerConfigModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel::modules
