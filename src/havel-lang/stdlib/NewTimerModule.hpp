/*
 * TimerModule.hpp
 *
 * Timer module for Havel VM
 * Provides timer operations using the new VApi system.
 */
#pragma once

#include "../compiler/bytecode/VMApi.hpp"

namespace havel::stdlib {

/**
 * Register timer module functions with VApi
 */
void registerNewTimerModule(havel::compiler::VMApi &api);

} // namespace havel::stdlib
