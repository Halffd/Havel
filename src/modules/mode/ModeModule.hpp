/*
 * ModeModule.hpp
 * 
 * Mode system module for Havel language.
 * Exposes mode.* API for scripts.
 */
#pragma once
#include "../../havel-lang/runtime/Environment.hpp"
#include "../IHostAPI.hpp"

namespace havel::modules {

void registerModeModule(Environment& env, std::shared_ptr<IHostAPI> hostAPI);

} // namespace havel::modules
