/*
 * ObjectModule.hpp
 *
 * Object manipulation functions for Havel standard library.
 */
#pragma once

#include "../havel-lang/runtime/Environment.hpp"

namespace havel::stdlib {

void registerObjectModule(Environment* env);

} // namespace havel::stdlib
