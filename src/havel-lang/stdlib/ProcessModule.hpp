/*
 * ProcessModule.hpp
 * 
 * Async process execution for Havel standard library.
 */
#pragma once

#include "../runtime/Environment.hpp"

namespace havel::stdlib {

void registerProcessModule(Environment* env);

} // namespace havel::stdlib
